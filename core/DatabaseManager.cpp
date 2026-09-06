#include "DatabaseManager.h"
#include <QDir>
#include <QVariant>
#include <QDebug>
#include <QSqlError>
#include <QDateTime>
#include <QStandardPaths>
#include <QMutexLocker>

namespace core {

// ── Retention configuration (seconds) ────────────────────────────────────────
// Raw 5-second samples → kept 2 hours (7200 s)
static constexpr qint64 kRawRetentionSecs    = 2 * 60 * 60;
// Minute-level aggregates → kept 7 days
static constexpr qint64 kMinuteRetentionSecs = 7 * 24 * 60 * 60;
// Hourly aggregates → kept 30 days
static constexpr qint64 kHourRetentionSecs   = 30 * 24 * 60 * 60;
// Daily aggregates → kept forever (no pruning)

DatabaseManager::DatabaseManager(QObject *parent) : QObject(parent) {}

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

    // Enable WAL mode for better concurrent write performance
    QSqlQuery pragma(m_db);
    pragma.exec("PRAGMA journal_mode=WAL");
    pragma.exec("PRAGMA synchronous=NORMAL");
    pragma.exec("PRAGMA cache_size=4000");

    return setupSchema();
}

bool DatabaseManager::setupSchema() {
    QSqlQuery q(m_db);

    // ── Core tables (original schema) ────────────────────────────────────
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

    if (!q.exec("CREATE TABLE IF NOT EXISTS events ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "timestamp TEXT, "
                "type INTEGER, "
                "message TEXT, "
                "source_ip TEXT)")) {
        qDebug() << "DatabaseManager: Schema error (events):" << q.lastError().text();
        return false;
    }

    if (q.exec("PRAGMA table_info(blacklist)")) {
        bool hasNetworkId = false;
        while (q.next()) {
            if (q.value(1).toString() == "network_id") {
                hasNetworkId = true;
                break;
            }
        }
        if (!hasNetworkId) {
            q.exec("CREATE TABLE blacklist_new ("
                   "network_id TEXT, "
                   "mac TEXT, "
                   "reason TEXT, "
                   "blocked_at TEXT, "
                   "PRIMARY KEY (network_id, mac))");
            q.exec("INSERT INTO blacklist_new (network_id, mac, reason, blocked_at) SELECT '', mac, reason, blocked_at FROM blacklist");
            q.exec("DROP TABLE blacklist");
            q.exec("ALTER TABLE blacklist_new RENAME TO blacklist");
        }
    } else {
        q.exec("CREATE TABLE IF NOT EXISTS blacklist ("
               "network_id TEXT, "
               "mac TEXT, "
               "reason TEXT, "
               "blocked_at TEXT, "
               "PRIMARY KEY (network_id, mac))");
    }

    if (q.exec("PRAGMA table_info(whitelist)")) {
        bool hasNetworkId = false;
        while (q.next()) {
            if (q.value(1).toString() == "network_id") {
                hasNetworkId = true;
                break;
            }
        }
        if (!hasNetworkId) {
            q.exec("CREATE TABLE whitelist_new ("
                   "network_id TEXT, "
                   "mac TEXT, "
                   "added_at TEXT, "
                   "PRIMARY KEY (network_id, mac))");
            q.exec("INSERT INTO whitelist_new (network_id, mac, added_at) SELECT '', mac, added_at FROM whitelist");
            q.exec("DROP TABLE whitelist");
            q.exec("ALTER TABLE whitelist_new RENAME TO whitelist");
        }
    } else {
        q.exec("CREATE TABLE IF NOT EXISTS whitelist ("
               "network_id TEXT, "
               "mac TEXT, "
               "added_at TEXT, "
               "PRIMARY KEY (network_id, mac))");
    }

    // ── Bandwidth sample tables ───────────────────────────────────────────
    // Shared schema — same structure for all resolutions.
    // rx_bytes / tx_bytes are CUMULATIVE totals at that timestamp (not deltas),
    // matching the BandwidthEngine's MacRecord.rxTotal / txTotal approach.
    auto createBwTable = [&](const QString &name) -> bool {
        bool ok = q.exec(QString(
            "CREATE TABLE IF NOT EXISTS %1 ("
            "  id       INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  mac      TEXT    NOT NULL,"
            "  ts       INTEGER NOT NULL,"
            "  rx_bytes INTEGER NOT NULL DEFAULT 0,"
            "  tx_bytes INTEGER NOT NULL DEFAULT 0,"
            "  rx_rate  INTEGER NOT NULL DEFAULT 0,"
            "  tx_rate  INTEGER NOT NULL DEFAULT 0"
            ")").arg(name));
        if (!ok) {
            qDebug() << "DatabaseManager: Schema error (" << name << "):" << q.lastError().text();
            return false;
        }
        // Index on (mac, ts) for fast time-range queries
        q.exec(QString("CREATE INDEX IF NOT EXISTS idx_%1_mac_ts ON %1(mac, ts)").arg(name));
        return true;
    };

    if (!createBwTable("bw_samples")) return false;  // 5-second raw
    if (!createBwTable("bw_minutes")) return false;  // 1-minute aggregated
    if (!createBwTable("bw_hours"))   return false;  // 1-hour aggregated
    if (!createBwTable("bw_days"))    return false;  // daily aggregated

    // ── IP history table ─────────────────────────────────────────────────
    if (!q.exec("CREATE TABLE IF NOT EXISTS ip_history ("
                "mac        TEXT    NOT NULL,"
                "ip         TEXT    NOT NULL,"
                "first_seen INTEGER NOT NULL,"
                "last_seen  INTEGER NOT NULL,"
                "PRIMARY KEY (mac, ip))")) {
        qDebug() << "DatabaseManager: Schema error (ip_history):" << q.lastError().text();
        return false;
    }

    // ── Schema migrations — add new columns if they don't exist yet ─────────
    // These are no-ops on fresh databases (columns already in CREATE TABLE);
    // on existing databases they add the columns gracefully.
    q.exec("ALTER TABLE devices ADD COLUMN device_type TEXT DEFAULT ''");
    q.exec("ALTER TABLE devices ADD COLUMN latency_ms INTEGER DEFAULT 9999");

    return true;
}

// ── Device persistence (unchanged) ───────────────────────────────────────────
void DatabaseManager::saveDevice(const Device &d) {
    if (d.mac().isEmpty()) return;

    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO devices "
              "(mac, last_ip, hostname, vendor, alias, status, is_known, last_seen, device_type, latency_ms) "
              "VALUES (:mac, :ip, :host, :vendor, :alias, :status, :known, :seen, :dtype, :lms)");
    q.bindValue(":mac",    d.mac());
    q.bindValue(":ip",     d.ip());
    q.bindValue(":host",   d.hostname());
    q.bindValue(":vendor", d.vendor());
    q.bindValue(":alias",  d.alias());
    q.bindValue(":status", d.status());
    q.bindValue(":known",  d.isKnown() ? 1 : 0);
    q.bindValue(":seen",   d.lastSeen().toString(Qt::ISODate));
    q.bindValue(":dtype",  d.deviceType());
    q.bindValue(":lms",    d.latencyMs());

    if (!q.exec())
        qDebug() << "DatabaseManager: Save error (device):" << q.lastError().text();
}

void DatabaseManager::removeDevice(const QString &ip) {
    if (ip.isEmpty()) return;
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM devices WHERE last_ip = :ip");
    q.bindValue(":ip", ip);
    if (!q.exec())
        qDebug() << "DatabaseManager: Remove error (device):" << q.lastError().text();
}

QList<Device> DatabaseManager::getAllDevices() {
    QList<Device> list;
    QSqlQuery migrate(m_db);
    migrate.exec("ALTER TABLE devices ADD COLUMN status TEXT DEFAULT 'Offline'");
    // Idempotent migrations for newer columns
    migrate.exec("ALTER TABLE devices ADD COLUMN device_type TEXT DEFAULT ''");
    migrate.exec("ALTER TABLE devices ADD COLUMN latency_ms INTEGER DEFAULT 9999");

    QSqlQuery q("SELECT * FROM devices", m_db);
    while (q.next()) {
        Device d;
        d.setMac(q.value("mac").toString());
        d.setIp(q.value("last_ip").toString());
        d.setHostname(q.value("hostname").toString());
        d.setVendor(q.value("vendor").toString());
        d.setAlias(q.value("alias").toString());
        QString st = q.value("status").toString();
        d.setStatus(st.isEmpty() ? "Offline" : st);
        d.setIsKnown(q.value("is_known").toInt() == 1);
        QDateTime seen = QDateTime::fromString(q.value("last_seen").toString(), Qt::ISODate);
        d.setLastSeen(seen.isValid() ? seen : QDateTime::fromSecsSinceEpoch(0));
        // New fields (graceful: value() returns invalid QVariant if column absent)
        QVariant dtVar = q.value("device_type");
        if (dtVar.isValid()) d.setDeviceType(dtVar.toString());
        QVariant lmsVar = q.value("latency_ms");
        if (lmsVar.isValid() && !lmsVar.isNull()) {
            quint32 lms = lmsVar.toUInt();
            if (lms != 9999) d.setLatencyMs(lms); // restore last known latency
        }
        list.append(d);
    }
    return list;
}

void DatabaseManager::updateAlias(const QString &mac, const QString &alias) {
    QSqlQuery q(m_db);
    q.prepare("UPDATE devices SET alias = :alias WHERE mac = :mac");
    q.bindValue(":alias", alias);
    q.bindValue(":mac",   mac);
    q.exec();
}

// ── Event persistence (unchanged) ────────────────────────────────────────────
void DatabaseManager::saveEvent(const NetworkEvent &e) {
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO events (timestamp, type, message, source_ip) VALUES (:ts, :type, :msg, :ip)");
    q.bindValue(":ts",   e.timestamp.toString(Qt::ISODate));
    q.bindValue(":type", (int)e.type);
    q.bindValue(":msg",  e.message);
    q.bindValue(":ip",   e.sourceIp);
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
            e.type      = static_cast<NetworkEvent::Type>(q.value("type").toInt());
            e.message   = q.value("message").toString();
            e.sourceIp  = q.value("source_ip").toString();
            list.append(e);
        }
    }
    return list;
}

// ── Blacklist / Whitelist DAO (updated) ─────────────────────────────────────
void DatabaseManager::addToBlacklist(const QString &networkId, const QString &mac, const QString &reason) {
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO blacklist (network_id, mac, reason, blocked_at) "
              "VALUES (:nid, :mac, :reason, :ts)");
    q.bindValue(":nid", networkId);
    q.bindValue(":mac", mac.toLower());
    q.bindValue(":reason", reason);
    q.bindValue(":ts", QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!q.exec()) {
        qWarning() << "Failed to add to blacklist:" << q.lastError().text();
    }
}

void DatabaseManager::removeFromBlacklist(const QString &networkId, const QString &mac) {
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM blacklist WHERE network_id = :nid AND mac = :mac");
    q.bindValue(":nid", networkId);
    q.bindValue(":mac", mac.toLower());
    q.exec();
}

bool DatabaseManager::isBlacklisted(const QString &networkId, const QString &mac) {
    QSqlQuery q(m_db);
    q.prepare("SELECT 1 FROM blacklist WHERE network_id = :nid AND mac = :mac");
    q.bindValue(":nid", networkId);
    q.bindValue(":mac", mac.toLower());
    if (q.exec() && q.next()) return true;
    return false;
}

void DatabaseManager::updateBlacklistReason(const QString &networkId, const QString &mac, const QString &newReason) {
    QSqlQuery q(m_db);
    q.prepare("UPDATE blacklist SET reason = :reason WHERE network_id = :nid AND mac = :mac");
    q.bindValue(":reason", newReason);
    q.bindValue(":nid", networkId);
    q.bindValue(":mac", mac.toLower());
    if (!q.exec()) {
        qWarning() << "Failed to update blacklist reason:" << q.lastError().text();
    }
}

QList<BlacklistEntry> DatabaseManager::getBlacklist(const QString &networkId) {
    QList<BlacklistEntry> list;
    QSqlQuery q(m_db);
    q.prepare("SELECT mac, reason, blocked_at FROM blacklist WHERE network_id = :nid ORDER BY blocked_at DESC");
    q.bindValue(":nid", networkId);
    if (q.exec()) {
        while (q.next()) {
            BlacklistEntry e;
            e.mac       = q.value(0).toString();
            e.reason    = q.value(1).toString();
            e.blockedAt = q.value(2).toString();
            list.append(e);
        }
    }
    return list;
}

void DatabaseManager::clearHistoricalDevices(const QString &currentNetworkId) {
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM blacklist WHERE network_id != :nid AND network_id != ''");
    q.bindValue(":nid", currentNetworkId);
    if (!q.exec()) {
        qWarning() << "Failed to clear historical blacklist:" << q.lastError().text();
    }
}

void DatabaseManager::addToWhitelist(const QString &networkId, const QString &mac) {
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO whitelist (network_id, mac, added_at) VALUES (:nid, :mac, :ts)");
    q.bindValue(":nid", networkId);
    q.bindValue(":mac", mac.toLower());
    q.bindValue(":ts", QDateTime::currentDateTime().toString(Qt::ISODate));
    q.exec();
}

void DatabaseManager::removeFromWhitelist(const QString &networkId, const QString &mac) {
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM whitelist WHERE network_id = :nid AND mac = :mac");
    q.bindValue(":nid", networkId);
    q.bindValue(":mac", mac.toLower());
    q.exec();
}

bool DatabaseManager::isWhitelisted(const QString &networkId, const QString &mac) {
    QSqlQuery q(m_db);
    q.prepare("SELECT 1 FROM whitelist WHERE network_id = :nid AND mac = :mac");
    q.bindValue(":nid", networkId);
    q.bindValue(":mac", mac.toLower());
    if (q.exec() && q.next()) return true;
    return false;
}

// ── Bandwidth history DAO ─────────────────────────────────────────────────────
void DatabaseManager::insertBwSample(const BwSample &s) {
    if (s.mac.isEmpty()) return;
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO bw_samples (mac, ts, rx_bytes, tx_bytes, rx_rate, tx_rate) "
              "VALUES (:mac, :ts, :rx, :tx, :rxr, :txr)");
    q.bindValue(":mac", s.mac);
    q.bindValue(":ts",  s.timestamp);
    q.bindValue(":rx",  (qint64)s.rxBytes);
    q.bindValue(":tx",  (qint64)s.txBytes);
    q.bindValue(":rxr", (int)s.rxRate);
    q.bindValue(":txr", (int)s.txRate);
    if (!q.exec())
        qDebug() << "DatabaseManager: insertBwSample error:" << q.lastError().text();
}

QList<BwSample> DatabaseManager::getBwSamples(const QString &mac, qint64 fromTs, qint64 toTs,
                                                const QString &table)
{
    QList<BwSample> list;
    QSqlQuery q(m_db);
    q.prepare(QString("SELECT mac, ts, rx_bytes, tx_bytes, rx_rate, tx_rate "
                      "FROM %1 WHERE mac = :mac AND ts >= :from AND ts <= :to "
                      "ORDER BY ts ASC").arg(table));
    q.bindValue(":mac",  mac);
    q.bindValue(":from", fromTs);
    q.bindValue(":to",   toTs);
    if (q.exec()) {
        while (q.next()) {
            BwSample s;
            s.mac       = q.value(0).toString();
            s.timestamp = q.value(1).toLongLong();
            s.rxBytes   = (quint64)q.value(2).toLongLong();
            s.txBytes   = (quint64)q.value(3).toLongLong();
            s.rxRate    = (quint32)q.value(4).toUInt();
            s.txRate    = (quint32)q.value(5).toUInt();
            list.append(s);
        }
    }
    return list;
}

QList<DatabaseManager::TopTalkerEntry>
DatabaseManager::getTopTalkers(int limit, qint64 periodSeconds, const QString &table)
{
    QList<TopTalkerEntry> list;
    QSqlQuery q(m_db);

    QString sql;
    if (periodSeconds <= 0) {
        // All-time: take the MAX rx/tx_bytes per device (cumulative totals)
        sql = QString("SELECT mac, MAX(rx_bytes) as rx, MAX(tx_bytes) as tx, "
                      "MAX(rx_bytes)+MAX(tx_bytes) as total "
                      "FROM %1 GROUP BY mac ORDER BY total DESC LIMIT :lim").arg(table);
        q.prepare(sql);
        q.bindValue(":lim", limit);
    } else {
        qint64 cutoff = QDateTime::currentSecsSinceEpoch() - periodSeconds;
        sql = QString("SELECT mac, MAX(rx_bytes)-MIN(rx_bytes) as rx, "
                      "MAX(tx_bytes)-MIN(tx_bytes) as tx, "
                      "(MAX(rx_bytes)-MIN(rx_bytes))+(MAX(tx_bytes)-MIN(tx_bytes)) as total "
                      "FROM %1 WHERE ts >= :cutoff "
                      "GROUP BY mac ORDER BY total DESC LIMIT :lim").arg(table);
        q.prepare(sql);
        q.bindValue(":cutoff", cutoff);
        q.bindValue(":lim",    limit);
    }

    if (q.exec()) {
        while (q.next()) {
            TopTalkerEntry e;
            e.mac     = q.value(0).toString();
            e.rxBytes = (quint64)q.value(1).toLongLong();
            e.txBytes = (quint64)q.value(2).toLongLong();
            e.total   = (quint64)q.value(3).toLongLong();
            list.append(e);
        }
    }
    return list;
}

void DatabaseManager::pruneOldSamples()
{
    // First aggregate before pruning so we don't lose data
    aggregateSamples();

    qint64 now = QDateTime::currentSecsSinceEpoch();
    QSqlQuery q(m_db);

    // Prune raw samples older than 2 hours
    q.prepare("DELETE FROM bw_samples WHERE ts < :cutoff");
    q.bindValue(":cutoff", now - kRawRetentionSecs);
    q.exec();

    // Prune minute-level data older than 7 days
    q.prepare("DELETE FROM bw_minutes WHERE ts < :cutoff");
    q.bindValue(":cutoff", now - kMinuteRetentionSecs);
    q.exec();

    // Prune hourly data older than 30 days
    q.prepare("DELETE FROM bw_hours WHERE ts < :cutoff");
    q.bindValue(":cutoff", now - kHourRetentionSecs);
    q.exec();

    // Daily data is never pruned (kept forever)
    qDebug() << "[DatabaseManager] Pruned old bandwidth samples";
}

void DatabaseManager::aggregateSamples()
{
    // Roll up bw_samples → bw_minutes (1-minute buckets)
    // We use the floor(ts / 60) * 60 trick to bucket by minute.
    // We only aggregate rows older than 2 minutes (allow for late arrivals).
    qint64 minuteCutoff = QDateTime::currentSecsSinceEpoch() - 120;

    QSqlQuery q(m_db);
    // Insert aggregated minute rows that don't already exist
    q.exec(QString(
        "INSERT OR IGNORE INTO bw_minutes (mac, ts, rx_bytes, tx_bytes, rx_rate, tx_rate) "
        "SELECT mac, (ts/60)*60 AS bucket, MAX(rx_bytes), MAX(tx_bytes), "
        "       AVG(rx_rate), AVG(tx_rate) "
        "FROM bw_samples WHERE ts < %1 "
        "GROUP BY mac, bucket"
    ).arg(minuteCutoff));

    // Roll up bw_minutes → bw_hours (1-hour buckets)
    qint64 hourCutoff = QDateTime::currentSecsSinceEpoch() - 3600;
    q.exec(QString(
        "INSERT OR IGNORE INTO bw_hours (mac, ts, rx_bytes, tx_bytes, rx_rate, tx_rate) "
        "SELECT mac, (ts/3600)*3600 AS bucket, MAX(rx_bytes), MAX(tx_bytes), "
        "       AVG(rx_rate), AVG(tx_rate) "
        "FROM bw_minutes WHERE ts < %1 "
        "GROUP BY mac, bucket"
    ).arg(hourCutoff));

    // Roll up bw_hours → bw_days (1-day buckets)
    qint64 dayCutoff = QDateTime::currentSecsSinceEpoch() - 86400;
    q.exec(QString(
        "INSERT OR IGNORE INTO bw_days (mac, ts, rx_bytes, tx_bytes, rx_rate, tx_rate) "
        "SELECT mac, (ts/86400)*86400 AS bucket, MAX(rx_bytes), MAX(tx_bytes), "
        "       AVG(rx_rate), AVG(tx_rate) "
        "FROM bw_hours WHERE ts < %1 "
        "GROUP BY mac, bucket"
    ).arg(dayCutoff));
}

// ── IP history ────────────────────────────────────────────────────────────────
void DatabaseManager::recordIpChange(const QString &mac, const QString &ip)
{
    if (mac.isEmpty() || ip.isEmpty()) return;
    qint64 now = QDateTime::currentSecsSinceEpoch();
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO ip_history (mac, ip, first_seen, last_seen) "
              "VALUES (:mac, :ip, :now, :now) "
              "ON CONFLICT(mac, ip) DO UPDATE SET last_seen = :now2");
    q.bindValue(":mac",  mac);
    q.bindValue(":ip",   ip);
    q.bindValue(":now",  now);
    q.bindValue(":now2", now);
    if (!q.exec())
        qDebug() << "DatabaseManager: recordIpChange error:" << q.lastError().text();
}

QList<IpHistoryEntry> DatabaseManager::getIpHistory(const QString &mac)
{
    QList<IpHistoryEntry> list;
    QSqlQuery q(m_db);
    q.prepare("SELECT mac, ip, first_seen, last_seen FROM ip_history "
              "WHERE mac = :mac ORDER BY last_seen DESC");
    q.bindValue(":mac", mac);
    if (q.exec()) {
        while (q.next()) {
            IpHistoryEntry e;
            e.mac       = q.value(0).toString();
            e.ip        = q.value(1).toString();
            e.firstSeen = QDateTime::fromSecsSinceEpoch(q.value(2).toLongLong());
            e.lastSeen  = QDateTime::fromSecsSinceEpoch(q.value(3).toLongLong());
            list.append(e);
        }
    }
    return list;
}

} // namespace core
