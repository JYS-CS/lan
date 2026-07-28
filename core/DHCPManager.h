#ifndef DHCPMANAGER_H
#define DHCPMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QTimer>
#include <QSet>

namespace core {

class NetworkManager;
class DhcpServer;

struct DHCPLease {
    QString ip;
    QString mac;
    QString hostname;
    QDateTime expiry;
};

struct DHCPServerConfig {
    QString interface;
    QString subnetMask;
    QString rangeStart;
    QString rangeEnd;
    QString routerIp;
    QString dns1;
    QString dns2;
    int leaseTimeSeconds;
    bool enabled;
    bool authoritative;
    // Host laptop's own identity — filled in by NetworkManager before starting the server.
    // The DHCP server will never respond to packets from this MAC/IP.
    QString hostMac;  // e.g. "d8:12:65:37:fe:2b"
    QString hostIp;   // e.g. "192.168.8.101"

    DHCPServerConfig() : leaseTimeSeconds(60), enabled(false), authoritative(true) {}
};

class DHCPManager : public QObject {
    Q_OBJECT
public:
    explicit DHCPManager(NetworkManager *networkManager, QObject *parent = nullptr);
    virtual ~DHCPManager();

    bool configureDHCPServer(const DHCPServerConfig &config);
    bool startServer();
    bool stopServer();
    bool isServerRunning() const;
    
    QList<DHCPLease> readActiveLeases();
    bool addStaticLease(const QString &mac, const QString &ip, const QString &hostname);
    bool removeLease(const QString &mac, const QString &ip);   // Expire a client's lease immediately (Kick)
    bool expireLease(const QString &mac);                     // Silent lease expiry for Poison Pill logic
    bool blockMAC(const QString &mac);      // Permanently block a MAC (Ban)
    bool unblockMAC(const QString &mac);    // Remove a block
    bool isMACBlocked(const QString &mac) const;
    
    // Whitelist
    bool addWhitelistedMAC(const QString &mac);
    bool removeWhitelistedMAC(const QString &mac);
    bool isMACWhitelisted(const QString &mac) const;
    
    QString checkConflicts();

signals:
    void dhcpStatusChanged(bool running);
    void dhcpError(const QString &message);
    void operationSuccess(const QString &message);
    void logEvent(const QString &message);
    void leaseDiscovered(const core::DHCPLease &lease);
    void leaseExpired(const QString &ip, const QString &mac);

private:
    NetworkManager *m_networkManager;
    DHCPServerConfig m_config;
    bool m_isRunning;
    DhcpServer *m_dhcpThread;
    QSet<QString> m_blockedMACs;  // MACs that are permanently denied a lease
    QSet<QString> m_whitelistedMACs;
};

} // namespace core

Q_DECLARE_METATYPE(core::DHCPLease)

#endif // DHCPMANAGER_H
