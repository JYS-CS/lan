#include "IntelLookupService.h"
#include <QHostInfo>
#include <QHostAddress>
#include <QTcpSocket>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

namespace core {

IntelLookupService::IntelLookupService(ThreatIntelManager *threatIntel, QObject *parent)
    : QObject(parent), m_threatIntel(threatIntel)
{
    m_nam = new QNetworkAccessManager(this);
}

bool IntelLookupService::looksLikeIp(const QString &s) {
    static const QRegularExpression ipRe("^\\d{1,3}(\\.\\d{1,3}){3}$");
    return ipRe.match(s.trimmed()).hasMatch();
}

QString IntelLookupService::reverseDnsLookup(const QString &ip) const {
    struct sockaddr_in sa{};
    sa.sin_family = AF_INET;
    inet_pton(AF_INET, ip.toLatin1().constData(), &sa.sin_addr);

    char host[NI_MAXHOST] = {};
    // NI_NAMEREQD would fail instead of falling back to the numeric form;
    // we want silence (empty string) on no-PTR, not an error path.
    int rc = getnameinfo(reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa),
                          host, sizeof(host), nullptr, 0, NI_NAMEREQD);
    if (rc != 0) return {};
    return QString::fromLatin1(host);
}

QList<int> IntelLookupService::scanCommonPorts(const QString &ip) const {
    static const QList<int> commonPorts = {
        21, 22, 23, 25, 53, 80, 110, 143, 443, 465, 587, 993, 995,
        3306, 3389, 5432, 5900, 8080, 8443
    };
    QList<int> open;
    for (int port : commonPorts) {
        QTcpSocket sock;
        sock.connectToHost(ip, (quint16)port);
        if (sock.waitForConnected(400)) open << port;
    }
    return open;
}

void IntelLookupService::lookup(const QString &target) {
    QString t = target.trimmed();
    if (t.isEmpty()) {
        emit lookupFailed("Enter a domain or IP address.");
        return;
    }

    bool isIp = looksLikeIp(t);
    m_domainForSubdomainSearch.clear();

    if (isIp) {
        m_primaryIp = t;
        emit resolvedIps({t});
        emit subdomainsFound({}); // N/A for a bare IP lookup
    } else {
        emit stageChanged("Resolving " + t + "…");
        QHostInfo info = QHostInfo::fromName(t);
        if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
            emit lookupFailed("Could not resolve " + t + ": " + info.errorString());
            return;
        }
        QStringList ips;
        for (const auto &addr : info.addresses()) {
            if (addr.protocol() == QAbstractSocket::IPv4Protocol) ips << addr.toString();
        }
        if (ips.isEmpty()) {
            emit lookupFailed("No IPv4 address found for " + t);
            return;
        }
        m_primaryIp = ips.first();
        m_domainForSubdomainSearch = t;
        emit resolvedIps(ips);

        emit stageChanged("Searching certificate transparency logs for subdomains…");
        QUrl crtUrl(QString("https://crt.sh/?q=%25.%1&output=json").arg(t));
        QNetworkRequest req(crtUrl);
        req.setTransferTimeout(15000);
        QNetworkReply *reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, &IntelLookupService::onCrtShReplyFinished);
    }

    emit stageChanged("Reverse DNS lookup…");
    QString ptr = reverseDnsLookup(m_primaryIp);
    emit reverseDnsResult(ptr);

    emit stageChanged("Checking threat intelligence…");
    bool malicious = m_threatIntel && m_threatIntel->isKnownMalicious(m_primaryIp);
    emit threatStatus(malicious);

    emit stageChanged("Scanning common ports…");
    QList<int> ports = scanCommonPorts(m_primaryIp);
    emit portsFound(ports);

    emit stageChanged("Looking up ASN / geolocation…");
    QUrl geoUrl(QString("http://ip-api.com/json/%1?fields=status,message,country,city,isp,org,as").arg(m_primaryIp));
    QNetworkRequest geoReq(geoUrl);
    geoReq.setTransferTimeout(10000);
    QNetworkReply *geoReply = m_nam->get(geoReq);
    connect(geoReply, &QNetworkReply::finished, this, &IntelLookupService::onGeoReplyFinished);

    if (isIp) emit lookupFinished(); // no crt.sh call pending; geo finishing will still fire its own signal
}

void IntelLookupService::onGeoReplyFinished() {
    auto *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit geoLookupFailed(reply->errorString());
        return;
    }
    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject obj = doc.object();
    if (obj.value("status").toString() != "success") {
        emit geoLookupFailed(obj.value("message").toString("lookup failed"));
        return;
    }
    emit geoResult(obj.value("country").toString(), obj.value("city").toString(),
                    obj.value("isp").toString(), obj.value("as").toString(), obj.value("org").toString());
}

void IntelLookupService::onCrtShReplyFinished() {
    auto *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();

    QSet<QString> unique;
    if (reply->error() == QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isArray()) {
            for (const QJsonValue &v : doc.array()) {
                QString nameValue = v.toObject().value("name_value").toString();
                for (const QString &name : nameValue.split('\n', Qt::SkipEmptyParts)) {
                    QString n = name.trimmed().toLower();
                    if (!n.isEmpty() && !n.startsWith('*')) unique.insert(n);
                }
            }
        }
    }
    QStringList sorted = unique.values();
    sorted.sort();
    emit subdomainsFound(sorted);
    emit lookupFinished();
}

} // namespace core
