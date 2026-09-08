#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QSet>
#include <QMap>

namespace core {

class FirewallManager : public QObject {
    Q_OBJECT

public:
    explicit FirewallManager(const QString &iface, QObject *parent = nullptr);
    virtual ~FirewallManager();

public slots:
    void setInterface(const QString &iface);
    void start();
    void stop();
    bool blockIP(const QString &ip);
    bool unblockIP(const QString &ip);
    bool blockMAC(const QString &mac);
    bool unblockMAC(const QString &mac);
    bool isMACBlocked(const QString &mac) const;
    bool enableBlockPageForMAC(const QString &mac);
    bool disableBlockPageForMAC(const QString &mac);
    bool unblockAll();
    void initFirewall();
    void setServerIP(const QString &ip);
    void flushArpCache(const QString &ip);
    
    // Source Guard & Whitelist
    bool addAllowedLease(const QString &ip, const QString &mac);
    bool removeAllowedLease(const QString &ip, const QString &mac);
    bool addWhitelistedMAC(const QString &mac);
    bool removeWhitelistedMAC(const QString &mac);
    bool isWhitelisted(const QString &mac) const;
    bool setStrictMode(bool enable);

    // Threat intelligence: block outbound connections to known-malicious
    // destination IPs (see ThreatIntelManager, which fetches the actual
    // list). entries can be plain IPv4 addresses or CIDR ranges.
    void setThreatBlocklistEnabled(bool enabled);
    bool isThreatBlocklistEnabled() const { return m_threatBlocklistEnabled; }
    void updateThreatBlocklist(const QStringList &entries);
    int threatBlocklistSize() const { return m_threatBlocklistSize; }

signals:
    void firewallError(const QString &message);
    void actionSuccess(const QString &message);

private:
    bool runNft(const QStringList &args);
    bool runNftScript(const QString &script, int timeoutMs = 30000);
    bool runCommand(const QString &cmd);
    
    // Atomic synchronization helpers
    void syncBlockedMACs();
    void syncWhitelistedMACs();
    void syncAllowedLeases();
    void syncBlockPageMACs();

    // filter_forward gets flushed and rebuilt in a few places (initFirewall,
    // setStrictMode); this re-adds the malicious-IP drop rule wherever that
    // happens so it's never silently lost.
    void addThreatBlocklistRule();

    QString m_tableName = "lan_monitor";
    QString m_interface;
    QString m_serverIP;
    QSet<QString> m_blockedMACs;
    QSet<QString> m_whitelistedMACs;
    bool m_threatBlocklistEnabled = false;
    int  m_threatBlocklistSize = 0;
    QSet<QString> m_blockPageMACs;
    QMap<QString, QString> m_allowedLeases; // IP -> MAC
    bool m_available = false;
    bool m_active = false;
};

} // namespace core
