// NetworkManager.cpp
// Full multi-layer network discovery: ARP cache -> ARP sweep -> ICMP/SYN -> fingerprint -> passive sniffer

#include "NetworkManager.h"
#include "DatabaseManager.h"
#include "RouterDetector.h"

// Qt
#include <QNetworkInterface>
#include <QThread>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QRunnable>
#include <QSet>
#include <QRegularExpression>
#include <QProcess>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QHostInfo>

// C++ std
#include <fstream>
#include <sstream>

// Linux networking
// NOTE: <netpacket/packet.h> is intentionally excluded — libcrafter already
// pulls in <linux/if_packet.h> which defines sockaddr_ll. Including both
// causes "redefinition of struct sockaddr_ll" errors.
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/ip_icmp.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <net/if_arp.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h> // Explicitly include for sockaddr_ll
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <cstdio>

// pcap
#include <pcap/pcap.h>

namespace core {

// File-scope helper — used by both PassiveSniffer and NetworkManager
static QString macBytesToString(const unsigned char *b) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
             b[0], b[1], b[2], b[3], b[4], b[5]);
    return QString::fromLatin1(buf);
}

// ============================================================
// OUI Vendor Lookup (static, top 30 prefixes)
// ============================================================
const QHash<QString, QString> &NetworkManager::ouiTable() {
    static QHash<QString, QString> t = {
        // Apple
        {"A4:D1:8C", "Apple"},   {"A8:BE:27", "Apple"},   {"3C:22:FB", "Apple"},
        {"F0:18:98", "Apple"},   {"DC:A9:04", "Apple"},   {"B8:09:8A", "Apple"},
        {"00:17:F2", "Apple"},   {"00:1F:5B", "Apple"},
        // Samsung
        {"B4:F1:DA", "Samsung"}, {"8C:77:12", "Samsung"}, {"94:D4:69", "Samsung"},
        {"E8:50:8B", "Samsung"}, {"2C:4D:54", "Samsung"}, {"CC:07:AB", "Samsung"},
        {"00:12:47", "Samsung"}, {"F4:42:8F", "Samsung"},
        // Xiaomi
        {"64:B4:73", "Xiaomi"},  {"F8:A4:5F", "Xiaomi"},  {"28:6C:07", "Xiaomi"},
        {"50:64:2B", "Xiaomi"},  {"AC:C1:EE", "Xiaomi"},  {"34:CE:00", "Xiaomi"},
        {"00:9E:C8", "Xiaomi"},  {"D4:97:0B", "Xiaomi"},
        // Huawei (routers, modems, WiFi AX series)
        {"74:4A:A4", "Huawei"},  {"B0:E5:ED", "Huawei"},  {"48:46:FB", "Huawei"},
        {"04:79:70", "Huawei"},  {"A4:BA:DB", "Huawei"},  {"54:89:98", "Huawei"},
        {"F8:01:13", "Huawei"},  {"10:47:80", "Huawei"},  {"00:46:4B", "Huawei"},
        {"CC:96:A0", "Huawei"},  {"30:D1:7E", "Huawei"},  {"6C:4B:90", "Huawei"},
        {"AC:E2:15", "Huawei"},  {"28:31:52", "Huawei"},  {"70:72:3C", "Huawei"},
        // TP-Link
        {"50:3E:AA", "TP-Link"}, {"C0:4A:00", "TP-Link"}, {"54:AF:97", "TP-Link"},
        {"98:DA:C4", "TP-Link"}, {"14:CF:92", "TP-Link"}, {"30:FC:68", "TP-Link"},
        {"EC:08:6B", "TP-Link"}, {"A0:F3:C1", "TP-Link"}, {"B0:95:8E", "TP-Link"},
        {"78:44:FD", "TP-Link"}, {"40:3F:8C", "TP-Link"}, {"B8:D5:0B", "TP-Link"},
        // Netgear
        {"A0:40:A0", "Netgear"}, {"C4:04:15", "Netgear"}, {"20:4E:7F", "Netgear"},
        {"00:14:6C", "Netgear"}, {"9C:D3:6D", "Netgear"}, {"28:C6:8E", "Netgear"},
        {"A4:2B:8C", "Netgear"}, {"C0:3F:0E", "Netgear"},
        // ASUS
        {"10:BF:48", "ASUS"},    {"50:46:5D", "ASUS"},    {"04:D4:C4", "ASUS"},
        {"2C:56:DC", "ASUS"},    {"F8:32:E4", "ASUS"},    {"BC:EE:7B", "ASUS"},
        {"AC:84:C6", "ASUS"},    {"74:D0:2B", "ASUS"},    {"00:26:18", "ASUS"},
        // D-Link
        {"1C:7E:E5", "D-Link"},  {"14:D6:4D", "D-Link"},  {"00:26:5A", "D-Link"},
        {"B8:A3:86", "D-Link"},  {"F0:7D:68", "D-Link"},  {"C8:D3:A3", "D-Link"},
        // Linksys / Belkin
        {"C8:D7:19", "Linksys"}, {"00:25:9C", "Linksys"}, {"20:AA:4B", "Linksys"},
        {"00:14:BF", "Linksys"}, {"E8:9F:80", "Belkin"},  {"94:44:52", "Belkin"},
        // Cisco / Meraki
        {"00:1A:A1", "Cisco"},   {"E8:BA:70", "Cisco"},   {"00:0C:29", "Cisco"},
        {"00:17:DF", "Cisco"},   {"34:DB:FD", "Cisco"},   {"00:23:5E", "Cisco"},
        {"E8:65:49", "Cisco Meraki"}, {"88:15:44", "Cisco Meraki"},
        // Ubiquiti
        {"24:A4:3C", "Ubiquiti"},{"00:27:22", "Ubiquiti"},{"F0:9F:C2", "Ubiquiti"},
        {"78:8A:20", "Ubiquiti"},{"E0:63:DA", "Ubiquiti"},{"74:83:C2", "Ubiquiti"},
        {"DC:9F:DB", "Ubiquiti"},{"68:72:51", "Ubiquiti"},
        // MikroTik
        {"4C:5E:0C", "MikroTik"},{"D4:CA:6D", "MikroTik"},{"2C:C8:1B", "MikroTik"},
        {"B8:69:F4", "MikroTik"},{"18:FD:74", "MikroTik"},{"08:55:31", "MikroTik"},
        // AVM FRITZ!Box
        {"C4:86:E9", "AVM"},     {"3C:A6:2F", "AVM"},     {"DC:39:6F", "AVM"},
        {"9C:C7:A6", "AVM"},     {"E0:28:6D", "AVM"},
        // Google / Nest
        {"3C:5A:B4", "Google"},  {"F4:F5:D8", "Google"},  {"54:60:09", "Google"},
        {"A4:77:33", "Google"},  {"F4:85:27", "Google"},
        // GL.iNet
        {"94:83:C4", "GL.iNet"}, {"E4:95:6E", "GL.iNet"},
        // Synology
        {"00:11:32", "Synology"},{"BC:24:11", "Synology"},
        // Raspberry Pi
        {"B8:27:EB", "Raspberry Pi"}, {"DC:A6:32", "Raspberry Pi"}, {"E4:5F:01", "Raspberry Pi"},
        // Intel (Wi-Fi cards)
        {"8C:8D:28", "Intel"},   {"A4:C3:F0", "Intel"},   {"00:21:6A", "Intel"},
        // Dell
        {"D4:BE:D9", "Dell"},    {"F8:DB:88", "Dell"},
        // Sony
        {"00:1A:80", "Sony"},    {"FC:0F:E6", "Sony"},
    };
    return t;
}

QString NetworkManager::getMacVendor(const QString &mac) {
    QString p = mac.toUpper();
    QString oui = p.left(8); // "XX:XX:XX"
    return ouiTable().value(oui, "Unknown Vendor");
}

// macBytesToString is now a file-scope function above (shared with PassiveSniffer)

// ============================================================
// PassiveSniffer
// ============================================================
PassiveSniffer::PassiveSniffer(const QString &iface, QObject *parent)
    : QObject(parent), m_iface(iface) {}

PassiveSniffer::~PassiveSniffer() {
    stop();
}

void PassiveSniffer::start() {
    char errbuf[PCAP_ERRBUF_SIZE];
    m_handle = pcap_create(m_iface.toLocal8Bit().constData(), errbuf);
    if (!m_handle) {
        emit snifferError(QString("pcap_create failed: %1").arg(errbuf));
        return;
    }

    // Set configuration
    pcap_set_snaplen(m_handle, 65535);
    pcap_set_promisc(m_handle, 1);
    pcap_set_timeout(m_handle, 100);
    
    // [CRITICAL FIX] Increase capture buffer size to 32MB to prevent the kernel 
    // from dropping packets during high-speed local downloads (gigabit+ Wi-Fi)
    pcap_set_buffer_size(m_handle, 32 * 1024 * 1024);

    int status = pcap_activate(m_handle);
    if (status != 0) {
        emit snifferError(QString("pcap_activate failed: %1").arg(pcap_geterr(m_handle)));
        pcap_close(m_handle);
        m_handle = nullptr;
        return;
    }

    // BPF filter: Capture ALL IPv4 and ARP traffic for genuine bandwidth metrics
    const char *filter_str = "ip or arp";
    struct bpf_program fp;
    if (pcap_compile(m_handle, &fp, filter_str, 1, PCAP_NETMASK_UNKNOWN) == -1) {
        emit snifferError(QString("pcap_compile failed: %1").arg(pcap_geterr(m_handle)));
        pcap_close(m_handle);
        m_handle = nullptr;
        return;
    }
    pcap_setfilter(m_handle, &fp);
    pcap_freecode(&fp);

    m_running = true;
    pcap_loop(m_handle, -1, PassiveSniffer::pcapCallback, reinterpret_cast<unsigned char *>(this));
}

void PassiveSniffer::stop() {
    m_running = false;
    if (m_handle) {
        pcap_breakloop(m_handle);
        pcap_close(m_handle);
        m_handle = nullptr;
    }
}

void PassiveSniffer::pcapCallback(unsigned char *user, const struct pcap_pkthdr *h, const unsigned char *pkt) {
    auto *self = reinterpret_cast<PassiveSniffer *>(user);
    if (!self->m_running) return;
    self->processPacket(pkt, h->caplen);
}

void PassiveSniffer::processPacket(const unsigned char *pkt, int len) {
    if (len < 14) return;
    uint16_t ethtype = (pkt[12] << 8) | pkt[13];

    if (ethtype == 0x0806 && len >= 28) {
        // ARP
        // Sender IP at offset 28, Sender MAC at offset 22
        const unsigned char *senderMac = pkt + 22;
        const unsigned char *senderIp  = pkt + 28;
        QHostAddress ip(QString("%1.%2.%3.%4")
            .arg(senderIp[0]).arg(senderIp[1]).arg(senderIp[2]).arg(senderIp[3]));
        QString mac = macBytesToString(senderMac);
        if (!ip.isNull() && !mac.startsWith("00:00:00")) {
            emit deviceSeen(ip, mac);
        }
    }
    // mDNS / NetBIOS / SSDP — just extract source MAC/IP from IP header
    else if (ethtype == 0x0800 && len >= 34) {
        const unsigned char *srcMac = pkt + 6;
        const unsigned char *srcIp  = pkt + 26;
        QHostAddress ip(QString("%1.%2.%3.%4")
            .arg(srcIp[0]).arg(srcIp[1]).arg(srcIp[2]).arg(srcIp[3]));
        QString mac = macBytesToString(srcMac);
        if (!ip.isNull() && !mac.startsWith("00:00:00") && !mac.startsWith("ff:ff:ff")) {
            emit deviceSeen(ip, mac);
        }
    }
}

// ============================================================
// NetworkManager — constructor / destructor
// ============================================================
NetworkManager::NetworkManager(QObject *parent) : QObject(parent) {
    // Database
    DatabaseManager::instance().init();
    
    // Load persisted devices
    auto saved = DatabaseManager::instance().getAllDevices();
    for (const auto &d : saved) {
        m_allDevices.insert(d.ip(), d);
    }

    // Traffic Monitor — safe to create early, doesn't touch the network
    m_trafficMonitor = new TrafficMonitor();
    connect(m_trafficMonitor, &TrafficMonitor::trafficUpdated, this, &NetworkManager::onTrafficUpdated);
    connect(m_trafficMonitor, &TrafficMonitor::globalStats,    this, &NetworkManager::onGlobalStats);

    // Firewall — created but NOT initialized yet (no nftables rules written)
    m_firewallManager = new FirewallManager("", this);
    connect(m_firewallManager, &FirewallManager::firewallError, this, &NetworkManager::scanError);

    // DHCP Manager — safe to create early, no server started
    m_dhcpManager = new DHCPManager(this);
    connect(m_dhcpManager, &DHCPManager::dhcpError,         this, &NetworkManager::dhcpOperationError);
    connect(m_dhcpManager, &DHCPManager::operationSuccess,  this, &NetworkManager::dhcpOperationSuccess);
    connect(m_dhcpManager, &DHCPManager::dhcpStatusChanged, this, [this](bool active) {
        emit dhcpStatusUpdate(active);
    });
    connect(m_dhcpManager, &DHCPManager::logEvent, this, [this](const QString &msg) {
        logEvent(core::NetworkEvent::Info, msg);
    }, Qt::QueuedConnection);

    connect(m_dhcpManager, &DHCPManager::leaseDiscovered, this, [this](const core::DHCPLease &lease) {
        if (lease.hostname == "(pending)") return; // skip tentative offers; wait for ACK
        Device d;
        d.setIp(lease.ip);
        d.setMac(lease.mac);
        d.setHostname(lease.hostname.isEmpty() ? lease.ip : lease.hostname);
        d.setVendor(getMacVendor(lease.mac));
        d.setStatus("Online");
        addDiscoveredDevice(d, /*fromDhcp=*/true);
        m_firewallManager->addAllowedLease(lease.ip, lease.mac);
    }, Qt::QueuedConnection);

    connect(m_dhcpManager, &DHCPManager::leaseExpired, this, [this](const QString &ip, const QString &mac) {
        m_firewallManager->removeAllowedLease(ip, mac);
        logEvent(core::NetworkEvent::Info, QString("Lease expired for %1 (%2) — removed from firewall").arg(ip, mac), ip);
    }, Qt::QueuedConnection);

    // Captive Portal — safe to create early
    m_captivePortal = new CaptivePortalManager(this);
    connect(m_captivePortal, &CaptivePortalManager::deviceStateChanged, this, [this](const QString &mac, bool enabled) {
        if (enabled && isDeviceBlocked(mac)) {
            m_firewallManager->enableBlockPageForMAC(mac);
        } else {
            m_firewallManager->disableBlockPageForMAC(mac);
        }
    });

    // Router Detector
    m_routerDetector = new RouterDetector();
    m_routerThread   = new QThread(this);
    m_routerDetector->moveToThread(m_routerThread);
    connect(m_routerThread, &QThread::finished, m_routerDetector, &QObject::deleteLater);
    connect(m_routerDetector, &RouterDetector::routerInfoReady,
            this, &NetworkManager::routerInfoReady, Qt::QueuedConnection);
    connect(m_routerDetector, &RouterDetector::detectionStage,
            this, &NetworkManager::routerDetectionStage, Qt::QueuedConnection);
    m_routerThread->start();

    // NOTE: PassiveSniffer, FirewallManager init, and cleanup timer are deferred
    // to activate() which is called only after the startup wizard completes.
    // This prevents any scan/firewall activity while the wizard is open.
}

void NetworkManager::activate() {
    QString iface = getActiveInterface();

    // Initialize the firewall now that we know the interface
    m_firewallManager->setInterface(iface);

    // Sync persisted blacklist/whitelist
    QList<Device> historicalDevices = DatabaseManager::instance().getAllDevices();
    for (const Device &d : historicalDevices) {
        QString lMac = d.mac().toLower();
        if (DatabaseManager::instance().isBlacklisted(lMac)) {
            m_firewallManager->blockMAC(lMac);
            m_dhcpManager->blockMAC(lMac);
        }
        if (DatabaseManager::instance().isWhitelisted(lMac)) {
            m_firewallManager->addWhitelistedMAC(lMac);
            m_dhcpManager->addWhitelistedMAC(lMac);
        }
    }

    // Start passive sniffer
    if (!iface.isEmpty() && !m_sniffer) {
        m_sniffer       = new PassiveSniffer(iface);
        m_snifferThread = new QThread(this);
        m_sniffer->moveToThread(m_snifferThread);
        connect(m_snifferThread, &QThread::started,  m_sniffer, &PassiveSniffer::start);
        connect(m_sniffer, &PassiveSniffer::deviceSeen,   this, &NetworkManager::onDeviceSeen,   Qt::QueuedConnection);
        connect(m_sniffer, &PassiveSniffer::snifferError, this, &NetworkManager::scanError,       Qt::QueuedConnection);
        connect(m_snifferThread, &QThread::finished, m_sniffer, &QObject::deleteLater);
        m_snifferThread->start();
    }

    // Start cleanup timer
    if (!m_cleanupTimer) {
        m_cleanupTimer = new QTimer(this);
        connect(m_cleanupTimer, &QTimer::timeout, this, &NetworkManager::cleanUpStaleDevices);
        m_cleanupTimer->start(60000);
    }

    qDebug() << "[NetworkManager] Activated on interface" << iface;
    runScan();
}


NetworkManager::~NetworkManager() {
    if (m_snifferThread) {
        if (m_sniffer) m_sniffer->stop();
        m_snifferThread->quit();
        m_snifferThread->wait(2000);
    }
    if (m_captureThread) {
        if (m_packetCapturer) m_packetCapturer->stopCapture();
        m_captureThread->quit();
        m_captureThread->wait(2000);
    }
    if (m_routerThread) {
        m_routerThread->quit();
        m_routerThread->wait(3000);
    }
}

// ============================================================
// Delegating slots (unchanged interface)
// ============================================================
void NetworkManager::onRefreshRequested()                            { runScan(); }
void NetworkManager::toggleDHCP(bool enable)                          { if (enable) m_dhcpManager->startServer(); else m_dhcpManager->stopServer(); }
void NetworkManager::startDHCPServer(const DHCPServerConfig &c)       { m_dhcpManager->configureDHCPServer(c); }
void NetworkManager::stopDHCPServer()                                  { m_dhcpManager->stopServer(); }

void NetworkManager::startScanning(const QString &interfaceName) {
    m_interfaceName = interfaceName;
    m_firewallManager->setInterface(interfaceName);

    // Sync persisted blacklist/whitelist with DHCP and firewall
    QList<Device> historicalDevices = DatabaseManager::instance().getAllDevices();
    for (const Device &d : historicalDevices) {
        QString lMac = d.mac().toLower();
        if (DatabaseManager::instance().isBlacklisted(lMac)) {
            m_firewallManager->blockMAC(lMac);
            m_dhcpManager->blockMAC(lMac);
        }
        if (DatabaseManager::instance().isWhitelisted(lMac)) {
            m_firewallManager->addWhitelistedMAC(lMac);
            m_dhcpManager->addWhitelistedMAC(lMac);
        }
    }

    runScan();
}

void NetworkManager::addStaticLease(const QString &mac, const QString &ip, const QString &host) {
    m_dhcpManager->addStaticLease(mac, ip, host);
    QMetaObject::invokeMethod(this, "runScan", Qt::QueuedConnection);
}

bool NetworkManager::isDeviceBlocked(const QString &mac) const {
    return m_firewallManager && m_firewallManager->isMACBlocked(mac);
}



// ============================================================
// Traffic / stats slots
// ============================================================
void NetworkManager::onTrafficUpdated(const QMap<QString, TrafficStats> &stats) {
    QMutexLocker lk(&m_resultsMutex);
    m_latestStats = stats;
    emit trafficUpdated(stats);
    auto fmt = [](quint32 b) -> QString {
        if (b < 1024)       return QString::number(b)                       + " B/s";
        if (b < 1024*1024)  return QString::number(b / 1024.0, 'f', 1)     + " KB/s";
        return               QString::number(b / (1024.0*1024.0), 'f', 1)  + " MB/s";
    };
    bool changed = false;
    for (auto it = m_allDevices.begin(); it != m_allDevices.end(); ++it) {
        auto statIt = m_latestStats.find(it.key()); // it.key() is IP
        if (statIt != m_latestStats.end()) {
            it.value().setUpBandwidth(fmt(statIt->currentRateUp));
            it.value().setDownBandwidth(fmt(statIt->currentRateDown));
            changed = true;
        }
    }
    if (changed) emit devicesUpdated(m_allDevices.values());
}

void NetworkManager::updateDeviceAlias(const QString &mac, const QString &alias) {
    DatabaseManager::instance().updateAlias(mac, alias);
    
    // Update live state
    for (auto it = m_allDevices.begin(); it != m_allDevices.end(); ++it) {
        if (it.value().mac() == mac) {
            it.value().setAlias(alias);
        }
    }
    emit devicesUpdated(m_allDevices.values());
}

void NetworkManager::addWhitelistedMAC(const QString &mac) {
    DatabaseManager::instance().addToWhitelist(mac);
    if (m_dhcpManager->addWhitelistedMAC(mac)) {
        m_firewallManager->addWhitelistedMAC(mac);
        logEvent(NetworkEvent::Security, QString("MAC %1 added to whitelist (persisted)").arg(mac));
    }
}

void NetworkManager::removeWhitelistedMAC(const QString &mac) {
    DatabaseManager::instance().removeFromWhitelist(mac);
    if (m_dhcpManager->removeWhitelistedMAC(mac)) {
        m_firewallManager->removeWhitelistedMAC(mac);
        logEvent(NetworkEvent::Security, QString("MAC %1 removed from whitelist (persisted)").arg(mac));
    }
}


void NetworkManager::logEvent(NetworkEvent::Type type, const QString &message, const QString &ip) {
    Q_UNUSED(type)
    Q_UNUSED(ip)
    
    // UI and DB logging have been removed. We only log to console now.
    qDebug() << "[Network Event]" << message;
}

void NetworkManager::onGlobalStats(int pkts, double pps, quint64 totalIn, quint64 totalOut) {
    emit globalTrafficStatus(QString("Capturing: %1 pkts | %2 pkts/s").arg(pkts).arg((int)pps));
    emit globalTrafficStatsUpdated(pkts, pps, totalIn, totalOut);
}

void NetworkManager::onDeviceSeen(const QHostAddress &ip, const QString &mac) {
    QString lMac = mac.toLower();

    // FILTER: Ignore external public internet IPs, loopbacks, and multicasts
    // We only care about internal LAN devices (RFC 1918 Private Ranges)
    quint32 ipAddr = ip.toIPv4Address();
    bool isPrivate = (ipAddr >= 0x0A000000 && ipAddr <= 0x0AFFFFFF) || // 10.0.0.0/8
                     (ipAddr >= 0xAC100000 && ipAddr <= 0xAC1FFFFF) || // 172.16.0.0/12
                     (ipAddr >= 0xC0A80000 && ipAddr <= 0xC0A8FFFF);   // 192.168.0.0/16

    if (!isPrivate && ip.toString() != m_gatewayIp) {
        return;
    }

    // Capture gateway MAC for identification
    if (ip.toString() == m_gatewayIp && !mac.isEmpty() && mac != "00:00:00:00:00:00") {
        if (m_gatewayMac != mac && mac != m_myMac) {
            m_gatewayMac = mac;
            qDebug() << "[NetworkManager] Resolved gateway MAC:" << mac;
            logEvent(NetworkEvent::Info, QString("Gateway resolved to %1").arg(mac));
        }
    }

    Device d;
    d.setIp(ip.toString());
    d.setMac(mac);
    d.setVendor(getMacVendor(mac));
    d.setLastSeen(QDateTime::currentDateTime());
    mergeArpEntry(ip.toString(), mac);
    emit deviceSeen(ip, mac);
}


// ============================================================
// Passive capture for traffic monitor (existing)
// ============================================================
void NetworkManager::startCapture(const QString &iface) {
    if (m_captureThread) return;
    m_captureThread  = new QThread(this);
    m_packetCapturer = new PacketCapture(iface);
    m_packetCapturer->moveToThread(m_captureThread);
    
    connect(m_captureThread, &QThread::started,  m_packetCapturer, &PacketCapture::startCapture);
    connect(m_packetCapturer, &PacketCapture::packetCaptured,
            m_trafficMonitor, &TrafficMonitor::processPacket, Qt::DirectConnection);
    connect(m_captureThread, &QThread::finished, m_packetCapturer, &QObject::deleteLater);
    m_captureThread->start();
}

// ============================================================
// Merge helper (thread-safe)
// ============================================================
void NetworkManager::mergeArpEntry(const QString &ip, const QString &mac, const QString &iface) {
    Q_UNUSED(iface)
    QMutexLocker lk(&m_resultsMutex);
    
    Device existingDev;
    bool moved = false;
    
    // Check if this MAC already exists under a DIFFERENT IP (device moved IPs)
    if (!mac.isEmpty() && mac != "00:00:00:00:00:00") {
        QString oldIp;
        for (auto it = m_allDevices.begin(); it != m_allDevices.end(); ++it) {
            if (it.value().mac() == mac && it.key() != ip) {
                oldIp = it.key();
                break;
            }
        }
        if (!oldIp.isEmpty()) {
            existingDev = m_allDevices[oldIp];
            DatabaseManager::instance().removeDevice(oldIp);
            m_allDevices.remove(oldIp);
            moved = true;
        }
    }

    if (m_allDevices.contains(ip)) {
        Device &d = m_allDevices[ip];
        if (!mac.isEmpty() && mac != "00:00:00:00:00:00" && d.mac() != mac) {
            d.setMac(mac);
            d.setVendor(getMacVendor(mac));
        }
        d.setLastSeen(QDateTime::currentDateTime());
        if (d.status().toLower() == "offline") {
            bool blocked = DatabaseManager::instance().isBlacklisted(mac) || (m_strictMode && !DatabaseManager::instance().isWhitelisted(mac));
            d.setStatus(blocked ? "Blocked" : "Online");
        }
        
        // Capture gateway MAC for identification
        if (ip == m_gatewayIp) {
            if (mac != m_myMac) {
                m_gatewayMac = mac;
            }
        }
        DatabaseManager::instance().saveDevice(d);
    } else {
        if (!mac.isEmpty() && mac != "00:00:00:00:00:00") {
            Device d = moved ? existingDev : Device();
            d.setIp(ip);
            d.setMac(mac);
            if (!moved || d.vendor().isEmpty()) {
                d.setVendor(getMacVendor(mac));
            }
            bool blocked = DatabaseManager::instance().isBlacklisted(mac) || (m_strictMode && !DatabaseManager::instance().isWhitelisted(mac));
            d.setStatus(blocked ? "Blocked" : "Online");
            m_allDevices.insert(ip, d);
            
            DatabaseManager::instance().saveDevice(d);
            logEvent(NetworkEvent::Discovery, QString("New device discovered: %1").arg(ip), ip);
        }
    }
    
    emit devicesUpdated(m_allDevices.values());
}

void NetworkManager::cleanUpStaleDevices() {
    QMutexLocker lk(&m_resultsMutex);
    bool changed = false;
    QDateTime now = QDateTime::currentDateTime();

    auto it = m_allDevices.begin();
    while (it != m_allDevices.end()) {
        QString hostIpStr = QHostAddress(m_myIpAddr).toString();
        
        qint64 diff = it.value().lastSeen().secsTo(now);
        if (diff > 30) {
            // Uniquely spare the Host device from ever reporting Offline.
            if (it.value().status().toLower() == "online" && it.value().status() != "Blocked" && it.key() != hostIpStr) {
                it.value().setStatus("Offline");
                changed = true;
            }
        }
        ++it;
    }
    
    if (changed) emit devicesUpdated(m_allDevices.values());
}

void NetworkManager::addDiscoveredDevice(const Device &dev, bool fromDhcp) {
    QMutexLocker lk(&m_resultsMutex);
    bool isBlocked = DatabaseManager::instance().isBlacklisted(dev.mac())
                  || (m_strictMode && !DatabaseManager::instance().isWhitelisted(dev.mac()));
    QString properStatus = isBlocked ? "Blocked" : (fromDhcp ? "Online" : dev.status());

    Device existingDev;
    bool moved = false;

    // Clean up ghosts: if this MAC exists on a different IP, delete the old IP entry
    if (!dev.mac().isEmpty() && dev.mac() != "00:00:00:00:00:00") {
        QString oldIp;
        for (auto it = m_allDevices.begin(); it != m_allDevices.end(); ++it) {
            if (it.value().mac() == dev.mac() && it.key() != dev.ip()) {
                oldIp = it.key();
                break;
            }
        }
        if (!oldIp.isEmpty()) {
            existingDev = m_allDevices[oldIp];
            DatabaseManager::instance().removeDevice(oldIp);
            m_allDevices.remove(oldIp);
            moved = true;
        }
    }

    if (m_allDevices.contains(dev.ip())) {
        Device &d = m_allDevices[dev.ip()];
        bool isHost = (d.mac() == m_myMac || dev.ip() == QHostAddress(m_myIpAddr).toString());
        
        // DHCP is authoritative: always update MAC and hostname when a lease is issued.
        // For ARP/passive discoveries, only update if we have better data.
        if (fromDhcp) {
            if (!dev.mac().isEmpty())     d.setMac(dev.mac());
            if (!dev.hostname().isEmpty() && !isHost) d.setHostname(dev.hostname()); // Protect Hostname
            if (!dev.vendor().isEmpty() && !isHost)  d.setVendor(dev.vendor());
        } else {
            if (!dev.hostname().isEmpty() && dev.hostname() != "Unknown" && !isHost)
                d.setHostname(dev.hostname());
        }
        
        // Ensure Host strictly retains its special visual tag.
        if (isHost) {
            d.setStatus("Online (Self)");
            if (d.hostname() == "Unknown" || d.hostname() == "localhost") d.setHostname(QHostInfo::localHostName());
            d.setVendor("This Device (Host)");
        } else if (!properStatus.isEmpty()) {
            d.setStatus(properStatus);
        }
        
        d.setLastSeen(QDateTime::currentDateTime());
        DatabaseManager::instance().saveDevice(d);
    } else {
        Device newDev = moved ? existingDev : dev;
        newDev.setIp(dev.ip());
        newDev.setMac(dev.mac());
        // For moves or fresh devices, bring over new info if it exists
        if (!dev.hostname().isEmpty() && dev.hostname() != "Unknown")
            newDev.setHostname(dev.hostname());
        if (!dev.vendor().isEmpty())
            newDev.setVendor(dev.vendor());
            
        if (!properStatus.isEmpty()) newDev.setStatus(properStatus);
        newDev.setLastSeen(QDateTime::currentDateTime());
        m_allDevices.insert(newDev.ip(), newDev);
        DatabaseManager::instance().saveDevice(newDev);
        
        if (!moved) {
            QString methodStr = fromDhcp ? "via DHCP: " : "";
            logEvent(NetworkEvent::Discovery, QString("New device discovered %1%2 (%3)"
                     ).arg(methodStr, newDev.ip(), newDev.hostname()), newDev.ip());
        }
    }
    emit devicesUpdated(m_allDevices.values());
}

// ============================================================
// STEP 1 — Read /proc/net/arp
// ============================================================
void NetworkManager::step1_readArpCache() {
    emit statusMessage("Step 1/5: Reading ARP cache…");
    QFile f("/proc/net/arp");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&f);
    in.readLine(); // skip header
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        QStringList parts = line.split(QRegularExpression("\\s+"));
        if (parts.size() < 6) continue;
        QString ip    = parts[0];
        QString flags = parts[2]; // "0x2" = complete
        QString mac   = parts[3];
        if (flags == "0x0" || mac == "00:00:00:00:00:00") continue;
        mergeArpEntry(ip, mac);
    }
    emit devicesUpdated(m_allDevices.values());
}

// ============================================================
// STEP 2 — ARP sweep via AF_PACKET
// ============================================================
int NetworkManager::openArpSocket(const QString &iface) {
    int sock = ::socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ARP));
    if (sock < 0) {
        emit scanError(QString("Raw ARP socket failed (%1) — run: sudo setcap cap_net_raw+eip ./LANMonitor")
                       .arg(strerror(errno)));
        return -1;
    }
    struct ifreq ifr{};
    strncpy(ifr.ifr_name, iface.toLocal8Bit().constData(), IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        emit scanError(QString("ioctl SIOCGIFINDEX failed: %1").arg(strerror(errno)));
        ::close(sock);
        return -1;
    }
    struct sockaddr_ll sll{};
    sll.sll_family   = AF_PACKET;
    sll.sll_protocol = htons(ETH_P_ARP);
    sll.sll_ifindex  = ifr.ifr_ifindex;
    if (::bind(sock, reinterpret_cast<struct sockaddr *>(&sll), sizeof(sll)) < 0) {
        emit scanError(QString("AF_PACKET bind failed: %1").arg(strerror(errno)));
        ::close(sock);
        return -1;
    }
    return sock;
}

bool NetworkManager::sendRawArpRequest(int sock, const QString &iface,
                                       const QString &srcMac, const QString &srcIp,
                                       const QString &targetIp) {
    // Ethernet + ARP frame, 42 bytes total
    unsigned char frame[42] = {};

    // Ethernet dst = broadcast
    memset(frame, 0xff, 6);

    // Parse source MAC
    unsigned int mb[6];
    if (sscanf(srcMac.toLatin1().constData(), "%x:%x:%x:%x:%x:%x",
               &mb[0], &mb[1], &mb[2], &mb[3], &mb[4], &mb[5]) != 6) return false;
    for (int i = 0; i < 6; ++i) frame[6 + i] = (unsigned char)mb[i];

    // Ethertype = ARP
    frame[12] = 0x08; frame[13] = 0x06;

    // ARP header
    frame[14] = 0x00; frame[15] = 0x01; // HTYPE Ethernet
    frame[16] = 0x08; frame[17] = 0x00; // PTYPE IPv4
    frame[18] = 6;                       // HLEN
    frame[19] = 4;                       // PLEN
    frame[20] = 0x00; frame[21] = 0x01; // OPER request

    // SHA (sender MAC)
    for (int i = 0; i < 6; ++i) frame[22 + i] = (unsigned char)mb[i];

    // SPA (sender IP)
    struct in_addr spa{};
    inet_pton(AF_INET, srcIp.toLatin1().constData(), &spa);
    memcpy(frame + 28, &spa, 4);

    // THA = zeros (we don't know)
    memset(frame + 32, 0, 6);

    // TPA (target IP)
    struct in_addr tpa{};
    inet_pton(AF_INET, targetIp.toLatin1().constData(), &tpa);
    memcpy(frame + 38, &tpa, 4);

    // Get interface index
    struct ifreq ifr{};
    strncpy(ifr.ifr_name, iface.toLocal8Bit().constData(), IFNAMSIZ - 1);
    ioctl(sock, SIOCGIFINDEX, &ifr);

    struct sockaddr_ll sll{};
    sll.sll_family   = AF_PACKET;
    sll.sll_protocol = htons(ETH_P_ARP);
    sll.sll_ifindex  = ifr.ifr_ifindex;
    sll.sll_halen    = 6;
    memset(sll.sll_addr, 0xff, 6);

    return ::sendto(sock, frame, sizeof(frame), 0,
                    reinterpret_cast<struct sockaddr *>(&sll), sizeof(sll)) == sizeof(frame);
}

void NetworkManager::step2_arpSweep(const QString &iface, quint32 networkAddr, quint32 broadcastAddr,
                                     quint32 myIpAddr, const QString &myMac) {
    quint32 hostCount = broadcastAddr - networkAddr - 1;
    if (hostCount > 65534) {
        emit statusMessage("Step 2/5: Subnet too large for ARP sweep (max /16), skipping.");
        return;
    }
    emit statusMessage(QString("Step 2/5: ARP sweep — %1 hosts…").arg(hostCount));

    int sendSock = openArpSocket(iface);
    if (sendSock < 0) return;

    // Open receive socket in parallel
    int recvSock = ::socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ARP));
    if (recvSock >= 0) {
        struct timeval tv{ 0, 100000 }; // 100ms receive timeout
        setsockopt(recvSock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    QString srcIp = QHostAddress(myIpAddr).toString();

    // Send thread
    auto *sendThread = QThread::create([&] {
        quint32 intervalUs = 1000; // 1000 pkt/s => 1ms between pkts
        for (quint32 ip = networkAddr + 1; ip < broadcastAddr; ++ip) {
            if (ip == myIpAddr) continue;
            sendRawArpRequest(sendSock, iface, myMac, srcIp, QHostAddress(ip).toString());
            ::usleep(intervalUs);
        }
    });
    sendThread->start();

    // Receive loop (2000ms total)
    auto deadline = QDateTime::currentMSecsSinceEpoch() + 2000;
    while (QDateTime::currentMSecsSinceEpoch() < deadline && recvSock >= 0) {
        unsigned char buf[128];
        int n = ::recv(recvSock, buf, sizeof(buf), 0);
        if (n < 42) continue;

        uint16_t ethtype = (buf[12] << 8) | buf[13];
        if (ethtype != 0x0806) continue;

        uint16_t oper = (buf[20] << 8) | buf[21];
        if (oper != 2) continue; // only ARP replies

        const unsigned char *senderMac = buf + 22;
        const unsigned char *senderIp  = buf + 28;

        QString mac = macBytesToString(senderMac);
        QString ip  = QString("%1.%2.%3.%4")
                      .arg(senderIp[0]).arg(senderIp[1]).arg(senderIp[2]).arg(senderIp[3]);

        if (!mac.startsWith("00:00:00")) {
            mergeArpEntry(ip, mac, iface);
            m_confirmedIps.insert(ip);

            // Capture gateway MAC
            if (ip == m_gatewayIp && m_gatewayMac.isEmpty())
                m_gatewayMac = mac;
        }
    }

    sendThread->wait();
    delete sendThread;
    ::close(sendSock);
    if (recvSock >= 0) ::close(recvSock);
}

// ============================================================
// STEP 3 — ICMP + TCP SYN fan-out for unconfirmed hosts
// ============================================================

// Checksum helper
static uint16_t ipChecksum(const void *data, int len) {
    auto *ptr = reinterpret_cast<const uint16_t *>(data);
    uint32_t sum = 0;
    while (len > 1) { sum += *ptr++; len -= 2; }
    if (len) sum += *(const uint8_t *)ptr;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

class ProbeRunnable : public QRunnable {
public:
    ProbeRunnable(const QString &targetIp, quint32 srcIp, std::function<void(const QString &)> onAlive)
        : m_target(targetIp), m_srcIp(srcIp), m_onAlive(std::move(onAlive)) {
        setAutoDelete(true);
    }

    void run() override {
        struct in_addr dst{};
        if (inet_pton(AF_INET, m_target.toLatin1().constData(), &dst) != 1) return;

        // ICMP echo only — fast 300ms timeout
        if (probeICMP(dst)) { m_onAlive(m_target); }
    }

private:
    bool probeICMP(struct in_addr dst) {
        int s = ::socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
        if (s < 0) return false;
        struct timeval tv{ 0, 300000 }; // 300ms
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        struct icmphdr req{};
        req.type = ICMP_ECHO;
        req.code = 0;
        req.un.echo.id  = (uint16_t)getpid();
        req.un.echo.sequence = 1;
        req.checksum = ipChecksum(&req, sizeof(req));

        struct sockaddr_in sin{};
        sin.sin_family = AF_INET;
        sin.sin_addr   = dst;

        if (::sendto(s, &req, sizeof(req), 0,
                     (struct sockaddr *)&sin, sizeof(sin)) < 0) { ::close(s); return false; }

        unsigned char buf[64];
        socklen_t sl = sizeof(sin);
        bool alive = ::recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr *)&sin, &sl) > 0;
        ::close(s);
        return alive;
    }

    QString m_target;
    quint32 m_srcIp;
    std::function<void(const QString &)> m_onAlive;
};

void NetworkManager::step3_probeUnconfirmed(const QString &iface, quint32 networkAddr,
                                             quint32 broadcastAddr, quint32 myIpAddr) {
    Q_UNUSED(iface)
    QList<QString> unconfirmed;
    {
        QMutexLocker lk(&m_resultsMutex);
        for (quint32 ip = networkAddr + 1; ip < broadcastAddr; ++ip) {
            QString ipStr = QHostAddress(ip).toString();
            if (ip != myIpAddr && !m_confirmedIps.contains(ipStr))
                unconfirmed.append(ipStr);
        }
    }

    if (unconfirmed.isEmpty()) return;
    // Cap to avoid excessive probing on large subnets
    if (unconfirmed.size() > 256) {
        emit statusMessage("Step 3/5: Subnet too large, skipping deep probes.");
        return;
    }

    emit statusMessage(QString("Step 3/5: Probing %1 unconfirmed hosts (ICMP, 300ms)…").arg(unconfirmed.size()));
    emit scanProgress(60);

    // Use a dedicated pool so we don't starve globalInstance()
    QThreadPool pool;
    pool.setMaxThreadCount(48);

    for (const auto &ip : unconfirmed) {
        auto *r = new ProbeRunnable(ip, myIpAddr, [this](const QString &aliveIp) {
            QMetaObject::invokeMethod(this, [this, aliveIp]() {
                mergeArpEntry(aliveIp, "");
            }, Qt::QueuedConnection);
        });
        pool.start(r);
    }
    pool.waitForDone(15000); // up to 15 s for the whole batch
}

// ============================================================
// STEP 4 — Hostname + OS fingerprinting
// ============================================================
void NetworkManager::step4_fingerprint(const QString &iface) {
    Q_UNUSED(iface)
    emit statusMessage("Step 4/5: Fingerprinting hostnames…");

    QMutexLocker lk(&m_resultsMutex);
    for (auto &dev : m_allDevices) {
        if (dev.hostname() != "Unknown") continue;

        // NetBIOS NS query (UDP 137)
        struct sockaddr_in target{};
        target.sin_family = AF_INET;
        target.sin_port   = htons(137);
        inet_pton(AF_INET, dev.ip().toLatin1().constData(), &target.sin_addr);

        int s = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (s < 0) continue;
        struct timeval tv{ 0, 300000 };
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        // Minimal NetBIOS name query
        unsigned char nbns[50] = {
            0xAB, 0xCD,  // Transaction ID
            0x00, 0x00,  // Flags: query
            0x00, 0x01,  // Questions: 1
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Ans/Auth/Add RRs
            0x20,        // Name length (32 encoded bytes)
            // NetBIOS wildcard name "CKAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
            'C','K','A','A','A','A','A','A','A','A','A','A','A','A','A','A',
            'A','A','A','A','A','A','A','A','A','A','A','A','A','A','A','A',
            0x00,        // Terminator
            0x00, 0x21,  // QTYPE NB_STAT
            0x00, 0x01,  // QCLASS IN
        };

        if (::sendto(s, nbns, sizeof(nbns), 0,
                     (struct sockaddr *)&target, sizeof(target)) > 0) {
            unsigned char buf[512];
            socklen_t sl = sizeof(target);
            int n = ::recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr *)&target, &sl);
            if (n > 57) {
                // Parse name from NetBIOS response (offset 57, 15 chars)
                char name[16] = {};
                memcpy(name, buf + 57, 15);
                name[15] = '\0';
                QString qname = QString::fromLatin1(name).trimmed();
                if (!qname.isEmpty() && qname != "*")
                    dev.setHostname(qname);
            }
        }
        ::close(s);

        // mDNS PTR query (UDP 5353)
        if (dev.hostname() == "Unknown") {
            struct sockaddr_in mdns{};
            mdns.sin_family = AF_INET;
            mdns.sin_port   = htons(5353);
            inet_pton(AF_INET, "224.0.0.251", &mdns.sin_addr);

            int ms = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (ms >= 0) {
                struct timeval tv2{ 0, 300000 };
                setsockopt(ms, SOL_SOCKET, SO_RCVTIMEO, &tv2, sizeof(tv2));
                // Build reversed IP PTR query e.g. "1.1.168.192.in-addr.arpa"
                QStringList parts = dev.ip().split(".");
                if (parts.size() == 4) {
                    QString ptr = parts[3] + "." + parts[2] + "." + parts[1] + "." + parts[0] + ".in-addr.arpa";
                    // Simplified mDNS query — just send, parsing full DNS is complex
                    // We rely on passive sniffer for full mDNS parsing
                }
                ::close(ms);
            }
        }
    }
}

// ============================================================
// MAIN SCAN ENTRY POINT
// ============================================================
void NetworkManager::runScan() {
    // Throttling: prevent redundant scans from clogging the event loop
    static QDateTime lastScan;
    if (lastScan.isValid() && lastScan.msecsTo(QDateTime::currentDateTime()) < 5000) return;
    lastScan = QDateTime::currentDateTime();

    QString iface = getActiveInterface();
    if (iface.isEmpty()) { emit scanError("No active network interface found."); return; }

    startCapture(iface); // traffic monitor

    QNetworkInterface qiface = QNetworkInterface::interfaceFromName(iface);
    QHostAddress myAddress, myNetmask;
    for (const auto &e : qiface.addressEntries()) {
        if (e.ip().protocol() == QAbstractSocket::IPv4Protocol) {
            myAddress = e.ip(); myNetmask = e.netmask(); break;
        }
    }
    if (myAddress.isNull()) { emit scanError("No IPv4 address found on interface."); return; }

    m_myIpAddr    = myAddress.toIPv4Address();
    m_netmaskAddr = myNetmask.toIPv4Address();
    m_networkAddr = m_myIpAddr & m_netmaskAddr;
    quint32 broadcastAddr = m_networkAddr | (~m_netmaskAddr);

    // Provide network info to TrafficMonitor for inbound/outbound classification
    m_trafficMonitor->setLocalNetwork(m_myIpAddr, m_netmaskAddr);

    // [MODERNIZE] Clear list if subnet/network changed to prevent ghost devices from old locations
    if (m_networkAddr != m_prevNetworkAddr || m_netmaskAddr != m_prevNetmaskAddr) {
        if (m_prevNetworkAddr != 0) { // Don't log "changed" on the very first run
            logEvent(core::NetworkEvent::Info, "Network context changed. Clearing stale device list.");
        }
        QMutexLocker lk(&m_resultsMutex);
        m_allDevices.clear();
        m_trafficMonitor->resetStats();
        m_prevNetworkAddr = m_networkAddr;
        m_prevNetmaskAddr = m_netmaskAddr;
    }

    setProperty("activeInterface", iface);
    setProperty("myIp", myAddress.toString());
    setProperty("gatewayIp", m_gatewayIp);
    setProperty("hostMac", getMyMac(iface));  // expose for DHCPPage self-filter
    
    // Sync laptop IP with firewall for ARP suppression
    m_firewallManager->setServerIP(myAddress.toString());

    // Step 0: Don't clear, just clear current scan confirmations
    QMutexLocker lk(&m_resultsMutex);
    m_confirmedIps.clear();
    lk.unlock();

    auto leases = readDHCPLeases();
    m_myMac = getMyMac(iface);
    m_gatewayIp = getGatewayIP();

    // Sync host identity to TrafficMonitor
    m_trafficMonitor->setHostIdentity(m_myMac, m_myIpAddr);


    // Self
    Device self;
    self.setIp(myAddress.toString());
    self.setMac(m_myMac);
    self.setVendor("This Device (Host)");
    self.setHostname(QHostInfo::localHostName());
    self.setStatus("Online (Self)");
    addDiscoveredDevice(self);
    m_confirmedIps.insert(myAddress.toString());

    // Gateway placeholder & Resolution
    if (!m_gatewayIp.isEmpty()) {
        if (m_gatewayMac.isEmpty() || m_gatewayMac == "Checking...") {
            int sock = openArpSocket(iface);
            if (sock >= 0) {
                sendRawArpRequest(sock, iface, m_myMac, myAddress.toString(), m_gatewayIp);
                ::close(sock);
            }
        }

        Device gw;
        gw.setIp(m_gatewayIp);
        gw.setMac(m_gatewayMac.isEmpty() ? "Checking..." : m_gatewayMac);
        gw.setVendor("Router / Gateway");
        gw.setHostname(leases.value(m_gatewayIp, "router"));
        gw.setStatus("Online (Gateway)");
        addDiscoveredDevice(gw);
        m_confirmedIps.insert(m_gatewayIp);
    }

    emit scanProgress(10);
    if (!m_firstScanLogged) {
        logEvent(NetworkEvent::Info, "Starting network discovery scan...");
        m_firstScanLogged = true;
    }

    // Steps 1 + 2: fast (ARP cache + ARP sweep) — run synchronously
    step1_readArpCache();
    emit scanProgress(25);
    step2_arpSweep(iface, m_networkAddr, broadcastAddr, m_myIpAddr, m_myMac);
    emit scanProgress(50);

    // Enrich immediately with any DHCP leases we already have
    {
        QMutexLocker lk2(&m_resultsMutex);
        for (auto &d : m_allDevices)
            if (d.hostname() == "Unknown" && leases.contains(d.ip()))
                d.setHostname(leases[d.ip()]);
        emit devicesUpdated(m_allDevices.values());
    }

    // Steps 3 + 4: slow probes — detach onto a background thread so runScan returns fast
    quint32 netAddr  = m_networkAddr;
    quint32 bcastAddr = broadcastAddr;
    quint32 myIpAddr  = m_myIpAddr;
    auto capturedLeases = leases; // copy for lambda

    auto *bgThread = QThread::create([this, iface, netAddr, bcastAddr, myIpAddr, capturedLeases]() {
        step3_probeUnconfirmed(iface, netAddr, bcastAddr, myIpAddr);
        emit scanProgress(80);
        
        // Trigger router detection NOW. The main thread has processed the ARP replies,
        // so m_gatewayMac is populated. This runs in parallel with step4.
        QMetaObject::invokeMethod(this, [this]() {
            triggerRouterDetection();
        }, Qt::QueuedConnection);

        step4_fingerprint(iface);
        emit scanProgress(95);

        // Final DHCP enrichment
        {
            QMutexLocker lk3(&m_resultsMutex);
            for (auto &d : m_allDevices)
                if (d.hostname() == "Unknown" && capturedLeases.contains(d.ip()))
                    d.setHostname(capturedLeases[d.ip()]);
            emit devicesUpdated(m_allDevices.values());
        }
        emit scanProgress(100);
        emit statusMessage(QString("Scan complete — %1 online").arg(m_allDevices.size()));
    });
    bgThread->setObjectName("ScanStep3-4");
    connect(bgThread, &QThread::finished, bgThread, &QObject::deleteLater);
    bgThread->start();
}

// ============================================================
// Utility helpers
// ============================================================
QString NetworkManager::getMyMac(const QString &iface) {
    struct ifreq ifr{};
    strncpy(ifr.ifr_name, iface.toLocal8Bit().constData(), IFNAMSIZ - 1);
    int s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return "";
    ioctl(s, SIOCGIFHWADDR, &ifr);
    ::close(s);
    return macBytesToString(reinterpret_cast<const unsigned char *>(ifr.ifr_hwaddr.sa_data));
}

QString NetworkManager::getGatewayIP() {
    std::ifstream f("/proc/net/route");
    std::string l;
    while (std::getline(f, l)) {
        std::stringstream ss(l);
        std::string ifc, dst, gw;
        ss >> ifc >> dst >> gw;
        if (dst == "00000000" && !gw.empty()) {
            unsigned int g;
            std::stringstream c; c << std::hex << gw; c >> g;
            struct in_addr a; a.s_addr = g;
            return QString::fromLatin1(inet_ntoa(a));
        }
    }
    return "";
}

QHostAddress NetworkManager::getInterfaceAddress(const QString &iface) {
    QNetworkInterface i = QNetworkInterface::interfaceFromName(iface);
    for (const auto &e : i.addressEntries())
        if (e.ip().protocol() == QAbstractSocket::IPv4Protocol)
            return e.ip();
    return QHostAddress();
}

QHostAddress NetworkManager::getInterfaceNetmask(const QString &iface) {
    QNetworkInterface i = QNetworkInterface::interfaceFromName(iface);
    for (const auto &e : i.addressEntries())
        if (e.ip().protocol() == QAbstractSocket::IPv4Protocol)
            return e.netmask();
    return QHostAddress();
}

QString NetworkManager::getActiveInterface() {
    for (const auto &i : QNetworkInterface::allInterfaces()) {
        if (i.flags().testFlag(QNetworkInterface::IsUp) &&
            !i.flags().testFlag(QNetworkInterface::IsLoopBack)) {
            for (const auto &e : i.addressEntries())
                if (e.ip().protocol() == QAbstractSocket::IPv4Protocol)
                    return i.name();
        }
    }
    return "";
}

QMap<QString, QString> NetworkManager::readDHCPLeases() {
    QMap<QString, QString> map;
    for (const auto &path : { "/var/lib/misc/dnsmasq.leases",
                               "/var/lib/dhcp/dhcpd.leases",
                               "/var/lib/dhcpd/dhcpd.leases" }) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QTextStream in(&f);
        while (!in.atEnd()) {
            QStringList p = in.readLine().trimmed().split(' ');
            if (p.size() >= 4 && p[3] != "*") map[p[2]] = p[3];
        }
        break;
    }
    return map;
}


RouterInfo NetworkManager::getRouterInfo() const {
    if (m_routerDetector) return m_routerDetector->lastInfo();
    return RouterInfo{};
}

void NetworkManager::triggerRouterDetection(bool force) {
    if (!m_routerDetector || m_gatewayIp.isEmpty()) return;

    // Fail-safe: If the MAC isn't resolved yet due to a race condition with the passive sniffer,
    // fetch it directly from the kernel ARP cache before running detection.
    if (m_gatewayMac.isEmpty()) {
        QFile f("/proc/net/arp");
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&f);
            in.readLine(); // skip header
            while (!in.atEnd()) {
                QStringList parts = in.readLine().trimmed().split(QRegularExpression("\\s+"));
                if (parts.size() >= 4 && parts[0] == m_gatewayIp) {
                    if (parts[3] != "00:00:00:00:00:00") {
                        m_gatewayMac = parts[3];
                    }
                    break;
                }
            }
        }
    }

    // Throttle: don't re-probe within 60 s of the last probe unless forced
    static QDateTime lastProbe;
    if (!force && lastProbe.isValid() && lastProbe.secsTo(QDateTime::currentDateTime()) < 60) return;
    lastProbe = QDateTime::currentDateTime();

    QString gwIp  = m_gatewayIp;
    QString gwMac = m_gatewayMac;
    QMetaObject::invokeMethod(m_routerDetector, [this, gwIp, gwMac]() {
        m_routerDetector->detect(gwIp, gwMac);
    }, Qt::QueuedConnection);
}

} // namespace core
