// RouterDetector.cpp
#include "RouterDetector.h"
#include "NetworkManager.h"

#include <QMutexLocker>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QCoreApplication>
#include <QDebug>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QSslSocket>
// Networking
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>

namespace core {

// ─── OID constants (BER-encoded, no length prefix) ─────────────────────────
static const QByteArray OID_SYSDESCR   = QByteArray("\x2b\x06\x01\x02\x01\x01\x01\x00", 8);
static const QByteArray OID_SYSNAME    = QByteArray("\x2b\x06\x01\x02\x01\x01\x05\x00", 8);
static const QByteArray OID_SYSLOCATION= QByteArray("\x2b\x06\x01\x02\x01\x01\x06\x00", 8);

// ─── BER helpers ────────────────────────────────────────────────────────────
static QByteArray berLen(int len) {
    if (len < 128) return QByteArray(1, (char)len);
    QByteArray r;
    r += (char)0x81;
    r += (char)(len & 0xff);
    return r;
}
static QByteArray berInt(int v) {
    QByteArray r;
    r += (char)0x02;
    if (v < 128) { r += (char)1; r += (char)v; }
    else          { r += (char)2; r += (char)((v>>8)&0xff); r += (char)(v&0xff); }
    return r;
}
static QByteArray berOctet(const QByteArray &s) {
    QByteArray r;
    r += (char)0x04;
    r += berLen(s.size());
    r += s;
    return r;
}
static QByteArray berOid(const QByteArray &oid) {
    QByteArray r;
    r += (char)0x06;
    r += (char)oid.size();
    r += oid;
    return r;
}
static QByteArray berSeq(char tag, const QByteArray &inner) {
    QByteArray r;
    r += tag;
    r += berLen(inner.size());
    r += inner;
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
RouterDetector::RouterDetector(QObject *parent) : QObject(parent) {
    // Find router_models.json next to the executable
    m_modelDbPath = QCoreApplication::applicationDirPath() + "/data/router_models.json";
    reloadModelDatabase();
}

RouterInfo RouterDetector::lastInfo() const {
    QMutexLocker lk(&m_mutex);
    return m_lastInfo;
}

void RouterDetector::reloadModelDatabase() {
    QFile f(m_modelDbPath);
    if (!f.open(QIODevice::ReadOnly)) {
        qDebug() << "[RouterDetector] model DB not found at" << m_modelDbPath;
        return;
    }
    auto doc = QJsonDocument::fromJson(f.readAll());
    m_modelDb = doc.object().value("models").toArray();
    qDebug() << "[RouterDetector] Loaded" << m_modelDb.size() << "model entries";
}

// ─────────────────────────────────────────────────────────────────────────────
//  Main entry point
// ─────────────────────────────────────────────────────────────────────────────
void RouterDetector::detect(const QString &gwIp, const QString &gwMac) {
    if (gwIp.isEmpty()) return;

    RouterInfo info;
    info.gatewayIp  = gwIp;
    info.gatewayMac = gwMac;
    info.lastScanned = QDateTime::currentDateTime();

    // Layer 0: MAC + IP heuristics — fastest, no network I/O
    emit detectionStage("Checking MAC / IP fingerprint…");
    probeMobileHotspot(info);

    emit detectionStage("Probing HTTP banner…");
    probeHTTP(info);

    emit detectionStage("Probing SSDP/UPnP…");
    probeSSDPUPnP(info);

    emit detectionStage("Probing mDNS…");
    probeMDNS(info);

    emit detectionStage("Probing SNMP…");
    probeSNMP(info);

    emit detectionStage("Scanning ports…");
    probePorts(info);

    emit detectionStage("Classifying…");
    classifyFromModels(info);
    inferCapabilities(info);

    info.isValid = true;
    emit detectionStage("Done");

    {
        QMutexLocker lk(&m_mutex);
        m_lastInfo = info;
    }
    emit routerInfoReady(info);
}

// ─────────────────────────────────────────────────────────────────────────────
//  MAC locally-administered-bit check
//  The second-least-significant bit of the first octet is 1 for locally-
//  administered (often randomized) MACs.  Android 12+ randomizes hotspot MACs.
// ─────────────────────────────────────────────────────────────────────────────
bool RouterDetector::isMacLocallyAdministered(const QString &mac) const {
    if (mac.isEmpty()) return false;
    // Accept "AA:BB:...", "aa-bb-...", "aabb..."
    QString clean = mac.toUpper().remove(':').remove('-').remove('.');
    if (clean.size() < 2) return false;
    bool ok = false;
    int firstByte = clean.left(2).toInt(&ok, 16);
    return ok && (firstByte & 0x02); // bit 1 set → locally administered
}

// ─────────────────────────────────────────────────────────────────────────────
//  Layer 0: Mobile Hotspot / Phone Tethering Heuristics
//  Called BEFORE any network probes.  Reasons:
//  1. Phone hotspots almost never respond to SNMP/UPnP/mDNS probes.
//  2. Android 12+ randomises the hotspot MAC (locally-administered bit = 1).
//  3. OS-specific default subnets give a strong secondary signal.
// ─────────────────────────────────────────────────────────────────────────────
void RouterDetector::probeMobileHotspot(RouterInfo &info) {
    info.isRandomizedMac = isMacLocallyAdministered(info.gatewayMac);

    // IP-based platform heuristics
    QHostAddress gwAddr(info.gatewayIp);

    // iPhone Personal Hotspot: always 172.20.10.1 with /28
    if (info.gatewayIp == "172.20.10.1") {
        info.routerClass   = RouterClass::MobileHotspot;
        info.hotspotType   = "iPhone";
        info.manufacturer  = "Apple";
        info.model         = "iPhone Personal Hotspot";
        info.matchedModelNotes =
            "iPhone hotspot always uses 172.20.10.1 / 172.20.10.0/28. "
            "No admin UI accessible from tethered clients. "
            "Shared internet from cellular data connection.";
        info.caps.hasDualBand = true;   // iPhone 12+ is dual-band hotspot
        info.caps.hasIPv6     = true;
        info.caps.hasWPA3     = true;   // iOS 15+ supports WPA3 Personal
        return;
    }

    // Android hotspot: classic 192.168.43.1, or any 10.x.x.x with randomised MAC
    bool isAndroidClassic = info.gatewayIp.startsWith("192.168.43.");
    bool isAndroid10x     = info.isRandomizedMac && info.gatewayIp.startsWith("10.");
    if (isAndroidClassic || isAndroid10x) {
        info.routerClass   = RouterClass::MobileHotspot;
        info.hotspotType   = "Android";
        info.manufacturer  = "Android Device";
        info.model         = isAndroidClassic ? "Android Hotspot (192.168.43.x)"
                                               : "Android Hotspot (randomised MAC)";
        info.matchedModelNotes =
            "Android tethering gateway. MAC is locally-administered (randomised on Android 12+). "
            "No SNMP/UPnP/admin-panel accessible from tethered clients. "
            "Shared internet from cellular data connection.";
        info.caps.hasDualBand = true;   // most modern Android phones
        info.caps.hasIPv6     = true;   // Android propagates IPv6 via CLAT/464XLAT
        return;
    }

    // Windows Mobile Hotspot: usually 192.168.137.1
    if (info.gatewayIp == "192.168.137.1") {
        info.routerClass   = RouterClass::MobileHotspot;
        info.hotspotType   = "Windows";
        info.manufacturer  = "Microsoft";
        info.model         = "Windows Mobile Hotspot (ICS)";
        info.matchedModelNotes =
            "Windows Internet Connection Sharing / Mobile Hotspot. "
            "Gateway is always 192.168.137.1. "
            "No router admin panel; controlled via Windows Settings.";
        return;
    }

    // macOS Internet Sharing: typically 192.168.2.1
    if (info.gatewayIp == "192.168.2.1" && info.isRandomizedMac) {
        info.routerClass   = RouterClass::MobileHotspot;
        info.hotspotType   = "macOS";
        info.manufacturer  = "Apple";
        info.model         = "macOS Internet Sharing";
        info.matchedModelNotes =
            "macOS Internet Sharing uses 192.168.2.1 by default. "
            "No router admin panel; controlled via System Settings \u2192 Sharing.";
        return;
    }

    // Generic: any locally-administered MAC on non-standard subnet with no probes yet
    if (info.isRandomizedMac) {
        // Don't commit to MobileHotspot class yet — could be a VM/container gateway
        // Just flag it and let the later probes confirm or deny
        info.matchedModelNotes =
            "MAC is locally-administered (bit 1 of first octet = 1). "
            "This typically means a phone hotspot (Android 12+ or iPhone) or a virtualised "
            "network interface (VM bridge, Docker, etc.). "
            "If all probes return empty, treat as a mobile hotspot.";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  TCP connect helper (non-blocking with timeout)
// ─────────────────────────────────────────────────────────────────────────────
bool RouterDetector::tcpConnect(const QString &ip, int port, int timeoutMs) const {
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return false;

    // Set non-blocking
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    inet_pton(AF_INET, ip.toLatin1().constData(), &addr.sin_addr);

    int r = ::connect(s, (struct sockaddr*)&addr, sizeof(addr));
    bool connected = false;
    if (r == 0) {
        connected = true;
    } else if (errno == EINPROGRESS) {
        fd_set wset;
        FD_ZERO(&wset);
        FD_SET(s, &wset);
        struct timeval tv{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
        if (::select(s + 1, nullptr, &wset, nullptr, &tv) > 0) {
            int err = 0; socklen_t len = sizeof(err);
            getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len);
            connected = (err == 0);
        }
    }
    ::close(s);
    return connected;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Raw HTTP GET (plain TCP, no TLS — for banner grabbing on 80/8080)
// ─────────────────────────────────────────────────────────────────────────────
QString RouterDetector::fetchUrl(const QString &host, int port,
                                  const QString &path, bool useTls,
                                  int timeoutMs) const {
    if (useTls) return fetchUrlTls(host, port, path, timeoutMs);

    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return {};

    struct timeval tv{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    inet_pton(AF_INET, host.toLatin1().constData(), &addr.sin_addr);

    // Set non-blocking for connect
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);

    if (::connect(s, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        if (errno != EINPROGRESS) {
            ::close(s); return {};
        }
        fd_set wset;
        FD_ZERO(&wset);
        FD_SET(s, &wset);
        if (::select(s + 1, nullptr, &wset, nullptr, &tv) <= 0) {
            ::close(s); return {};
        }
        int err = 0; socklen_t len = sizeof(err);
        getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len);
        if (err != 0) {
            ::close(s); return {};
        }
    }

    // Restore blocking mode for send/recv with timeouts
    fcntl(s, F_SETFL, flags);


    QByteArray req = QString("GET %1 HTTP/1.0\r\nHost: %2\r\nConnection: close\r\n\r\n")
                         .arg(path, host).toLatin1();
    ::send(s, req.constData(), req.size(), 0);

    QByteArray resp;
    char buf[4096];
    int n;
    while ((n = ::recv(s, buf, sizeof(buf), 0)) > 0)
        resp.append(buf, n);
    ::close(s);
    return QString::fromLatin1(resp.left(8192));
}

// ─────────────────────────────────────────────────────────────────────────────
//  HTTPS GET (real TLS handshake) — for banner grabbing on 443/8443.
//  Router admin certs are almost always self-signed, so peer verification
//  is intentionally disabled here; this is banner-grabbing, not a trust
//  decision, and previously HTTPS-only admin UIs (increasingly common on
//  modern hardware) were silently skipped because the plain-TCP path just
//  received undecodable TLS handshake bytes and gave up.
// ─────────────────────────────────────────────────────────────────────────────
QString RouterDetector::fetchUrlTls(const QString &host, int port,
                                     const QString &path, int timeoutMs) const {
    QSslSocket sock;
    sock.setPeerVerifyMode(QSslSocket::VerifyNone);
    sock.connectToHostEncrypted(host, (quint16)port);
    if (!sock.waitForEncrypted(timeoutMs)) return {};

    QByteArray req = QString("GET %1 HTTP/1.0\r\nHost: %2\r\nConnection: close\r\n\r\n")
                         .arg(path, host).toLatin1();
    sock.write(req);
    if (!sock.waitForBytesWritten(timeoutMs)) return {};

    QByteArray resp;
    QElapsedTimer deadline;
    deadline.start();
    while (deadline.elapsed() < timeoutMs && resp.size() < 8192) {
        if (!sock.waitForReadyRead(200)) {
            if (sock.state() != QAbstractSocket::ConnectedState) break;
            continue;
        }
        resp += sock.readAll();
    }
    return QString::fromLatin1(resp.left(8192));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Layer 1: HTTP Banner Grab
// ─────────────────────────────────────────────────────────────────────────────
void RouterDetector::probeHTTP(RouterInfo &info) {
    struct PortDef { int port; bool tls; };
    const QList<PortDef> ports = {
        {80,   false},
        {8080, false},
        {443,  true},
        {8443, true},
    };
    for (const auto &pd : ports) {
        QString resp = fetchUrl(info.gatewayIp, pd.port, "/", pd.tls, 2000);
        if (resp.isEmpty()) continue;

        info.httpBanner = resp.left(2000);
        info.caps.hasWebUI = true;

        // Server: header
        QRegularExpression reServer("Server:\\s*([^\\r\\n]+)", QRegularExpression::CaseInsensitiveOption);
        auto m = reServer.match(resp);
        if (m.hasMatch()) {
            QString srv = m.captured(1).trimmed();
            if (info.firmware.isEmpty()) info.firmware = srv;
        }

        // WWW-Authenticate realm (model often embedded here)
        QRegularExpression reRealm("realm=\"([^\"]+)\"", QRegularExpression::CaseInsensitiveOption);
        auto mr = reRealm.match(resp);
        if (mr.hasMatch() && info.model.isEmpty())
            info.model = mr.captured(1).trimmed();

        // HTML <title>
        QRegularExpression reTitle("<title[^>]*>([^<]+)</title>", QRegularExpression::CaseInsensitiveOption);
        auto mt = reTitle.match(resp);
        if (mt.hasMatch() && info.friendlyName.isEmpty())
            info.friendlyName = mt.captured(1).trimmed();

        break; // use first responding port
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Layer 2: SSDP / UPnP
//  An M-SEARCH is a multicast broadcast — every UPnP device on the LAN
//  (smart TVs, printers, media servers, consoles…) can answer it, not just
//  the router. Only trusting whichever reply arrives first risks identifying
//  a random device instead of the gateway. We keep reading replies for the
//  full timeout window and only act on the one whose source IP matches the
//  gateway we were asked to probe.
// ─────────────────────────────────────────────────────────────────────────────
void RouterDetector::probeSSDPUPnP(RouterInfo &info) {
    int s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) return;

    struct timeval tv{ 0, 250000 }; // short per-call timeout; we loop until the deadline
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    QByteArray msearch =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 2\r\n"
        "ST: upnp:rootdevice\r\n"
        "\r\n";

    struct sockaddr_in mcast{};
    mcast.sin_family = AF_INET;
    mcast.sin_port   = htons(1900);
    inet_pton(AF_INET, "239.255.255.250", &mcast.sin_addr);
    ::sendto(s, msearch.constData(), msearch.size(), 0,
             (struct sockaddr*)&mcast, sizeof(mcast));

    struct in_addr gwBin{};
    inet_pton(AF_INET, info.gatewayIp.toLatin1().constData(), &gwBin);

    QString ssdpResp;
    char buf[4096];
    QElapsedTimer deadline;
    deadline.start();
    while (deadline.elapsed() < 2200) { // matches the MX:2 delay window, plus slack
        struct sockaddr_in from{};
        socklen_t fromLen = sizeof(from);
        int n = ::recvfrom(s, buf, sizeof(buf)-1, 0, (struct sockaddr*)&from, &fromLen);
        if (n <= 0) continue; // timed out this pass, or no data yet — keep waiting for the deadline

        if (from.sin_addr.s_addr == gwBin.s_addr) {
            buf[n] = '\0';
            ssdpResp = QString::fromLatin1(buf, n);
            break; // found the gateway's own reply — stop, don't keep waiting
        }
        // Reply from some other device on the LAN — ignore it and keep listening
    }
    ::close(s);
    if (ssdpResp.isEmpty()) return;

    info.ssdpResponse = ssdpResp.left(1500);
    info.caps.hasUPnP = true;

    // Extract LOCATION header
    QRegularExpression reLoc("LOCATION:\\s*(http://([^:/]+)(?::(\\d+))?(/[^\\r\\n]*))",
                              QRegularExpression::CaseInsensitiveOption);
    auto ml = reLoc.match(ssdpResp);
    if (!ml.hasMatch()) return;

    QString locHost = ml.captured(2);
    int     locPort = ml.captured(3).isEmpty() ? 80 : ml.captured(3).toInt();
    QString locPath = ml.captured(4);
    if (locPath.isEmpty()) locPath = "/";

    // Fetch the UPnP device descriptor XML
    QString xml = fetchUrl(locHost, locPort, locPath, false, 3000);
    if (!xml.isEmpty()) {
        info.upnpXml = xml.left(4000);
        parseUpnpXml(xml, info);
    }
}

void RouterDetector::parseUpnpXml(const QString &xml, RouterInfo &info) const {
    auto extract = [&](const QString &tag) -> QString {
        QRegularExpression re(QString("<%1>([^<]+)</%1>").arg(tag),
                              QRegularExpression::CaseInsensitiveOption);
        auto m = re.match(xml);
        return m.hasMatch() ? m.captured(1).trimmed() : QString();
    };

    QString mfr    = extract("manufacturer");
    QString model  = extract("modelName");
    QString mnum   = extract("modelNumber");
    QString fname  = extract("friendlyName");
    QString pres   = extract("presentationURL");
    Q_UNUSED(pres)

    if (!mfr.isEmpty()   && info.manufacturer.isEmpty())  info.manufacturer = mfr;
    if (!model.isEmpty() && info.model.isEmpty())         info.model = model;
    if (!mnum.isEmpty()  && info.firmware.isEmpty())      info.firmware = mnum;
    if (!fname.isEmpty() && info.friendlyName.isEmpty())  info.friendlyName = fname;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Layer 3: mDNS PTR query
// ─────────────────────────────────────────────────────────────────────────────
void RouterDetector::probeMDNS(RouterInfo &info) {
    // Send a simple mDNS PTR query for _http._tcp.local and parse the reply.
    // We craft a minimal DNS query packet.
    int s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) return;

    struct timeval tv{ 1, 500000 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // DNS query for "_http._tcp.local" PTR
    // Labels: _http (5), _tcp (4), local (5), 0x00
    QByteArray query;
    query += QByteArray("\x00\x01", 2); // transaction ID
    query += QByteArray("\x00\x00", 2); // flags: standard query
    query += QByteArray("\x00\x01", 2); // QDCount=1
    query += QByteArray("\x00\x00\x00\x00\x00\x00", 6); // AN/NS/AR = 0
    // QNAME: _http._tcp.local
    query += (char)5; query += "_http";
    query += (char)4; query += "_tcp";
    query += (char)5; query += "local";
    query += (char)0;
    query += QByteArray("\x00\x0c", 2); // QTYPE PTR
    query += QByteArray("\x00\x01", 2); // QCLASS IN

    struct sockaddr_in mdns{};
    mdns.sin_family = AF_INET;
    mdns.sin_port   = htons(5353);
    inet_pton(AF_INET, "224.0.0.251", &mdns.sin_addr);
    ::sendto(s, query.constData(), query.size(), 0,
             (struct sockaddr*)&mdns, sizeof(mdns));

    char buf[2048];
    struct sockaddr_in from{};
    socklen_t fromLen = sizeof(from);
    int n = ::recvfrom(s, buf, sizeof(buf)-1, 0, (struct sockaddr*)&from, &fromLen);
    ::close(s);

    struct in_addr gwBin{};
    inet_pton(AF_INET, info.gatewayIp.toLatin1().constData(), &gwBin);
    if (n > 12 && from.sin_addr.s_addr == gwBin.s_addr) {
        // Just record that we got a response; hostname resolution is handled by step4_fingerprint
        info.mdnsInfo = QString("mDNS responded (%1 bytes)").arg(n);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Layer 4: SNMP v1 – sysDescr / sysName / sysLocation
// ─────────────────────────────────────────────────────────────────────────────
QByteArray RouterDetector::buildSnmpGetRequest(const QByteArray &community,
                                                int requestId,
                                                const QList<QByteArray> &oids) const {
    // Build variable-bindings
    QByteArray varbinds;
    for (const auto &oid : oids) {
        QByteArray vb = berOid(oid) + QByteArray("\x05\x00", 2); // OID + NULL
        varbinds += berSeq(0x30, vb);
    }
    QByteArray varbindList = berSeq(0x30, varbinds);

    // GetRequest-PDU (tag 0xA0)
    QByteArray pdu = berInt(requestId) + berInt(0) + berInt(0) + varbindList;
    QByteArray getPdu = berSeq((char)0xA0, pdu);

    // SNMP message
    QByteArray msg = berInt(0) + berOctet(community) + getPdu; // version=0 (v1)
    return berSeq(0x30, msg);
}

QString RouterDetector::parseSnmpString(const QByteArray &resp, int /*oidIndex*/) const {
    // Find OCTET STRING (0x04) in the response after the varbind OID
    int i = 0;
    while (i < resp.size() - 2) {
        if ((unsigned char)resp[i] == 0x04) {
            int len = (unsigned char)resp[i+1];
            if (i + 2 + len <= resp.size())
                return QString::fromLatin1(resp.mid(i+2, len));
        }
        ++i;
    }
    return {};
}

void RouterDetector::probeSNMP(RouterInfo &info) {
    const QList<QByteArray> communities = {"public", "private", "admin"};
    const QList<QByteArray> oids = { OID_SYSDESCR, OID_SYSNAME, OID_SYSLOCATION };

    struct sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port   = htons(161);
    inet_pton(AF_INET, info.gatewayIp.toLatin1().constData(), &target.sin_addr);

    for (const auto &community : communities) {
        int s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s < 0) continue;
        struct timeval tv{ 1, 0 };
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        QByteArray pkt = buildSnmpGetRequest(community, 1, oids);
        ::sendto(s, pkt.constData(), pkt.size(), 0,
                 (struct sockaddr*)&target, sizeof(target));

        char buf[2048];
        struct sockaddr_in from{};
        socklen_t fromLen = sizeof(from);
        int n = ::recvfrom(s, buf, sizeof(buf)-1, 0, (struct sockaddr*)&from, &fromLen);
        ::close(s);

        // Only trust a reply that actually came from the gateway we asked
        if (n > 0 && from.sin_addr.s_addr != target.sin_addr.s_addr) continue;

        if (n > 0) {
            QByteArray resp(buf, n);
            info.caps.hasSNMP = true;

            // Extract OctetStrings in order: sysDescr, sysName, sysLocation
            // Walk resp to find all OCTET STRINGs
            QStringList vals;
            for (int i = 0; i < resp.size() - 2; ++i) {
                if ((unsigned char)resp[i] == 0x04) {
                    int len = (unsigned char)resp[i+1];
                    if (len > 0 && i+2+len <= resp.size()) {
                        vals << QString::fromLatin1(resp.mid(i+2, len)).trimmed();
                        i += 1 + len;
                    }
                }
            }

            // First val = community echo, then sysDescr, sysName, sysLocation
            if (vals.size() >= 2) info.snmpSysDescr   = vals[1];
            if (vals.size() >= 3) info.systemName      = vals[2];
            if (vals.size() >= 4) info.systemLocation  = vals[3];

            break; // stop at first responding community
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Layer 5: Port scan
// ─────────────────────────────────────────────────────────────────────────────
void RouterDetector::probePorts(RouterInfo &info) {
    struct PortDef { int port; QString label; };
    const QList<PortDef> ports = {
        {22,   "22/SSH"},
        {23,   "23/Telnet"},
        {80,   "80/HTTP"},
        {443,  "443/HTTPS"},
        {8080, "8080/HTTP-Alt"},
        {8443, "8443/HTTPS-Alt"},
        {9090, "9090/OpenWRT-LuCI"},
        {179,  "179/BGP"},
        {830,  "830/NETCONF"},
    };
    for (const auto &p : ports) {
        if (tcpConnect(info.gatewayIp, p.port, 400)) {
            info.openPorts << p.label;
            if (p.port == 22)   info.caps.hasSSH    = true;
            if (p.port == 23)   info.caps.hasTelnet = true;
            if (p.port == 80 || p.port == 443 || p.port == 8080 || p.port == 8443)
                info.caps.hasWebUI = true;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Classification: match against model DB
// ─────────────────────────────────────────────────────────────────────────────
void RouterDetector::classifyFromModels(RouterInfo &info) {
    if (m_modelDb.isEmpty()) return;

    // Build a single lowercase searchable blob from all probe outputs
    QString blob = QString("%1 %2 %3 %4 %5 %6")
        .arg(info.snmpSysDescr, info.httpBanner, info.upnpXml,
             info.ssdpResponse, info.model, info.manufacturer)
        .toLower();

    // ── Priority-based best-match ────────────────────────────────────────────
    // Scan ALL entries, skip the __unknown__ sentinel, keep the highest-priority hit.
    int         bestPriority = INT_MIN;
    QJsonObject bestEntry;
    bool        anyMatch = false;

    for (const auto &entry : qAsConst(m_modelDb)) {
        QJsonObject obj = entry.toObject();
        QJsonArray  kws = obj.value("keywords").toArray();

        // Skip programmatic sentinel
        bool sentinel = false;
        for (const auto &kw : qAsConst(kws))
            if (kw.toString() == "__unknown__") { sentinel = true; break; }
        if (sentinel) continue;

        bool matched = false;
        for (const auto &kw : qAsConst(kws)) {
            QString k = kw.toString();
            if (!k.isEmpty() && blob.contains(k.toLower())) { matched = true; break; }
        }
        if (!matched) continue;

        int priority = obj.value("priority").toInt(0);
        if (!anyMatch || priority > bestPriority) {
            bestPriority = priority;
            bestEntry    = obj;
            anyMatch     = true;
        }
    }

    if (!anyMatch) return;

    // ── Apply winning entry ──────────────────────────────────────────────────
    if (info.manufacturer.isEmpty())
        info.manufacturer = bestEntry.value("manufacturer").toString();
    if (info.model.isEmpty())
        info.model = bestEntry.value("model").toString();

    info.matchPriority     = bestPriority;
    info.matchedModelNotes = bestEntry.value("notes").toString();

    QString cls = bestEntry.value("class").toString().toLower();
    if      (cls == "enterprise") info.routerClass = RouterClass::Enterprise;
    else if (cls == "smb")        info.routerClass = RouterClass::SMB;
    else if (cls == "isp-cpe")    info.routerClass = RouterClass::ISPCPE;
    else                          info.routerClass = RouterClass::Home;

    QJsonObject caps = bestEntry.value("caps").toObject();
    // Boolean caps — only set true, never retract a probe-detected true
    if (caps.value("hasGuestWifi").toBool()) info.caps.hasGuestWifi = true;
    if (caps.value("hasVPN").toBool())       info.caps.hasVPN       = true;
    if (caps.value("hasQoS").toBool())       info.caps.hasQoS       = true;
    if (caps.value("hasDualBand").toBool())  info.caps.hasDualBand  = true;
    if (caps.value("hasTriBand").toBool())   info.caps.hasTriBand   = true;
    if (caps.value("hasIPv6").toBool())      info.caps.hasIPv6      = true;
    if (caps.value("hasWPA3").toBool())      info.caps.hasWPA3      = true;
    // Security fields — explicit DB value (including false) is authoritative
    if (caps.contains("defaultCredsRisk"))
        info.caps.defaultCredsRisk = caps.value("defaultCredsRisk").toBool(false);
    if (caps.contains("firmwareRisk")) {
        QString fr = caps.value("firmwareRisk").toString();
        if (fr != "null" && !fr.isEmpty()) info.caps.firmwareRisk = fr;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Infer remaining caps from port/SNMP/UPnP evidence
// ─────────────────────────────────────────────────────────────────────────────
void RouterDetector::inferCapabilities(RouterInfo &info) {
    // Enterprise rule: SSH + SNMP implies managed/enterprise hardware
    if (info.caps.hasSSH && info.caps.hasSNMP)
        info.caps.isEnterprise = true;
    if (info.routerClass == RouterClass::Enterprise)
        info.caps.isEnterprise = true;

    // Fallback class assignment (only if model DB didn't match)
    if (info.routerClass == RouterClass::Unknown) {
        if (info.caps.isEnterprise)  info.routerClass = RouterClass::Enterprise;
        else if (info.caps.hasSSH)   info.routerClass = RouterClass::SMB;
        else if (info.isRandomizedMac &&
                 !info.caps.hasWebUI &&
                 !info.caps.hasSNMP &&
                 !info.caps.hasUPnP) {
            info.routerClass = RouterClass::MobileHotspot;
            if (info.manufacturer.isEmpty()) info.manufacturer = "Mobile Device";
            if (info.model.isEmpty())        info.model        = "Generic Tethering Hotspot";
        }
        else                         info.routerClass = RouterClass::Home;
    }

    // VPN / IPv6 hints from SNMP sysDescr
    QString desc = info.snmpSysDescr.toLower();
    if (desc.contains("vpn") || desc.contains("ipsec") || desc.contains("openvpn"))
        info.caps.hasVPN = true;
    if (desc.contains("ipv6") || desc.contains("6in4"))
        info.caps.hasIPv6 = true;

    // Manufacturer fallback from MAC OUI table
    if (info.manufacturer.isEmpty() && !info.gatewayMac.isEmpty()) {
        QString oui = core::NetworkManager::getMacVendor(info.gatewayMac);
        info.manufacturer = (oui == "Unknown Vendor") ? "Unknown" : oui;
    }

    if (info.model.isEmpty())    info.model    = "Unknown Model";
    if (info.firmware.isEmpty()) info.firmware = "—";
}


} // namespace core
