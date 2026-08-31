#include "DatabaseManager.h"
#include <QDir>
#include <QVariant>
#include <QDebug>

namespace core {

DatabaseManager::DatabaseManager(QObject *parent) : QObject(parent) {
}

DatabaseManager::~DatabaseManager() {
    if (m_db.isOpen()) m_db.close();
}

bool DatabaseManager::init(const QString &dbPath) {
    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        qDebug() << "DatabaseManager: Error opening database:" << m_db.lastError().text();
        return false;
    }

    return setupSchema();
}

bool DatabaseManager::setupSchema() {
    QSqlQuery q(m_db);
    
    // Devices table
    if (!q.exec("CREATE TABLE IF NOT EXISTS devices ("
                "mac TEXT PRIMARY KEY, "
                "last_ip TEXT, "
                "hostname TEXT, "
                "vendor TEXT, "
                "alias TEXT, "
                "status TEXT DEFAULT 'Offline', "
                "is_known INTEGER DEFAULT 0, "
                "last_seen TEXT)")) {
        qDebug() << "DatabaseManager: Schema error (devices):" << q.lastError().text();
        return false;
    }

    // Events table
    if (!q.exec("CREATE TABLE IF NOT EXISTS events ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "timestamp TEXT, "
                "type INTEGER, "
                "message TEXT, "
                "source_ip TEXT)")) {
        qDebug() << "DatabaseManager: Schema error (events):" << q.lastError().text();
        return false;
    }

    // Blacklist table (Active Access Control)
    if (!q.exec("CREATE TABLE IF NOT EXISTS blacklist ("
                "mac TEXT PRIMARY KEY, "
                "reason TEXT, "
                "blocked_at TEXT)")) {
        qDebug() << "DatabaseManager: Schema error (blacklist):" << q.lastError().text();
        return false;
    }

    // Whitelist table (Strict Mode)
    if (!q.exec("CREATE TABLE IF NOT EXISTS whitelist ("
                "mac TEXT PRIMARY KEY, "
                "added_at TEXT)")) {
        qDebug() << "DatabaseManager: Schema error (whitelist):" << q.lastError().text();
        return false;
    }

    return true;
}

void DatabaseManager::saveDevice(const Device &d) {
    if (d.mac().isEmpty()) return;

    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO devices (mac, last_ip, hostname, vendor, alias, status, is_known, last_seen) "
              "VALUES (:mac, :ip, :host, :vendor, :alias, :status, :known, :seen)");
    q.bindValue(":mac", d.mac());
    q.bindValue(":ip", d.ip());
    q.bindValue(":host", d.hostname());
    q.bindValue(":vendor", d.vendor());
    q.bindValue(":alias", d.alias());
    q.bindValue(":status", d.status());
    q.bindValue(":known", d.isKnown() ? 1 : 0);
    q.bindValue(":seen", d.lastSeen().toString(Qt::ISODate));

    if (!q.exec()) {
        qDebug() << "DatabaseManager: Save error (device):" << q.lastError().text();
    }
}

void DatabaseManager::removeDevice(const QString &ip) {
    if (ip.isEmpty()) return;
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM devices WHERE last_ip = :ip");
    q.bindValue(":ip", ip);
    if (!q.exec()) {
        qDebug() << "DatabaseManager: Remove error (device):" << q.lastError().text();
    }
}

QList<Device> DatabaseManager::getAllDevices() {
    QList<Device> list;
    // Use ALTER TABLE to add the status column for existing DBs that predate this change
    QSqlQuery migrate(m_db);
    migrate.exec("ALTER TABLE devices ADD COLUMN status TEXT DEFAULT 'Offline'");
    // Ignore error — it just means the column already exists

    QSqlQuery q("SELECT * FROM devices", m_db);

    while (q.next()) {
        Device d;
        d.setMac(q.value("mac").toString());
        d.setIp(q.value("last_ip").toString());
        d.setHostname(q.value("hostname").toString());
        d.setVendor(q.value("vendor").toString());
        d.setAlias(q.value("alias").toString());
        // Load persisted status; treat empty/null as Offline so startup is conservative
        QString st = q.value("status").toString();
        d.setStatus(st.isEmpty() ? "Offline" : st);
        d.setIsKnown(q.value("is_known").toInt() == 1);
        QDateTime seen = QDateTime::fromString(q.value("last_seen").toString(), Qt::ISODate);
        // If DB has a valid timestamp, use it; otherwise mark as epoch so cleanup fires quickly
        d.setLastSeen(seen.isValid() ? seen : QDateTime::fromSecsSinceEpoch(0));
        list.append(d);
    }
    return list;
}

void DatabaseManager::updateAlias(const QString &mac, const QString &alias) {
    QSqlQuery q(m_db);
    q.prepare("UPDATE devices SET alias = :alias WHERE mac = :mac");
    q.bindValue(":alias", alias);
    q.bindValue(":mac", mac);
    q.exec();
}

void DatabaseManager::saveEvent(const NetworkEvent &e) {
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO events (timestamp, type, message, source_ip) VALUES (:ts, :type, :msg, :ip)");
    q.bindValue(":ts", e.timestamp.toString(Qt::ISODate));
    q.bindValue(":type", (int)e.type);
    q.bindValue(":msg", e.message);
    q.bindValue(":ip", e.sourceIp);
    q.exec();
}

QList<NetworkEvent> DatabaseManager::getAllEvents(int limit) {
    QList<NetworkEvent> list;
    QSqlQuery q(m_db);
    q.prepare("SELECT * FROM events ORDER BY id DESC LIMIT :limit");
    q.bindValue(":limit", limit);
    
    if (q.exec()) {
        while (q.next()) {
            NetworkEvent e;
            e.timestamp = QDateTime::fromString(q.value("timestamp").toString(), Qt::ISODate);
            e.type = static_cast<NetworkEvent::Type>(q.value("type").toInt());
            e.message = q.value("message").toString();
            e.sourceIp = q.value("source_ip").toString();
            list.append(e);
        }
    }
    return list;
}

// --- Blacklist / Whitelist DAO ---

void DatabaseManager::addToBlacklist(const QString &mac, const QString &reason) {
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO blacklist (mac, reason, blocked_at) VALUES (:mac, :reason, :at)");
    q.bindValue(":mac", mac.toLower());
    q.bindValue(":reason", reason);
    q.bindValue(":at", QDateTime::currentDateTime().toString(Qt::ISODate));
    q.exec();
}

void DatabaseManager::removeFromBlacklist(const QString &mac) {
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM blacklist WHERE mac = :mac");
    q.bindValue(":mac", mac.toLower());
    q.exec();
}

bool DatabaseManager::isBlacklisted(const QString &mac) {
    QSqlQuery q(m_db);
    q.prepare("SELECT 1 FROM blacklist WHERE mac = :mac");
    q.bindValue(":mac", mac.toLower());
    if (q.exec() && q.next()) return true;
    return false;
}

void DatabaseManager::addToWhitelist(const QString &mac) {
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO whitelist (mac, added_at) VALUES (:mac, :at)");
    q.bindValue(":mac", mac.toLower());
    q.bindValue(":at", QDateTime::currentDateTime().toString(Qt::ISODate));
    q.exec();
}

void DatabaseManager::removeFromWhitelist(const QString &mac) {
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM whitelist WHERE mac = :mac");
    q.bindValue(":mac", mac.toLower());
    q.exec();
}

bool DatabaseManager::isWhitelisted(const QString &mac) {
    QSqlQuery q(m_db);
    q.prepare("SELECT 1 FROM whitelist WHERE mac = :mac");
    q.bindValue(":mac", mac.toLower());
    if (q.exec() && q.next()) return true;
    return false;
}

} // namespace core
