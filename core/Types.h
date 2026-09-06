#pragma once

#include <QString>
#include <QDateTime>
#include <QMap>

namespace core {

// Traffic metrics per device (keyed by IP inside TrafficMonitor, then
// joined to MAC in BandwidthEngine for stable identity)
struct TrafficStats {
    quint64 totalBytesUp   = 0;
    quint64 totalBytesDown = 0;
    quint32 currentRateUp  = 0;   // bytes/sec
    quint32 currentRateDown= 0;
    quint32 peakRateUp     = 0;   // session high-water mark
    quint32 peakRateDown   = 0;

    // Protocol breakdown (Port -> Total Bytes)
    QMap<int, quint64> protocolBytes;

    // Internal counters for rate calculation
    quint64 lastSnapUp   = 0;
    quint64 lastSnapDown = 0;
};

// Per-device bandwidth snapshot emitted by BandwidthEngine (MAC-keyed)
struct DeviceBandwidth {
    QString mac;
    QString ip;
    QString displayName;    // alias > hostname > ip
    QString vendor;
    bool    online     = false;

    quint32 rxRate     = 0;   // bytes/sec current download
    quint32 txRate     = 0;   // bytes/sec current upload
    quint32 peakRx     = 0;   // session peak download
    quint32 peakTx     = 0;   // session peak upload
    quint64 rxTotal    = 0;   // session total bytes downloaded
    quint64 txTotal    = 0;   // session total bytes uploaded

    QDateTime lastSeen;
};

// Describes whether this host can see per-device WAN traffic
enum class TopologyCapability {
    FullGateway,    // ip_forward=1, default route is on LAN iface → we ARE the gateway
    PartialSniffer, // promiscuous capture, but not in the forwarding path
    Unsupported     // can only see own traffic (switched network, wrong interface)
};

// Aggregated bandwidth sample — written to the database for history
struct BwSample {
    QString  mac;
    qint64   timestamp = 0;   // Unix epoch seconds
    quint64  rxBytes   = 0;
    quint64  txBytes   = 0;
    quint32  rxRate    = 0;
    quint32  txRate    = 0;
};

// IP address history entry (tracks DHCP reassignments)
struct IpHistoryEntry {
    QString mac;
    QString ip;
    QDateTime firstSeen;
    QDateTime lastSeen;
};

// Global network events (Discovery, Security, etc.)
struct NetworkEvent {
    enum Type { Info, Discovery, Security, Warning };
    QDateTime timestamp;
    Type type;
    QString message;
    QString sourceIp;
};

// Port scanning results
struct PortResult {
    int port;
    bool isOpen;
    QString service;
};

} // namespace core
