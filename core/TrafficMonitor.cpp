#include "TrafficMonitor.h"
#include <QDebug>
#include <QHostAddress>

// Linux networking
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <net/ethernet.h>
#include <arpa/inet.h>

namespace core {

TrafficMonitor::TrafficMonitor(QObject *parent) : QObject(parent) {
    m_statsTimer = new QTimer(this);
    connect(m_statsTimer, &QTimer::timeout, this, &TrafficMonitor::calculateRates);
    m_statsTimer->start(1000); // 1Hz update (every 1 second) to prevent UI blinking
    m_elapsedTimer.start();
}

void TrafficMonitor::setHostIdentity(const QString &mac, quint32 ip) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_hostMac == mac.toLower() && m_hostIp == ip) return; // no change
    m_hostMac = mac.toLower();
    m_hostIp = ip;
    qDebug() << "TrafficMonitor: Host Identity set to" << m_hostMac << "|" << QHostAddress(ip).toString();
}

void TrafficMonitor::processPacket(const unsigned char* pkt, int len) {
    if (len < (int)sizeof(struct ether_header)) return;

    m_packetCount++;

    struct ether_header *eth = (struct ether_header *)pkt;
    if (ntohs(eth->ether_type) != ETHERTYPE_IP) return;

    struct iphdr *ip = (struct iphdr *)(pkt + sizeof(struct ether_header));
    if (len < (int)(sizeof(struct ether_header) + (ip->ihl * 4))) return;

    // Parse MACs
    auto macToString = [](const unsigned char* m) {
        return QString("%1:%2:%3:%4:%5:%6")
            .arg(m[0], 2, 16, QChar('0')).arg(m[1], 2, 16, QChar('0'))
            .arg(m[2], 2, 16, QChar('0')).arg(m[3], 2, 16, QChar('0'))
            .arg(m[4], 2, 16, QChar('0')).arg(m[5], 2, 16, QChar('0')).toLower();
    };
    QString srcMac = macToString(eth->ether_shost);
    QString dstMac = macToString(eth->ether_dhost);

    uint32_t saddr = ntohl(ip->saddr);
    uint32_t daddr = ntohl(ip->daddr);

    char srcIpBuf[INET_ADDRSTRLEN];
    char dstIpBuf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(ip->saddr), srcIpBuf, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &(ip->daddr), dstIpBuf, INET_ADDRSTRLEN);

    QString srcIP = QString::fromLatin1(srcIpBuf);
    QString dstIP = QString::fromLatin1(dstIpBuf);
    quint32 size = len;

    // --- ACCURATE ATTRIBUTION LOGIC ---
    // In Gateway Mode, we see packets entering and leaving.
    // 1. Packet from Client entering Laptop: (srcMAC=Client, dstMAC=Laptop, srcIP=Client)
    // 2. Packet from Laptop leaving for Client: (srcMAC=Laptop, dstMAC=Client, dstIP=Client)
    
    bool isSrcLocal = isLocal(saddr);
    bool isDstLocal = isLocal(daddr);

    // Enter critical section ONLY for updating stats dictionary
    std::lock_guard<std::mutex> lock(m_mutex);

    // [UPLOAD] Client -> Laptop
    if (dstMac == m_hostMac && isSrcLocal && saddr != m_hostIp) {
        m_deviceStats[srcIP].totalBytesUp += size;
        m_totalOutbound += size; // Global export
    }
    // [DOWNLOAD] Laptop -> Client
    else if (srcMac == m_hostMac && isDstLocal && daddr != m_hostIp) {
        m_deviceStats[dstIP].totalBytesDown += size;
        m_totalInbound += size; // Global import
    }
    // [HOST OWN TRAFFIC] If laptop is talker (not forward)
    else if (saddr == m_hostIp) {
        m_deviceStats[srcIP].totalBytesUp += size;
        m_totalOutbound += size;
    }
    else if (daddr == m_hostIp) {
        m_deviceStats[dstIP].totalBytesDown += size;
        m_totalInbound += size;
    }

    // [CLEANUP] Remove Ghost entries (any map entry not in local network)
    // Actually, we just don't add them above anymore.
}


void TrafficMonitor::setLocalNetwork(quint32 ip, quint32 mask) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_localIp == ip && m_localMask == mask) return; // no change
    m_localIp = ip;
    m_localMask = mask;
    qDebug() << "TrafficMonitor: Network set to" << QHostAddress(ip).toString() << "/" << QHostAddress(mask).toString();
}

void TrafficMonitor::calculateRates() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    qint64 elapsedMs = m_elapsedTimer.restart();
    if (elapsedMs == 0) elapsedMs = 1; // Prevent division by zero mathematically
    double multiplier = 1000.0 / static_cast<double>(elapsedMs);

    for (auto it = m_deviceStats.begin(); it != m_deviceStats.end(); ++it) {
        TrafficStats &stats = it.value();
        
        // Dynamically scale delta math to normalize strictly over 1 real-world second
        stats.currentRateUp = static_cast<quint64>((stats.totalBytesUp - stats.lastSnapUp) * multiplier);
        stats.currentRateDown = static_cast<quint64>((stats.totalBytesDown - stats.lastSnapDown) * multiplier);
        
        stats.lastSnapUp = stats.totalBytesUp;
        stats.lastSnapDown = stats.totalBytesDown;
    }
    
    emit trafficUpdated(m_deviceStats);
    
    int currentBatch = m_packetCount.load() - m_lastPacketCount;
    double pps = (currentBatch * 1000.0) / static_cast<double>(elapsedMs);
    
    emit globalStats(m_packetCount.load(), pps, m_totalInbound, m_totalOutbound);
    m_lastPacketCount = m_packetCount.load();
}

void TrafficMonitor::resetStats() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_deviceStats.clear();
    m_packetCount = 0;
    m_lastPacketCount = 0;
    m_totalInbound = 0;
    m_totalOutbound = 0;
}

} // namespace core
