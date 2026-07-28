#pragma once

#include <QObject>
#include <QMap>
#include <QString>
#include <QTimer>
#include <mutex>
#include "Types.h"

namespace core {

class TrafficMonitor : public QObject {
    Q_OBJECT

public:
    explicit TrafficMonitor(QObject *parent = nullptr);
    virtual ~TrafficMonitor() = default;

public slots:
    void processPacket(const unsigned char* pkt, int len);
    void resetStats();
    void setLocalNetwork(quint32 ip, quint32 mask);
    void setHostIdentity(const QString &mac, quint32 ip);

signals:
    void trafficUpdated(const QMap<QString, core::TrafficStats> &stats);
    void globalStats(int packetCount, double packetsPerSecond, quint64 totalIn, quint64 totalOut);

private slots:
    void calculateRates();

private:
    std::mutex m_mutex;
    QMap<QString, core::TrafficStats> m_deviceStats;
    
    int m_packetCount = 0;
    int m_lastPacketCount = 0;
    QTimer *m_statsTimer;

    quint32 m_localIp = 0;
    quint32 m_localMask = 0;
    
    QString m_hostMac;
    quint32 m_hostIp = 0;
    
    quint64 m_totalInbound = 0;
    quint64 m_totalOutbound = 0;

    bool isLocal(quint32 ip) const {
        if (m_localMask == 0) return false;
        return (ip & m_localMask) == (m_localIp & m_localMask);
    }
};

} // namespace core
