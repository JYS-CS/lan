#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QList>
#include "Device.h"
#include "Types.h"

namespace core {

// A single blocked-device record, as stored in the blacklist table.
struct BlacklistEntry {
    QString mac;
    QString reason;
    QString blockedAt; // ISO 8601
};

// A single DNS query, as recorded by DnsProxyServer for browsing-activity
// visibility.
struct DnsLogEntry {
    QString clientIp;
    QString clientMac;
    QString domain;
    QString qtype;
    bool blocked = false;
    bool cached  = false;
    QString timestamp; // ISO 8601
};

class DatabaseManager : public QObject {
    Q_OBJECT

public:
    static DatabaseManager& instance() {
        static DatabaseManager inst;
        return inst;
    }

    bool init(const QString &dbPath = "lan_monitor.db");

    // ── Device persistence ────────────────────────────────────────────────
    void saveDevice(const Device &d);
    void removeDevice(const QString &networkId, const QString &mac);
    QList<Device> getAllDevices();
    void updateAlias(const QString &networkId, const QString &mac, const QString &alias);

    // ── Event persistence ─────────────────────────────────────────────────
    void saveEvent(const core::NetworkEvent &e);
    QList<core::NetworkEvent> getAllEvents(int limit = 500);

    // ── Blacklist / Whitelist ─────────────────────────────────────────────
    void addToBlacklist(const QString &networkId, const QString &mac, const QString &reason);
    void removeFromBlacklist(const QString &networkId, const QString &mac);
    bool isBlacklisted(const QString &networkId, const QString &mac);
    void updateBlacklistReason(const QString &networkId, const QString &mac, const QString &newReason);
    QList<BlacklistEntry> getBlacklist(const QString &networkId);
    void clearHistoricalDevices(const QString &currentNetworkId);
    
    void addToWhitelist(const QString &networkId, const QString &mac);
    void removeFromWhitelist(const QString &networkId, const QString &mac);
    bool isWhitelisted(const QString &networkId, const QString &mac);

    // Device identity correlation (see DeviceIdentityEngine)
    QString findIdentityByFingerprint(const QString &networkId, const QString &fingerprint, const QString &hostname);
    QString findIdentityByClientId(const QString &networkId, const QString &clientId);
    QString mostRecentMacForIdentity(const QString &networkId, const QString &identityId);
    void createIdentity(const QString &networkId, const QString &identityId, const QString &fingerprint,
                         const QString &hostname, const QString &clientId);
    void linkMacToIdentity(const QString &networkId, const QString &identityId, const QString &mac);

    // DNS query log (see DnsProxyServer)
    void logDnsQuery(const QString &networkId, const DnsLogEntry &entry);
    QList<DnsLogEntry> getRecentDnsQueries(const QString &networkId, int limit = 500, const QString &clientIpFilter = QString());
    int countDnsQueries(const QString &networkId, bool blockedOnly = false);
    // Keeps the table from growing unbounded — call periodically.
    void pruneDnsLog(const QString &networkId, int maxRows = 20000);

    // ── Bandwidth history ─────────────────────────────────────────────────
    // Insert one raw 5-second sample.
    void insertBwSample(const BwSample &sample);

    // Query samples for a MAC in a time range (Unix epoch seconds).
    QList<BwSample> getBwSamples(const QString &mac, qint64 fromTs, qint64 toTs,
                                  const QString &table = "bw_samples");

    // Top N devices by total bytes (rx+tx) over the last `periodSeconds`.
    // If periodSeconds == 0, returns all-time totals.
    struct TopTalkerEntry {
        QString mac;
        quint64 rxBytes = 0;
        quint64 txBytes = 0;
        quint64 total   = 0;
    };
    QList<TopTalkerEntry> getTopTalkers(int limit, qint64 periodSeconds = 0,
                                         const QString &table = "bw_days");

    // Retention: prune raw samples older than retention periods.
    // Called by BandwidthEngine every hour.
    void pruneOldSamples();

    // ── IP history ────────────────────────────────────────────────────────
    void recordIpChange(const QString &mac, const QString &ip);
    QList<IpHistoryEntry> getIpHistory(const QString &mac);

private:
    explicit DatabaseManager(QObject *parent = nullptr);
    ~DatabaseManager();
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    bool setupSchema();
    void aggregateSamples();  // rolls up bw_samples → bw_minutes → bw_hours → bw_days

    QSqlDatabase m_db;
};

} // namespace core
