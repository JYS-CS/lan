#pragma once

#include <QObject>
#include <QList>
#include <QString>
#include <QThread>
#include <QMutex>
#include <QHash>
#include <QSet>
#include <QHostAddress>
#include <QDateTime>
#include <QThreadPool>
#include <QTimer>
#include <atomic>

#include "Device.h"
#include "TrafficMonitor.h"
#include "PacketCapture.h"
#include "FirewallManager.h"
#include "DHCPManager.h"
#include "CaptivePortalManager.h"
#include "Types.h"

// Forward-declare pcap types to avoid pulling pcap.h into every TU
struct pcap;
typedef struct pcap pcap_t;
struct pcap_pkthdr;

namespace core {

class PassiveSniffer : public QObject {
    Q_OBJECT
public:
    explicit PassiveSniffer(const QString &iface, QObject *parent = nullptr);
    ~PassiveSniffer() override;

public slots:
    void start();
    void stop();

signals:
    void deviceSeen(const QHostAddress &ip, const QString &mac);
    void snifferError(const QString &msg);

private:
    static void pcapCallback(unsigned char *user, const struct pcap_pkthdr *h, const unsigned char *pkt);
    void processPacket(const unsigned char *pkt, int len);

    QString m_iface;
    pcap_t *m_handle = nullptr;
    std::atomic<bool> m_running{false};
};

class NetworkManager : public QObject {
    Q_OBJECT

public:
    explicit NetworkManager(QObject *parent = nullptr);
    virtual ~NetworkManager();

    // Discovery & Monitoring
    void startScanning(const QString &interfaceName);
    QList<Device> getDevices() const;

public slots:
    // Management actions
    void activate();  // Called once after mode selection to start sniffer, timers, and firewall
    void addStaticLease(const QString &mac, const QString &ip, const QString &host);
    void startDHCPServer(const core::DHCPServerConfig &config);
    void stopDHCPServer();
    void toggleDHCP(bool enable);
    void updateDeviceAlias(const QString &mac, const QString &alias);
    void addWhitelistedMAC(const QString &mac);
    void removeWhitelistedMAC(const QString &mac);

public:
    // Helpers
    static const QHash<QString, QString>& ouiTable();
    static QString getMacVendor(const QString &mac);
    bool isDeviceBlocked(const QString &mac) const;
    DHCPManager* getDHCPManager() const { return m_dhcpManager; }
    void addDiscoveredDevice(const core::Device &dev, bool fromDhcp = false);
    void startCapture(const QString &iface);
    QString getActiveInterface();
    QHostAddress getInterfaceAddress(const QString &iface);
    QHostAddress getInterfaceNetmask(const QString &iface);

signals:
    void devicesUpdated(const QList<core::Device> &devices);
    void scanError(const QString &msg);
    void scanProgress(int percent);
    void deviceSeen(const QHostAddress &ip, const QString &mac);
    void statusMessage(const QString &msg);
    void globalTrafficStatus(const QString &msg);
    void eventLogged(const core::NetworkEvent &event);
    
    // DHCP signals
    void dhcpStatusUpdate(bool running);
    void dhcpOperationSuccess(const QString &msg);
    void dhcpOperationError(const QString &msg);

    // Traffic signals
    void trafficUpdated(const QMap<QString, core::TrafficStats> &stats);
    void globalTrafficStatsUpdated(int packetCount, double pps, quint64 totalIn, quint64 totalOut);

public slots:
    void onRefreshRequested();

private slots:
    void onTrafficUpdated(const QMap<QString, core::TrafficStats> &stats);
    void onGlobalStats(int packetCount, double pps, quint64 totalIn, quint64 totalOut);
    void onDeviceSeen(const QHostAddress &ip, const QString &mac);
    void cleanUpStaleDevices();
    void runScan();

private:
    void step1_readArpCache();
    int  openArpSocket(const QString &iface);
    bool sendRawArpRequest(int sock, const QString &iface, const QString &myMac, const QString &targetIp, const QString &myIp);
    void step2_arpSweep(const QString &iface, quint32 networkAddr, quint32 broadcastAddr, quint32 myIp, const QString &myMac);
    void step3_probeUnconfirmed(const QString &iface, quint32 networkAddr, quint32 broadcastAddr, quint32 myIp);
    void step4_fingerprint(const QString &iface);

    QString getMyMac(const QString &iface);
    QString getGatewayIP();
    QMap<QString, QString> readDHCPLeases();

    void mergeArpEntry(const QString &ip, const QString &mac, const QString &hostname = "");
    void logEvent(core::NetworkEvent::Type type, const QString &message, const QString &ip = "");

public:
    void logNetworkEvent(core::NetworkEvent::Type type, const QString &message, const QString &ip = "") {
        logEvent(type, message, ip);
    }

private:
    QThread *m_networkThread = nullptr;
    QMutex m_resultsMutex;
    QMap<QString, Device> m_allDevices;
    
    // Sub-components
    PacketCapture *m_packetCapturer = nullptr;
    TrafficMonitor *m_trafficMonitor = nullptr;
    FirewallManager *m_firewallManager = nullptr;
    DHCPManager    *m_dhcpManager     = nullptr;
    CaptivePortalManager *m_captivePortal = nullptr;

    QString m_interfaceName;
    QString m_gatewayIp;
    QString m_gatewayMac;
    bool m_firstScanLogged = false;

    PassiveSniffer *m_sniffer         = nullptr;
    QThread        *m_snifferThread  = nullptr;
    QThread        *m_captureThread  = nullptr;
    QTimer         *m_cleanupTimer    = nullptr;

    QSet<QString>  m_confirmedIps;
    quint32        m_myIpAddr         = 0;
    quint32        m_netmaskAddr      = 0;
    quint32        m_networkAddr      = 0;
    
    quint32        m_prevNetworkAddr  = 0;
    quint32        m_prevNetmaskAddr  = 0;
    
    QString        m_myMac;

    QMap<QString, core::TrafficStats> m_latestStats;
    bool m_strictMode = false;
};

} // namespace core
