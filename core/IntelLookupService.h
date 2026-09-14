#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QNetworkAccessManager>
#include "ThreatIntelManager.h"

namespace core {

// IP & Domain Intelligence — given a domain or an IP, gathers:
//   - resolved IP(s) (if given a domain)
//   - subdomains, via certificate transparency logs (crt.sh) — the same
//     free, non-intrusive technique real recon tools use; no brute-forcing
//   - reverse DNS
//   - ASN / geolocation / ISP (ip-api.com, free tier, no key needed)
//   - open ports (lightweight TCP-connect scan over common ports)
//   - whether the IP is on our own threat-intel blocklist
//
// Lives on its own dedicated thread — mixes blocking calls (reverse DNS,
// port scan) with async HTTP (geo lookup, crt.sh), same pattern as
// RouterDetector/VulnerabilityScanner.
class IntelLookupService : public QObject {
    Q_OBJECT
public:
    // threatIntel is not owned; only used for isKnownMalicious() lookups.
    explicit IntelLookupService(ThreatIntelManager *threatIntel, QObject *parent = nullptr);

public slots:
    void lookup(const QString &target);

signals:
    void stageChanged(const QString &stage);
    void resolvedIps(const QStringList &ips);
    void reverseDnsResult(const QString &hostname);
    void geoResult(const QString &country, const QString &city, const QString &isp, const QString &asn, const QString &org);
    void geoLookupFailed(const QString &error);
    void subdomainsFound(const QStringList &subdomains);
    void portsFound(const QList<int> &openPorts);
    void threatStatus(bool malicious);
    void lookupFinished();
    void lookupFailed(const QString &error);

private slots:
    void onGeoReplyFinished();
    void onCrtShReplyFinished();

private:
    static bool looksLikeIp(const QString &s);
    QString reverseDnsLookup(const QString &ip) const;
    QList<int> scanCommonPorts(const QString &ip) const;

    ThreatIntelManager *m_threatIntel;
    QNetworkAccessManager *m_nam;
    QString m_primaryIp;
    QString m_domainForSubdomainSearch;
};

} // namespace core
