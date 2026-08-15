#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QMutex>

namespace core {

// ─────────────────────────────────────────────────────────────────────────────
//  RouterCapabilities — boolean feature map
// ─────────────────────────────────────────────────────────────────────────────
struct RouterCapabilities {
    // Network-probed capabilities
    bool hasSSH       = false;  // TCP 22 open
    bool hasTelnet    = false;  // TCP 23 open (legacy)
    bool hasWebUI     = false;  // TCP 80/443/8080/8443 open
    bool hasSNMP      = false;  // UDP 161 responded
    bool hasUPnP      = false;  // SSDP/UPnP responded
    bool isEnterprise = false;  // rule-based classification

    // Model-DB capabilities
    bool hasIPv6      = false;
    bool hasGuestWifi = false;
    bool hasVPN       = false;
    bool hasQoS       = false;
    bool hasDualBand  = false;
    bool hasTriBand   = false;
    bool hasWPA3      = false;

    // Security posture (from model DB)
    bool    defaultCredsRisk = false;  // documented history of unchanged defaults
    QString firmwareRisk     = "unknown"; // low | medium | high | unknown
};

// ─────────────────────────────────────────────────────────────────────────────
//  RouterClass
// ─────────────────────────────────────────────────────────────────────────────
enum class RouterClass {
    Unknown,
    Home,
    SMB,
    Enterprise,
    ISPCPE,        // Carrier/ISP-supplied CPE gateway
    MobileHotspot  // Phone tethering / portable hotspot
};

inline QString routerClassString(RouterClass c) {
    switch (c) {
        case RouterClass::Home:          return "Home";
        case RouterClass::SMB:           return "SMB / Prosumer";
        case RouterClass::Enterprise:    return "Enterprise";
        case RouterClass::ISPCPE:        return "ISP CPE";
        case RouterClass::MobileHotspot: return "Mobile Hotspot";
        default:                         return "Unknown";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  RouterInfo — complete result set emitted after detection
// ─────────────────────────────────────────────────────────────────────────────
struct RouterInfo {
    // Gateway identity (inputs)
    QString gatewayIp;
    QString gatewayMac;

    // Resolved identity
    QString manufacturer;     // e.g. "TP-Link"
    QString model;            // e.g. "Archer AX3000"
    QString firmware;         // from HTTP banner / SNMP sysDescr
    QString friendlyName;     // from UPnP <friendlyName>
    QString systemName;       // from SNMP sysName
    QString systemLocation;   // from SNMP sysLocation
    RouterClass routerClass = RouterClass::Unknown;
    int     matchPriority   = -1;  // priority of the winning model-DB entry
    QString matchedModelNotes;     // DB "notes" field of the winning entry

    // MAC flags
    bool    isRandomizedMac = false; // locally-administered bit set (phone hotspot / VM)
    QString hotspotType;             // "Android", "iPhone", "Windows" if detected

    // Open ports list
    QStringList openPorts;

    // Raw probe strings (shown in accordion)
    QString httpBanner;     // full HTTP response headers from best port
    QString ssdpResponse;   // raw SSDP M-SEARCH reply
    QString upnpXml;        // trimmed UPnP device descriptor XML
    QString snmpSysDescr;   // SNMP sysDescr.0 value
    QString mdnsInfo;       // mDNS PTR response summary

    // Feature capabilities
    RouterCapabilities caps;

    QDateTime lastScanned;
    bool isValid = false;
};

// ─────────────────────────────────────────────────────────────────────────────
//  RouterDetector — runs all probe layers against the gateway
// ─────────────────────────────────────────────────────────────────────────────
class RouterDetector : public QObject {
    Q_OBJECT
public:
    explicit RouterDetector(QObject *parent = nullptr);

    RouterInfo lastInfo() const;

public slots:
    /// Kick off a full detection pass (runs on caller's thread — move to QThread if needed)
    void detect(const QString &gatewayIp, const QString &gatewayMac);

    /// Reload router_models.json from disk (called after user edits)
    void reloadModelDatabase();

signals:
    void routerInfoReady(const core::RouterInfo &info);
    void detectionStage(const QString &msg);

private:
    // Probe layers — each mutates `info` in-place
    void probeHTTP    (RouterInfo &info);  // TCP 80/8080/443/8443 — Server header + HTML title
    void probeSSDPUPnP(RouterInfo &info);  // UDP M-SEARCH → fetch Location XML
    void probeMDNS    (RouterInfo &info);  // mDNS PTR query
    void probeSNMP    (RouterInfo &info);  // SNMP v1/v2c GET sysDescr/sysName/sysLocation
    void probePorts   (RouterInfo &info);  // TCP port scan → caps
    void probeMobileHotspot(RouterInfo &info); // MAC + IP heuristics → phone tethering

    // Classification
    void classifyFromModels(RouterInfo &info);
    void inferCapabilities (RouterInfo &info);

    // SNMP helpers
    QByteArray buildSnmpGetRequest(const QByteArray &community, int requestId,
                                   const QList<QByteArray> &oids) const;
    QString parseSnmpString(const QByteArray &response, int oidIndex) const;

    // UPnP XML fetch
    QString fetchUrl(const QString &host, int port, const QString &path,
                     bool useTls = false, int timeoutMs = 2000) const;
    QString fetchUrlTls(const QString &host, int port, const QString &path,
                        int timeoutMs) const;
    void parseUpnpXml(const QString &xml, RouterInfo &info) const;

    // Utilities
    bool    tcpConnect(const QString &ip, int port, int timeoutMs = 400) const;
    bool    isMacLocallyAdministered(const QString &mac) const;

    QString         m_modelDbPath;
    QJsonArray      m_modelDb;
    RouterInfo      m_lastInfo;
    mutable QMutex  m_mutex;  // protects m_lastInfo
};

} // namespace core
