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

signals:
    void firewallError(const QString &message);
    void actionSuccess(const QString &message);

private:
    bool runNft(const QStringList &args);
    bool runCommand(const QString &cmd);
    
    // Atomic synchronization helpers
    void syncBlockedMACs();
    void syncWhitelistedMACs();
    void syncAllowedLeases();
    void syncBlockPageMACs();

    QString m_tableName = "lan_monitor";
    QString m_interface;
    QString m_serverIP;
    QSet<QString> m_blockedMACs;
    QSet<QString> m_whitelistedMACs;
    QSet<QString> m_blockPageMACs;
    QMap<QString, QString> m_allowedLeases; // IP -> MAC
    bool m_available = false;
};

} // namespace core
