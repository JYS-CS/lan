#pragma once

// MacVendorManager.h
// Production-ready, 3-tier MAC vendor auto-discovery:
//   Tier 1 — hardcoded static table   (instant, ~200 common OUIs)
//   Tier 2 — local IEEE OUI CSV       (fast, ~30k entries, auto-updated weekly)
//   Tier 3 — online API fallback      (async, rate-limited, results cached in SQLite)

#include <QObject>
#include <QString>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QMutex>
#include <QReadWriteLock>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSqlDatabase>

namespace core {

class MacVendorManager : public QObject {
    Q_OBJECT

public:
    // Singleton accessor — always use this.
    static MacVendorManager &instance();

    // ---------------------------------------------------------------
    // Primary lookup API
    // ---------------------------------------------------------------
    // Synchronous Tier 1+2 lookup. Returns vendor name immediately.
    // If the result is "Unknown Vendor" AND the MAC is globally unique,
    // an async Tier-3 (online API) lookup is queued automatically.
    // When the async result arrives, vendorResolved() is emitted.
    QString lookupVendor(const QString &mac);

    // Force a refresh of the local IEEE OUI database from the network.
    void refreshDatabase();

    // How many entries are loaded in the local OUI table.
    int localEntryCount() const;

    // Returns the timestamp (ISO-8601) of the last successful OUI DB update.
    QString lastUpdateTimestamp() const;

signals:
    // Emitted when an async Tier-3 lookup resolves.
    // Connect this in NetworkManager to update the live device vendor field.
    void vendorResolved(const QString &mac, const QString &vendor);

    // Emitted after the local OUI database is (re-)loaded.
    void databaseUpdated(int entryCount);

    // Status messages for the UI / log.
    void statusMessage(const QString &msg);

private:
    explicit MacVendorManager(QObject *parent = nullptr);

    // ---- Tier 1 --------------------------------------------------------
    // Hardcoded fast-path table. Checked before anything else.
    static const QHash<QString, QString> &staticTable();

    // ---- Tier 2 --------------------------------------------------------
    void     loadLocalDatabase();           // Parse cached oui.csv from disk
    void     parseOuiCsv(const QByteArray &data); // In-place CSV parser
    QString  m_ouiCsvPath;                  // ~/.local/share/LANMonitor/oui.csv
    QHash<QString, QString> m_ouiTable;     // OUI (6 uppercase hex) -> vendor
    mutable QReadWriteLock m_tableLock;

    // ---- Tier 3 --------------------------------------------------------
    void initSqliteCache();
    QString getCachedVendor(const QString &oui6) const; // returns "" if not cached
    void    cacheVendor(const QString &oui6, const QString &vendor);
    QSet<QString> m_negativeCache;          // MACs that returned 404 (don't re-query)

    void     enqueueApiLookup(const QString &mac);
    void     processApiQueue();             // Called by m_apiTimer every 1100ms
    QStringList       m_apiQueue;           // Pending MACs (de-duplicated)
    QSet<QString>     m_apiPending;         // MACs currently in-flight or queued
    QMutex            m_queueMutex;
    bool              m_apiRequestInFlight = false;
    QNetworkAccessManager *m_nam = nullptr;
    QTimer            *m_apiTimer = nullptr;

    // ---- Auto-update ---------------------------------------------------
    void     scheduleWeeklyUpdate();
    bool     isUpdateNeeded() const;        // True if oui.csv is >7 days old
    void     downloadOuiDatabase();         // Fetches from IEEE
    QTimer   *m_updateCheckTimer = nullptr;
    std::atomic<bool> m_downloadInProgress{false}; // Guards against concurrent downloads

    // ---- Helpers -------------------------------------------------------
    // Returns true when bit 1 of the first MAC octet is set → locally-administered
    // (randomized privacy MACs). These are never sent to the API.
    static bool isLocallyAdministered(const QString &mac);
    // Normalize MAC to 6 uppercase hex chars (no separators).
    static QString normalizeOui(const QString &mac);

    // SQLite connection (separate from main DB to allow multi-thread access)
    mutable QSqlDatabase m_cacheDb;
    bool m_cacheDbReady = false;

    // Prevent copy
    MacVendorManager(const MacVendorManager &) = delete;
    MacVendorManager &operator=(const MacVendorManager &) = delete;
};

} // namespace core
