#pragma once

#include <QString>
#include <QDateTime>

namespace core {

class Device {
public:
    Device() = default;
    
    // IP Address
    QString ip() const { return m_ip; }
    void setIp(const QString &ip) { m_ip = ip; }

    // MAC Address
    QString mac() const { return m_mac; }
    void setMac(const QString &mac) { m_mac = mac; }

    // Hostname
    QString hostname() const { return m_hostname; }
    void setHostname(const QString &hostname) { m_hostname = hostname; }

    // Bandwidth — string representation (kept for display delegate compatibility)
    QString upBandwidth() const { return m_upBandwidth; }
    void setUpBandwidth(const QString &bw) { m_upBandwidth = bw; }
    
    QString downBandwidth() const { return m_downBandwidth; }
    void setDownBandwidth(const QString &bw) { m_downBandwidth = bw; }

    // Numeric bandwidth fields (set by BandwidthEngine)
    quint32 rxRate()     const { return m_rxRate; }
    quint32 txRate()     const { return m_txRate; }
    quint32 peakRx()     const { return m_peakRx; }
    quint32 peakTx()     const { return m_peakTx; }
    quint64 rxTotal()    const { return m_rxTotal; }
    quint64 txTotal()    const { return m_txTotal; }

    void setRxRate(quint32 v)  { m_rxRate  = v; }
    void setTxRate(quint32 v)  { m_txRate  = v; }
    void setPeakRx(quint32 v)  { m_peakRx  = v; }
    void setPeakTx(quint32 v)  { m_peakTx  = v; }
    void setRxTotal(quint64 v) { m_rxTotal = v; }
    void setTxTotal(quint64 v) { m_txTotal = v; }

    // Status
    QString status() const { return m_status; }
    void setStatus(const QString &status) { m_status = status; }

    // Latency — numeric ms (for sorting + ping results) + display string
    quint32 latencyMs() const { return m_latencyMs; }
    void setLatencyMs(quint32 ms) {
        m_latencyMs = ms;
        if (ms == 0 && m_latency == "-") return; // Not yet pinged
        m_latency = (ms == 0) ? "0 ms" : QString("%1 ms").arg(ms);
    }
    QString latency() const { return m_latency; }
    void setLatency(const QString &latency) { m_latency = latency; }

    // Device Type — inferred from DHCP/vendor/hostname heuristics
    QString deviceType() const { return m_deviceType; }
    void setDeviceType(const QString &type) { m_deviceType = type; }

    // Vendor
    QString vendor() const { return m_vendor; }
    void setVendor(const QString &vendor) { m_vendor = vendor; }

    // Last Seen
    QDateTime lastSeen() const { return m_lastSeen; }
    void setLastSeen(const QDateTime &dt) { m_lastSeen = dt; }

    // Alias / Display Name
    QString alias() const { return m_alias; }
    void setAlias(const QString &alias) { m_alias = alias; }

    // Persistence flag
    bool isKnown() const { return m_isKnown; }
    void setIsKnown(bool known) { m_isKnown = known; }

    bool useBlockPage() const { return m_useBlockPage; }
    void setUseBlockPage(bool use) { m_useBlockPage = use; }

private:
    QString m_ip;
    QString m_mac;
    QString m_hostname    = "Unknown";
    QString m_upBandwidth = "0 KB/s";
    QString m_downBandwidth = "0 KB/s";
    QString m_status      = "Online";
    QString m_latency     = "-";
    quint32 m_latencyMs   = 9999; // 9999 = unprobed sentinel
    QString m_deviceType; // empty = not yet classified
    QString m_vendor      = "Unknown Vendor";
    QString m_alias;
    bool    m_isKnown     = false;
    bool    m_useBlockPage= false;
    QDateTime m_lastSeen  = QDateTime::currentDateTime();

    // Numeric bandwidth (populated by BandwidthEngine, not persisted in DB)
    quint32 m_rxRate  = 0;
    quint32 m_txRate  = 0;
    quint32 m_peakRx  = 0;
    quint32 m_peakTx  = 0;
    quint64 m_rxTotal = 0;
    quint64 m_txTotal = 0;
};

} // namespace core
