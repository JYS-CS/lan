#pragma once

#include <QString>
#include <QDateTime>
#include <QMap>

namespace core {

// Traffic metrics per device
struct TrafficStats {
    quint64 totalBytesUp = 0;
    quint64 totalBytesDown = 0;
    quint32 currentRateUp = 0;   // bytes/sec
    quint32 currentRateDown = 0;
    
    // Protocol breakdown (Port -> Total Bytes)
    QMap<int, quint64> protocolBytes;
    
    // Internal counters for rate calculation
    quint64 lastSnapUp = 0;
    quint64 lastSnapDown = 0;
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
