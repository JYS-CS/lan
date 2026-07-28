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

    // Bandwidth
    QString upBandwidth() const { return m_upBandwidth; }
    void setUpBandwidth(const QString &bw) { m_upBandwidth = bw; }
    
    QString downBandwidth() const { return m_downBandwidth; }
    void setDownBandwidth(const QString &bw) { m_downBandwidth = bw; }

    // Status
    QString status() const { return m_status; }
    void setStatus(const QString &status) { m_status = status; }

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
    QString m_hostname = "Unknown";
    QString m_upBandwidth = "0 KB/s";
    QString m_downBandwidth = "0 KB/s";
    QString m_status = "Online";
    QString m_vendor = "Unknown Vendor";
    QString m_alias;
    bool m_isKnown = false;
    bool m_useBlockPage = false;
    QDateTime m_lastSeen = QDateTime::currentDateTime();
};

} // namespace core
