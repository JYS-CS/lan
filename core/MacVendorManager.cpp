// MacVendorManager.cpp
// Production-ready MAC vendor auto-discovery.
//
// Lookup priority:
//   1. Static hardcoded table  (~200 common OUIs, zero I/O)
//   2. Local IEEE OUI CSV      (~30 k entries, loaded at startup, updated weekly)
//   3. api.macvendors.com      (async, 1 req/s, results cached forever in SQLite)
//
// Thread-safety:
//   - lookupVendor() is safe to call from any thread (QReadWriteLock + atomic queue).
//   - The async API queue runs on the MacVendorManager's own thread (main thread is fine).

#include "MacVendorManager.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QDateTime>
#include <QThread>
#include <QReadLocker>
#include <QWriteLocker>
#include <QUrl>
#include <atomic>

namespace core {

// ============================================================
// Constants
// ============================================================
static constexpr int    kApiIntervalMs     = 1100;  // >1 s between requests (rate limit = 1/s)
static constexpr int    kUpdateIntervalDays = 7;
static constexpr int    kMaxQueueSize       = 500;   // safety cap
static const QString    kApiBaseUrl         = QStringLiteral("https://api.macvendors.com/");
static const QString    kIeeeOuiUrl         = QStringLiteral("https://standards-oui.ieee.org/oui/oui.csv");
static const QString    kCacheDbName        = QStringLiteral("oui_cache");

// ============================================================
// Vendor name normalization (file-scope, used by Tier 2 + Tier 3)
// IEEE CSV / API returns full legal names like
// "TP-LINK TECHNOLOGIES CO.,LTD." — we strip the boilerplate.
// ============================================================
static QString normalizeVendorName(const QString &raw) {
    QString name = raw.trimmed();

    // Strip any stray leading/trailing quotes left by CSV parsers or APIs
    if (name.startsWith('"')) name = name.mid(1);
    if (name.endsWith('"'))   name.chop(1);
    name = name.trimmed();
    if (name.isEmpty()) return {};

    // Strip common corporate suffixes (longest-first for greedy match)
    static const QStringList kSuffixes = {
        " TECHNOLOGIES CO.,LTD.", " TECHNOLOGIES CO., LTD.",
        " TECHNOLOGIES CO.,LTD",  " TECHNOLOGIES CO., LTD",
        " TECHNOLOGIES CO.LTD",
        " CO.,LTD.", " CO., LTD.", " CO.,LTD", " CO., LTD",
        " CO.,LTD.(", " CO. LTD.",
        " INC.", " INC", " LLC", " LLC.",
        " LTD.", " LTD",
        " CORP.", " CORP",
        " GMBH", " AG", " S.A.", " S.A", " S.L.",
        " ELECTRONICS", " ELECTRIC", " SEMICONDUCTOR",
        " TECHNOLOGY", " TECHNOLOGIES",
        " INTERNATIONAL", " SYSTEMS", " NETWORKS",
        " NETWORK", " HOLDING", " HOLDINGS",
    };
    for (const QString &suf : kSuffixes) {
        if (name.endsWith(suf, Qt::CaseInsensitive)) {
            name.chop(suf.length());
            name = name.trimmed();
            break;
        }
    }

    // Title-case only when the whole string is ALL CAPS (raw IEEE style)
    QStringList words = name.split(' ', Qt::SkipEmptyParts);
    bool allCaps = std::all_of(name.begin(), name.end(),
                               [](QChar c){ return !c.isLetter() || c.isUpper(); });
    if (allCaps && words.size() > 1) {
        for (QString &w : words) {
            if (w.length() > 1)
                w = w.at(0).toUpper() + w.mid(1).toLower();
        }
        name = words.join(' ');
    }

    // Hard cap at 40 chars for UI sanity
    if (name.length() > 40)
        name = name.left(38) + "…";

    return name;
}

// ============================================================
// RFC-4180 compliant CSV line splitter
// Returns the individual field values with quotes removed.
// Handles embedded commas and escaped double-quotes ("").
// ============================================================
static QStringList parseCsvFields(const QString &line) {
    QStringList fields;
    QString field;
    bool inQuotes = false;
    for (int i = 0; i < line.size(); ++i) {
        QChar c = line.at(i);
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line.at(i + 1) == '"') {
                    field += '"'; // escaped quote
                    ++i;
                } else {
                    inQuotes = false; // closing quote
                }
            } else {
                field += c;
            }
        } else {
            if (c == '"') {
                inQuotes = true;
            } else if (c == ',') {
                fields.append(field.trimmed());
                field.clear();
            } else {
                field += c;
            }
        }
    }
    fields.append(field.trimmed()); // last field
    return fields;
}

// ============================================================
// Singleton
// ============================================================
MacVendorManager &MacVendorManager::instance() {
    static MacVendorManager inst;
    return inst;
}

// ============================================================
// Constructor
// ============================================================
MacVendorManager::MacVendorManager(QObject *parent) : QObject(parent) {

    // Determine storage path  (~/.local/share/LANMonitor/)
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dataDir.isEmpty()) dataDir = QDir::homePath() + "/.local/share/LANMonitor";
    QDir().mkpath(dataDir);
    m_ouiCsvPath = dataDir + "/oui.csv";

    // Open a dedicated SQLite connection for the OUI cache.
    // Using a separate connection avoids locking the main DB from worker threads.
    m_cacheDb = QSqlDatabase::addDatabase("QSQLITE", kCacheDbName);
    m_cacheDb.setDatabaseName(dataDir + "/oui_cache.db");
    if (m_cacheDb.open()) {
        initSqliteCache();
        m_cacheDbReady = true;
    } else {
        qWarning() << "[MacVendorManager] Failed to open OUI cache DB:"
                   << m_cacheDb.lastError().text();
    }

    // Tier 2 — load local OUI CSV (non-blocking; runs in a worker thread)
    QThread *loaderThread = QThread::create([this] { loadLocalDatabase(); });
    connect(loaderThread, &QThread::finished, loaderThread, &QObject::deleteLater);
    loaderThread->start();

    // Tier 3 — network manager + rate-limited timer
    m_nam      = new QNetworkAccessManager(this);
    m_apiTimer = new QTimer(this);
    m_apiTimer->setInterval(kApiIntervalMs);
    connect(m_apiTimer, &QTimer::timeout, this, &MacVendorManager::processApiQueue);
    m_apiTimer->start();

    // Weekly auto-update check (runs 10 s after startup to avoid blocking launch)
    m_updateCheckTimer = new QTimer(this);
    m_updateCheckTimer->setSingleShot(true);
    connect(m_updateCheckTimer, &QTimer::timeout, this, &MacVendorManager::scheduleWeeklyUpdate);
    m_updateCheckTimer->start(10'000);
}

// ============================================================
// Tier 1 — Static hardcoded table (fast-path, zero I/O)
// ============================================================
const QHash<QString, QString> &MacVendorManager::staticTable() {
    // Keys are 6 uppercase hex chars (no separators) = first 3 MAC bytes
    static const QHash<QString, QString> t = {
        // Apple
        {"A4D18C","Apple"}, {"A8BE27","Apple"}, {"3C22FB","Apple"},
        {"F01898","Apple"}, {"DCA904","Apple"}, {"B8098A","Apple"},
        {"0017F2","Apple"}, {"001F5B","Apple"}, {"98D19C","Apple"},
        {"F0DBFE","Apple"}, {"00CD1B","Apple"}, {"6083E7","Apple"},
        // Samsung
        {"B4F1DA","Samsung"}, {"8C7712","Samsung"}, {"94D469","Samsung"},
        {"E8508B","Samsung"}, {"2C4D54","Samsung"}, {"CC07AB","Samsung"},
        {"001247","Samsung"}, {"F4428F","Samsung"}, {"784F43","Samsung"},
        // Xiaomi
        {"64B473","Xiaomi"}, {"F8A45F","Xiaomi"}, {"286C07","Xiaomi"},
        {"50642B","Xiaomi"}, {"ACC1EE","Xiaomi"}, {"34CE00","Xiaomi"},
        {"009EC8","Xiaomi"}, {"D4970B","Xiaomi"}, {"8C97EA","Xiaomi"},
        // Huawei
        {"744AA4","Huawei"}, {"B0E5ED","Huawei"}, {"4846FB","Huawei"},
        {"047970","Huawei"}, {"A4BADB","Huawei"}, {"548998","Huawei"},
        {"F80113","Huawei"}, {"104780","Huawei"}, {"00464B","Huawei"},
        {"CC96A0","Huawei"}, {"30D17E","Huawei"}, {"6C4B90","Huawei"},
        {"ACE215","Huawei"}, {"283152","Huawei"}, {"70723C","Huawei"},
        // TP-Link
        {"503EAA","TP-Link"}, {"C04A00","TP-Link"}, {"54AF97","TP-Link"},
        {"98DAC4","TP-Link"}, {"14CF92","TP-Link"}, {"30FC68","TP-Link"},
        {"EC086B","TP-Link"}, {"A0F3C1","TP-Link"}, {"B0958E","TP-Link"},
        {"7844FD","TP-Link"}, {"403F8C","TP-Link"}, {"B8D50B","TP-Link"},
        // Netgear
        {"A040A0","Netgear"}, {"C40415","Netgear"}, {"204E7F","Netgear"},
        {"00146C","Netgear"}, {"9CD36D","Netgear"}, {"28C68E","Netgear"},
        {"A42B8C","Netgear"}, {"C03F0E","Netgear"},
        // ASUS
        {"10BF48","ASUS"}, {"50465D","ASUS"}, {"04D4C4","ASUS"},
        {"2C56DC","ASUS"}, {"F832E4","ASUS"}, {"BCEE7B","ASUS"},
        {"AC84C6","ASUS"}, {"74D02B","ASUS"}, {"002618","ASUS"},
        // D-Link
        {"1C7EE5","D-Link"}, {"14D64D","D-Link"}, {"00265A","D-Link"},
        {"B8A386","D-Link"}, {"F07D68","D-Link"}, {"C8D3A3","D-Link"},
        // Linksys / Belkin
        {"C8D719","Linksys"}, {"00259C","Linksys"}, {"20AA4B","Linksys"},
        {"0014BF","Linksys"}, {"E89F80","Belkin"}, {"944452","Belkin"},
        // Cisco / Meraki
        {"001AA1","Cisco"}, {"E8BA70","Cisco"}, {"000C29","VMware"},
        {"0017DF","Cisco"}, {"34DBFD","Cisco"}, {"00235E","Cisco"},
        {"E86549","Cisco Meraki"}, {"881544","Cisco Meraki"},
        // Ubiquiti
        {"24A43C","Ubiquiti"}, {"002722","Ubiquiti"}, {"F09FC2","Ubiquiti"},
        {"788A20","Ubiquiti"}, {"E063DA","Ubiquiti"}, {"7483C2","Ubiquiti"},
        {"DC9FDB","Ubiquiti"}, {"687251","Ubiquiti"},
        // MikroTik
        {"4C5E0C","MikroTik"}, {"D4CA6D","MikroTik"}, {"2CC81B","MikroTik"},
        {"B869F4","MikroTik"}, {"18FD74","MikroTik"}, {"085531","MikroTik"},
        // AVM FRITZ!Box
        {"C486E9","AVM"}, {"3CA62F","AVM"}, {"DC396F","AVM"},
        {"9CC7A6","AVM"}, {"E0286D","AVM"},
        // Google / Nest / Android TV
        {"3C5AB4","Google"}, {"F4F5D8","Google"}, {"546009","Google"},
        {"A47733","Google"}, {"F48527","Google"}, {"1C1A0C","Google"},
        // GL.iNet
        {"9483C4","GL.iNet"}, {"E4956E","GL.iNet"},
        // Synology
        {"001132","Synology"}, {"BC2411","Synology"},
        // Raspberry Pi Foundation
        {"B827EB","Raspberry Pi Foundation"},
        {"DCA632","Raspberry Pi Foundation"},
        {"E45F01","Raspberry Pi Foundation"},
        {"2CCF67","Raspberry Pi Foundation"},
        // Intel (Wi-Fi / Ethernet)
        {"8C8D28","Intel"}, {"A4C3F0","Intel"}, {"00216A","Intel"},
        {"28D244","Intel"}, {"AC9E17","Intel"}, {"08D4E0","Intel"},
        // Dell
        {"D4BED9","Dell"}, {"F8DB88","Dell"}, {"18A994","Dell"},
        // Sony
        {"001A80","Sony"}, {"FC0FE6","Sony"}, {"0C9ABF","Sony"},
        // Nintendo
        {"E84ECE","Nintendo"}, {"002459","Nintendo"}, {"7866B9","Nintendo"},
        // Amazon (Echo, Fire TV, Kindle)
        {"FC65DE","Amazon"}, {"A002DC","Amazon"}, {"68D93C","Amazon"},
        {"749D8F","Amazon"}, {"F0272D","Amazon"},
        // Microsoft (Surface, Xbox)
        {"28184D","Microsoft"}, {"60451D","Microsoft"}, {"7C1E52","Microsoft"},
        // VMware / VirtualBox
        {"005056","VMware"}, {"000569","VMware"}, {"080027","VirtualBox"},
        // Docker / KVM / Hyper-V (common virtual MACs)
        {"525400","QEMU/KVM"}, {"0015E9","Dell iDRAC"},
    };
    return t;
}

// ============================================================
// Helper — Normalize MAC to 6 uppercase hex (no separators)
// ============================================================
QString MacVendorManager::normalizeOui(const QString &mac) {
    // Strip all separators and take first 6 chars
    QString stripped = mac;
    stripped.remove(':');
    stripped.remove('-');
    stripped.remove('.');
    return stripped.left(6).toUpper();
}

// ============================================================
// Helper — Locally-administered bit check
// ============================================================
bool MacVendorManager::isLocallyAdministered(const QString &mac) {
    // First byte of MAC: bit 1 (0x02) = locally administered
    QString oui = normalizeOui(mac);
    if (oui.size() < 2) return false;
    bool ok = false;
    int firstByte = oui.left(2).toInt(&ok, 16);
    return ok && (firstByte & 0x02) != 0;
}

// ============================================================
// Primary lookup  (thread-safe)
// ============================================================
QString MacVendorManager::lookupVendor(const QString &mac) {
    if (mac.isEmpty()) return QStringLiteral("Unknown Vendor");

    const QString oui6 = normalizeOui(mac);

    // --- Tier 1: static table ---
    {
        const auto &st = staticTable();
        auto it = st.find(oui6);
        if (it != st.end()) return it.value();
    }

    // --- Tier 2: local IEEE OUI table ---
    {
        QReadLocker lk(&m_tableLock);
        auto it = m_ouiTable.find(oui6);
        if (it != m_ouiTable.end()) return it.value();
    }

    // --- Tier 3 check: SQLite cache (fast synchronous read) ---
    // Re-normalize cached values so old raw API entries (stored before normalization
    // was added) get cleaned up transparently on first hit.
    if (m_cacheDbReady) {
        QString cached = getCachedVendor(oui6);
        if (!cached.isEmpty()) {
            QString clean = normalizeVendorName(cached);
            // If normalization changed the string, update the cache record
            if (clean != cached) cacheVendor(oui6, clean);
            return clean;
        }
    }

    // Queue an async Tier-3 API request (skips randomized/local MACs)
    if (!isLocallyAdministered(mac)) {
        enqueueApiLookup(mac);
    }

    return QStringLiteral("Unknown Vendor");
}

// ============================================================
// Tier 2 — load local OUI CSV from disk
// ============================================================
void MacVendorManager::loadLocalDatabase() {
    QFile f(m_ouiCsvPath);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        qDebug() << "[MacVendorManager] Local OUI CSV not found, will download.";
        // Trigger download from main thread
        QMetaObject::invokeMethod(this, &MacVendorManager::downloadOuiDatabase,
                                  Qt::QueuedConnection);
        return;
    }

    const QByteArray data = f.readAll();
    f.close();
    parseOuiCsv(data);
}

void MacVendorManager::parseOuiCsv(const QByteArray &data) {
    // IEEE CSV format (MA-L):
    //   Registry,Assignment,Organization Name,Organization Address
    //   MA-L,FCFCFE,PRIVATE,
    //   MA-L,000000,"Xerox Corporation","..."
    //
    // Company names often contain commas (e.g. "CO., LTD.") so we use
    // a proper RFC-4180 quoted-field parser instead of naive indexOf(',').

    QHash<QString, QString> table;
    table.reserve(32768);

    QTextStream in(data);
    in.readLine(); // skip header line

    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.isEmpty()) continue;

        const QStringList fields = parseCsvFields(line);
        // fields: [Registry, Assignment, Organization Name, Organization Address]
        if (fields.size() < 3) continue;

        const QString oui  = fields.at(1).trimmed().toUpper(); // 6 hex chars, e.g. "001678"
        const QString name = fields.at(2).trimmed();

        if (oui.size() != 6 || name.isEmpty()
                || name.compare("PRIVATE", Qt::CaseInsensitive) == 0)
            continue;

        const QString normalized = normalizeVendorName(name);
        if (!normalized.isEmpty())
            table.insert(oui, normalized);
    }

    {
        QWriteLocker lk(&m_tableLock);
        m_ouiTable = std::move(table);
    }

    int count = m_ouiTable.size();
    qDebug() << "[MacVendorManager] Loaded" << count << "OUI entries from local CSV.";
    emit databaseUpdated(count);
    emit statusMessage(QString("OUI database loaded: %1 vendor entries").arg(count));
}

int MacVendorManager::localEntryCount() const {
    QReadLocker lk(&m_tableLock);
    return m_ouiTable.size();
}

// ============================================================
// Tier 3 — SQLite OUI cache
// ============================================================
void MacVendorManager::initSqliteCache() {
    QSqlQuery q(m_cacheDb);
    bool ok = q.exec(
        "CREATE TABLE IF NOT EXISTS oui_cache ("
        "  oui     TEXT PRIMARY KEY, "   // 6 uppercase hex chars
        "  vendor  TEXT NOT NULL, "
        "  cached_at TEXT NOT NULL"
        ")"
    );
    if (!ok) {
        qWarning() << "[MacVendorManager] Failed to create oui_cache table:"
                   << q.lastError().text();
    }
}

QString MacVendorManager::getCachedVendor(const QString &oui6) const {
    if (!m_cacheDbReady) return {};
    QSqlQuery q(m_cacheDb);
    q.prepare("SELECT vendor FROM oui_cache WHERE oui = :oui");
    q.bindValue(":oui", oui6);
    if (q.exec() && q.next()) {
        return q.value(0).toString();
    }
    return {};
}

void MacVendorManager::cacheVendor(const QString &oui6, const QString &vendor) {
    if (!m_cacheDbReady) return;
    QSqlQuery q(m_cacheDb);
    q.prepare("INSERT OR REPLACE INTO oui_cache (oui, vendor, cached_at) "
              "VALUES (:oui, :vendor, :at)");
    q.bindValue(":oui",    oui6);
    q.bindValue(":vendor", vendor);
    q.bindValue(":at",     QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!q.exec()) {
        qWarning() << "[MacVendorManager] Cache write error:" << q.lastError().text();
    }
}

// ============================================================
// Tier 3 — Async API queue
// ============================================================
void MacVendorManager::enqueueApiLookup(const QString &mac) {
    const QString oui6 = normalizeOui(mac);

    QMutexLocker lk(&m_queueMutex);

    // Skip if already pending or recently failed
    if (m_apiPending.contains(oui6)) return;
    if (m_negativeCache.contains(oui6)) return;
    if (m_apiQueue.size() >= kMaxQueueSize) return; // cap reached, drop

    m_apiPending.insert(oui6);
    // Store normalized OUI (colon-formatted) for the API URL
    QString apiMac = QString("%1:%2:%3")
                     .arg(oui6.mid(0,2), oui6.mid(2,2), oui6.mid(4,2));
    m_apiQueue.append(apiMac);
}

void MacVendorManager::processApiQueue() {
    if (m_apiRequestInFlight) return;

    QMutexLocker lk(&m_queueMutex);
    if (m_apiQueue.isEmpty()) return;

    QString mac = m_apiQueue.takeFirst();
    lk.unlock();

    const QString oui6 = normalizeOui(mac);

    m_apiRequestInFlight = true;

    QNetworkRequest req(QUrl(kApiBaseUrl + mac));
    req.setHeader(QNetworkRequest::UserAgentHeader, "LANMonitor/1.0 (github.com/lan-monitor)");
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                     QNetworkRequest::AlwaysNetwork);
    // 5-second timeout
    req.setTransferTimeout(5000);

    QNetworkReply *reply = m_nam->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply, oui6, mac]() {
        m_apiRequestInFlight = false;

        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray body = reply->readAll().trimmed();
        reply->deleteLater();

        if (reply->error() == QNetworkReply::NoError && httpStatus == 200 && !body.isEmpty()) {
            QString vendor = normalizeVendorName(QString::fromUtf8(body));

            qDebug() << "[MacVendorManager] API resolved" << mac << "->" << vendor;
            cacheVendor(oui6, vendor);

            // Reconstruct a colon-separated OUI for the signal
            QString canonicalMac = QString("%1:%2:%3:xx:xx:xx")
                                   .arg(oui6.mid(0,2), oui6.mid(2,2), oui6.mid(4,2));
            emit vendorResolved(oui6, vendor);

        } else if (httpStatus == 404) {
            // OUI not registered — cache the negative result so we don't retry
            qDebug() << "[MacVendorManager] API: OUI" << oui6 << "not found (404)";
            cacheVendor(oui6, "Unknown Vendor");
            QMutexLocker lk2(&m_queueMutex);
            m_negativeCache.insert(oui6);

        } else if (httpStatus == 429) {
            // Rate-limited — put it back at the front of the queue
            qDebug() << "[MacVendorManager] API rate-limited, re-queuing" << mac;
            QMutexLocker lk2(&m_queueMutex);
            m_apiQueue.prepend(mac);
            // Back off: stop the timer for 5 s then restart
            m_apiTimer->stop();
            QTimer::singleShot(5000, this, [this]() { m_apiTimer->start(); });

        } else {
            // Network error — re-queue with back-off
            qDebug() << "[MacVendorManager] API error for" << mac
                     << reply->errorString();
            QMutexLocker lk2(&m_queueMutex);
            m_apiPending.remove(oui6);  // allow retry later
        }
    });
}

// ============================================================
// Auto-update — weekly IEEE OUI database refresh
// ============================================================
QString MacVendorManager::lastUpdateTimestamp() const {
    QFileInfo fi(m_ouiCsvPath);
    if (!fi.exists()) return {};
    return fi.lastModified().toString(Qt::ISODate);
}

bool MacVendorManager::isUpdateNeeded() const {
    QFileInfo fi(m_ouiCsvPath);
    if (!fi.exists()) return true;
    return fi.lastModified().daysTo(QDateTime::currentDateTime()) >= kUpdateIntervalDays;
}

void MacVendorManager::scheduleWeeklyUpdate() {
    if (isUpdateNeeded()) {
        qDebug() << "[MacVendorManager] OUI DB outdated, downloading fresh copy…";
        downloadOuiDatabase();
    } else {
        qDebug() << "[MacVendorManager] OUI DB is current, next check in"
                 << kUpdateIntervalDays << "days.";
    }

    // Re-arm: check again in 24 h (actual download only happens when stale)
    m_updateCheckTimer->start(24 * 60 * 60 * 1000);
    m_updateCheckTimer->setSingleShot(false);
    disconnect(m_updateCheckTimer, &QTimer::timeout, this,
               &MacVendorManager::scheduleWeeklyUpdate);
    connect(m_updateCheckTimer, &QTimer::timeout, this, [this]() {
        if (isUpdateNeeded()) downloadOuiDatabase();
    });
}

// ============================================================
// Vendor name normalization was moved above constants.
// ============================================================
void MacVendorManager::downloadOuiDatabase() {
    // Guard: only one download at a time
    if (m_downloadInProgress.exchange(true)) {
        qDebug() << "[MacVendorManager] Download already in progress, skipping duplicate request.";
        return;
    }
    emit statusMessage("Downloading IEEE OUI database…");
    qDebug() << "[MacVendorManager] Fetching" << kIeeeOuiUrl;

    QUrl ieeeUrl{kIeeeOuiUrl};
    QNetworkRequest req{ieeeUrl};
    req.setHeader(QNetworkRequest::UserAgentHeader, "LANMonitor/1.0");
    // Don't transfer-timeout here — file is ~3.5 MB, takes a few seconds
    QNetworkReply *reply = m_nam->get(req);

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 recv, qint64 total) {
                if (total > 0) {
                    int pct = static_cast<int>(recv * 100 / total);
                    emit statusMessage(
                        QString("Downloading OUI database… %1%").arg(pct));
                }
            });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            m_downloadInProgress = false; // release guard on error too
            qWarning() << "[MacVendorManager] OUI download failed:"
                       << reply->errorString();
            emit statusMessage("OUI database update failed: " + reply->errorString());
            reply->deleteLater();
            return;
        }

        const QByteArray data = reply->readAll();
        reply->deleteLater();
        m_downloadInProgress = false; // release guard

        if (data.isEmpty() || data.size() < 10000) {
            qWarning() << "[MacVendorManager] Downloaded OUI file seems truncated ("
                       << data.size() << "bytes), discarding.";
            return;
        }

        // Save to disk
        QFile f(m_ouiCsvPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(data);
            f.close();
            qDebug() << "[MacVendorManager] OUI CSV saved to" << m_ouiCsvPath
                     << "(" << data.size() << "bytes)";
        } else {
            qWarning() << "[MacVendorManager] Failed to save OUI CSV:" << f.errorString();
        }

        // Parse in a worker thread so the download callback returns quickly
        QThread *t = QThread::create([this, data] { parseOuiCsv(data); });
        connect(t, &QThread::finished, t, &QObject::deleteLater);
        t->start();

        emit statusMessage("OUI database updated successfully.");
    });
}

void MacVendorManager::refreshDatabase() {
    downloadOuiDatabase();
}

} // namespace core
