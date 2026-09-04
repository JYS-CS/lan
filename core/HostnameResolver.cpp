// HostnameResolver.cpp
// Multi-layer automatic hostname discovery.
//
// KEY DESIGN:
//   Per device: all 4 layers run in PARALLEL sub-threads; we take the first
//   result that arrives via a mutex-protected shared string + condition variable.
//   This ensures wall-clock time = fastest responding layer, not sum of timeouts.
//
//   Across devices: each device gets its own PerDeviceRunnable in a QThreadPool,
//   so all devices are probed concurrently.
//
// WHY NOT getnameinfo():
//   getnameinfo() with NI_NAMEREQD ignores our timeout and uses the system resolver
//   retry policy (typically 5 s × 2 retries = 10+ s per call). We use a raw
//   DNS UDP socket instead so the timeout is always respected.

#include "HostnameResolver.h"

#include <QThreadPool>
#include <QRunnable>
#include <QMutex>
#include <QMutexLocker>
#include <QWaitCondition>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QTcpSocket>
#include <QRegularExpression>

// POSIX / Linux
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>

namespace core {

// ─────────────────────────────────────────────────────────────────────────────
// Shared result: allows the first successful layer to report a hostname
// ─────────────────────────────────────────────────────────────────────────────
struct SharedResult {
    QMutex       mutex;
    QWaitCondition cond;
    QString      hostname;
    int          done = 0;   // layers that have finished (success or fail)
    int          total = 4;  // total layers running
    bool         resolved = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Helper: build a DNS/mDNS PTR query for  d.c.b.a.in-addr.arpa
// ─────────────────────────────────────────────────────────────────────────────
static int buildPtrQuery(unsigned char *buf, int bufSize,
                         const QString &ip, uint16_t txId)
{
    QStringList parts = ip.split('.');
    if (parts.size() != 4) return 0;

    memset(buf, 0, bufSize);
    buf[0] = (txId >> 8) & 0xFF;
    buf[1] =  txId       & 0xFF;
    buf[2] = 0x01;  // RD = 1 (recursion desired, used by rDNS layer)
    buf[3] = 0x00;
    buf[4] = 0x00; buf[5] = 0x01; // QDCOUNT = 1

    int pos = 12;
    // QNAME: d.c.b.a.in-addr.arpa
    const QString labels[] = { parts[3], parts[2], parts[1], parts[0],
                                QStringLiteral("in-addr"), QStringLiteral("arpa") };
    for (const QString &lbl : labels) {
        QByteArray lb = lbl.toLatin1();
        if (pos + 1 + lb.size() >= bufSize) return 0;
        buf[pos++] = (unsigned char)lb.size();
        memcpy(buf + pos, lb.constData(), lb.size());
        pos += lb.size();
    }
    buf[pos++] = 0x00;               // root label
    buf[pos++] = 0x00; buf[pos++] = 0x0C; // QTYPE PTR
    buf[pos++] = 0x00; buf[pos++] = 0x01; // QCLASS IN
    return pos;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: parse a DNS name from packet, advancing offset past the name
// ─────────────────────────────────────────────────────────────────────────────
static QString parseDnsName(const unsigned char *buf, int bufLen, int &offset)
{
    QString result;
    int maxJumps = 10, savedOffset = 0;
    bool jumped = false;

    while (offset < bufLen) {
        unsigned char len = buf[offset];
        if (len == 0) { ++offset; break; }
        if ((len & 0xC0) == 0xC0) {
            if (offset + 1 >= bufLen) break;
            int ptr = ((len & 0x3F) << 8) | buf[offset + 1];
            if (!jumped) savedOffset = offset + 2;
            offset = ptr; jumped = true;
            if (--maxJumps == 0) break;
            continue;
        }
        ++offset;
        if (offset + len > bufLen) break;
        if (!result.isEmpty()) result += '.';
        result += QString::fromLatin1(reinterpret_cast<const char *>(buf + offset), len);
        offset += len;
    }
    if (jumped && savedOffset) offset = savedOffset;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: parse PTR answer from a DNS/mDNS/LLMNR response buffer
// ─────────────────────────────────────────────────────────────────────────────
static QString parsePtrFromResponse(const unsigned char *buf, int n, uint16_t txId,
                                    bool acceptZeroTx = false)
{
    if (n < 12) return {};
    uint16_t respTx = (uint16_t)((buf[0] << 8) | buf[1]);
    if (respTx != txId && !(acceptZeroTx && respTx == 0)) return {};
    if (!(buf[2] & 0x80)) return {}; // QR must be 1
    if ((buf[3] & 0x0F) != 0) return {}; // RCODE must be 0

    uint16_t anCount = (uint16_t)((buf[6] << 8) | buf[7]);
    if (anCount == 0) return {};

    int offset = 12;
    uint16_t qdCount = (uint16_t)((buf[4] << 8) | buf[5]);
    for (int q = 0; q < qdCount && offset < n; ++q) {
        parseDnsName(buf, n, offset);
        offset += 4;
    }

    for (int a = 0; a < anCount && offset < n; ++a) {
        parseDnsName(buf, n, offset);
        if (offset + 10 > n) break;
        uint16_t rtype = (uint16_t)((buf[offset] << 8) | buf[offset+1]); offset += 2;
        offset += 6; // CLASS + TTL
        uint16_t rdlen = (uint16_t)((buf[offset] << 8) | buf[offset+1]); offset += 2;
        if (rtype == 12 && rdlen > 1) {
            int ns = offset;
            QString name = parseDnsName(buf, n, ns);
            if (!name.isEmpty()) return name;
        }
        offset += rdlen;
    }
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: read first nameserver from /etc/resolv.conf
// ─────────────────────────────────────────────────────────────────────────────
static QString localNameserver()
{
    QFile f(QStringLiteral("/etc/resolv.conf"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.startsWith(QLatin1String("nameserver "))) {
                QString ns = line.mid(11).trimmed();
                if (!ns.isEmpty()) return ns;
            }
        }
    }
    return QStringLiteral("127.0.0.53"); // systemd-resolved fallback
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: open a UDP socket with receive timeout
// ─────────────────────────────────────────────────────────────────────────────
static int udpSocket(int timeoutMs)
{
    int s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return -1;
    struct timeval tv{};
    tv.tv_sec  =  timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return s;
}

// ═════════════════════════════════════════════════════════════════════════════
// LAYER 1 — mDNS legacy-unicast query (RFC 6762 §6.7)
//
// Sending from a non-5353 source port signals a "legacy unicast" query.
// The target device sends a unicast response back to us directly.
// We send to 224.0.0.251 (multicast) so devices that only listen on the
// multicast address also hear us — sending to the device's unicast IP
// is ignored by most mDNS stacks.
// ═════════════════════════════════════════════════════════════════════════════
QString HostnameResolver::tryMDNS(const QString &ip, int timeoutMs)
{
    int s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return {};

    struct timeval tv{};
    tv.tv_sec  = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // TTL=1 — link-local only, never leaves the LAN segment
    int ttl = 1;
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    // Destination: mDNS multicast group
    struct sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port   = htons(5353);
    inet_pton(AF_INET, "224.0.0.251", &dest.sin_addr);

    unsigned char pkt[512];
    // Non-zero txId + non-5353 source port = legacy unicast query (RFC 6762 §6.7)
    uint16_t txId = (uint16_t)(rand() % 0xFFFE + 1);
    int pktLen = buildPtrQuery(pkt, sizeof(pkt), ip, txId);
    if (pktLen <= 0) { ::close(s); return {}; }
    pkt[2] = 0x00; pkt[3] = 0x00; // flags: standard query, no RD

    if (::sendto(s, pkt, pktLen, 0,
                 reinterpret_cast<sockaddr *>(&dest), sizeof(dest)) < 0) {
        ::close(s); return {};
    }

    // Receive loop — only accept response from our target IP
    unsigned char buf[512];
    struct sockaddr_in src{};
    socklen_t sl = sizeof(src);
    while (true) {
        int n = ::recvfrom(s, buf, sizeof(buf), 0,
                           reinterpret_cast<sockaddr *>(&src), &sl);
        if (n < 0) break; // timeout (EAGAIN/EWOULDBLOCK) or error
        if (n < 12) continue;

        char srcBuf[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &src.sin_addr, srcBuf, sizeof(srcBuf));
        if (ip != QString::fromLatin1(srcBuf)) continue; // not from our target

        uint16_t respTx = (uint16_t)((buf[0] << 8) | buf[1]);
        // Accept matching txId or zero (some implementations always use 0)
        bool txOk = (respTx == txId) || (respTx == 0);
        QString name = parsePtrFromResponse(buf, n, respTx, txOk);
        if (!name.isEmpty()) {
            ::close(s);
            int si = name.indexOf(QLatin1String("._"));
            if (si > 0) name = name.left(si);
            return stripLocal(name);
        }
    }
    ::close(s);
    return {};
}



// ═════════════════════════════════════════════════════════════════════════════
// LAYER 2 — NetBIOS Name Service Node-Status (RFC 1002)
// ═════════════════════════════════════════════════════════════════════════════
QString HostnameResolver::tryNBNS(const QString &ip, int timeoutMs)
{
    int s = udpSocket(timeoutMs);
    if (s < 0) return {};

    struct sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port   = htons(137);
    inet_pton(AF_INET, ip.toLatin1().constData(), &dest.sin_addr);

    // NBNS Node Status Request for wildcard '*'
    static const unsigned char nbns[] = {
        0xAB, 0xCD,                   // Transaction ID
        0x00, 0x00,                   // Flags: standard query
        0x00, 0x01,                   // QDCOUNT=1
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x20,                         // encoded name length=32
        'C','K',                      // '*' (0x2A) encoded
        'A','A','A','A','A','A','A','A','A','A','A','A','A','A',
        'A','A','A','A','A','A','A','A','A','A','A','A','A','A',
        'A','A',
        0x00,                         // root label
        0x00, 0x21,                   // QTYPE NBSTAT
        0x00, 0x01,                   // QCLASS IN
    };

    if (::sendto(s, nbns, sizeof(nbns), 0,
                 reinterpret_cast<sockaddr *>(&dest), sizeof(dest)) < 0) {
        ::close(s); return {};
    }

    unsigned char buf[512];
    socklen_t sl = sizeof(dest);
    int n = ::recvfrom(s, buf, sizeof(buf), 0,
                       reinterpret_cast<sockaddr *>(&dest), &sl);
    ::close(s);

    // NBNS NODE STATUS RESPONSE byte layout (RFC 1002 §4.2.18):
    //   [0-11]  : DNS-like header (12 bytes)
    //   [12-49] : Question section — QNAME (34 bytes) + QTYPE + QCLASS (4 bytes)
    //   [50-51] : Answer NAME — 0xC0 0x0C (compressed pointer)
    //   [52-53] : TYPE  = 0x0021 (NBSTAT)
    //   [54-55] : CLASS = 0x0001
    //   [56-59] : TTL   (4 bytes)
    //   [60-61] : RDLENGTH
    //   [62]    : NUM_NAMES
    //   [63+]   : Name entries (18 bytes each: 15-char name + 1 suffix + 2 flags)
    if (n < 63) return {};
    if (!(buf[2] & 0x80)) return {};     // QR=1
    if ((buf[3] & 0x0F) != 0) return {}; // RCODE=0

    int numNames = (int)buf[62];
    if (numNames <= 0 || 63 + numNames * 18 > n) return {};

    QString first;
    for (int i = 0; i < numNames; ++i) {
        const unsigned char *e = buf + 63 + i * 18;
        char raw[16] = {};
        memcpy(raw, e, 15); raw[15] = '\0';
        uint16_t flags   = (uint16_t)((e[16] << 8) | e[17]);
        bool isGroup      = (flags & 0x8000) != 0;
        unsigned char sfx = e[15];
        if (isGroup) continue;
        QString name = QString::fromLatin1(raw).trimmed();
        if (name.isEmpty() || name == QStringLiteral("*")) continue;
        if (first.isEmpty()) first = name;
        if (sfx == 0x00) return name; // workstation suffix — best match
    }
    return first;
}


// ═════════════════════════════════════════════════════════════════════════════
// LAYER 3 — LLMNR PTR query (RFC 4795)
// ═════════════════════════════════════════════════════════════════════════════
QString HostnameResolver::tryLLMNR(const QString &ip, int timeoutMs)
{
    int s = udpSocket(timeoutMs);
    if (s < 0) return {};

    struct sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port   = htons(5355);
    inet_pton(AF_INET, ip.toLatin1().constData(), &dest.sin_addr);

    unsigned char pkt[512];
    uint16_t txId = (uint16_t)(rand() % 0xFFFE + 1);
    int pktLen = buildPtrQuery(pkt, sizeof(pkt), ip, txId);
    if (pktLen <= 0) { ::close(s); return {}; }
    pkt[2] = 0x00; pkt[3] = 0x00; // LLMNR flags: standard query, no RD

    if (::sendto(s, pkt, pktLen, 0,
                 reinterpret_cast<sockaddr *>(&dest), sizeof(dest)) < 0) {
        ::close(s); return {};
    }

    unsigned char buf[512];
    socklen_t sl = sizeof(dest);
    int n = ::recvfrom(s, buf, sizeof(buf), 0,
                       reinterpret_cast<sockaddr *>(&dest), &sl);
    ::close(s);

    return stripLocal(parsePtrFromResponse(buf, n, txId));
}

// ═════════════════════════════════════════════════════════════════════════════
// LAYER 4 — Reverse DNS via raw UDP/53 to local nameserver
// Uses a hand-crafted DNS PTR query so we can enforce timeoutMs.
// Does NOT use getnameinfo() which ignores our timeout (can block 10+ s).
// ═════════════════════════════════════════════════════════════════════════════
QString HostnameResolver::tryReverseDNS(const QString &ip, int timeoutMs)
{
    static const QString ns = localNameserver(); // read once, reuse

    int s = udpSocket(timeoutMs);
    if (s < 0) return {};

    struct sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port   = htons(53);
    inet_pton(AF_INET, ns.toLatin1().constData(), &dest.sin_addr);

    unsigned char pkt[512];
    uint16_t txId = (uint16_t)(rand() % 0xFFFE + 1);
    int pktLen = buildPtrQuery(pkt, sizeof(pkt), ip, txId);
    if (pktLen <= 0) { ::close(s); return {}; }
    // RD=1 already set by buildPtrQuery

    if (::sendto(s, pkt, pktLen, 0,
                 reinterpret_cast<sockaddr *>(&dest), sizeof(dest)) < 0) {
        ::close(s); return {};
    }

    unsigned char buf[512];
    socklen_t sl = sizeof(dest);
    int n = ::recvfrom(s, buf, sizeof(buf), 0,
                       reinterpret_cast<sockaddr *>(&dest), &sl);
    ::close(s);

    QString name = parsePtrFromResponse(buf, n, txId);
    if (name.isEmpty() || name == ip) return {};
    // Strip the trailing dot that some resolvers append
    if (name.endsWith('.')) name.chop(1);
    return stripLocal(name);
}

// ─────────────────────────────────────────────────────────────────────────────
QString HostnameResolver::stripLocal(const QString &name)
{
    QString n = name;
    if (n.endsWith(QLatin1String(".local."), Qt::CaseInsensitive))
        n.chop(7);
    else if (n.endsWith(QLatin1String(".local"), Qt::CaseInsensitive))
        n.chop(6);
    return n;
}

// ═════════════════════════════════════════════════════════════════════════════
// LAYER 5 — SSDP/UPnP (Simple Service Discovery Protocol)
//
// Covers: WiFi repeaters, routers, IP cameras, smart TVs, printers, smart
// home devices — anything with a UPnP stack (nearly all consumer devices).
//
// 1. Send an M-SEARCH to the SSDP multicast group 239.255.255.250:1900.
// 2. Filter responses from our target IP.
// 3. Extract device name from the SERVER: header (fast, no HTTP needed).
// 4. If SERVER is too generic, fetch the LOCATION: URL and parse <friendlyName>
//    from the UPnP device description XML.
// ═════════════════════════════════════════════════════════════════════════════
QString HostnameResolver::trySSDPUPnP(const QString &ip, int timeoutMs)
{
    // ── Step 1: send M-SEARCH multicast ──────────────────────────────────────
    int s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return {};

    struct timeval tv{};
    tv.tv_sec  = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int ttl = 2;
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    struct sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port   = htons(1900);
    inet_pton(AF_INET, "239.255.255.250", &dest.sin_addr);

    const char msearch[] =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 1\r\n"
        "ST: ssdp:all\r\n"
        "\r\n";

    if (::sendto(s, msearch, strlen(msearch), 0,
                 reinterpret_cast<sockaddr *>(&dest), sizeof(dest)) < 0) {
        ::close(s); return {};
    }

    // ── Step 2: receive responses, filter by source IP ───────────────────────
    QString locationUrl;
    QString serverHeader;

    char buf[2048];
    struct sockaddr_in src{};
    socklen_t sl = sizeof(src);
    while (true) {
        int n = ::recvfrom(s, buf, sizeof(buf) - 1, 0,
                           reinterpret_cast<sockaddr *>(&src), &sl);
        if (n < 0) break; // timeout
        buf[n] = '\0';

        char srcStr[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &src.sin_addr, srcStr, sizeof(srcStr));
        if (ip != QString::fromLatin1(srcStr)) continue; // not our device

        // Parse headers
        QString response = QString::fromLatin1(buf, n);
        const QStringList lines = response.split(QStringLiteral("\r\n"));
        for (const QString &line : lines) {
            QString upper = line.toUpper();
            if (upper.startsWith(QLatin1String("LOCATION:")) && locationUrl.isEmpty())
                locationUrl = line.mid(9).trimmed();
            if (upper.startsWith(QLatin1String("SERVER:")) && serverHeader.isEmpty())
                serverHeader = line.mid(7).trimmed();
        }
        if (!locationUrl.isEmpty()) break; // got what we need
    }
    ::close(s);

    // ── Step 3: quick name from SERVER: header ────────────────────────────────
    // SERVER: often looks like "Linux/3.14 UPnP/1.0 TP-Link/1.0"
    // or "Windows/10 UPnP/1.0 Microsoft-HTTPAPI/2.0"
    // Extract the last product token (most specific)
    if (!serverHeader.isEmpty()) {
        // Remove "UPnP/x.x" token and generic OS tokens, keep the product name
        static const QRegularExpression reProduct(
            QStringLiteral("([A-Za-z0-9_\\-\\.]+)/[\\d\\.]+$"));
        auto m = reProduct.match(serverHeader);
        if (m.hasMatch()) {
            QString prod = m.captured(1);
            // Skip generic tokens
            static const QStringList skip = {
                "UPnP","Linux","Windows","Darwin","FreeBSD","POSIX","Microsoft"
            };
            if (!skip.contains(prod, Qt::CaseInsensitive) && prod.length() > 2)
                return prod;
        }
    }

    // ── Step 4: fetch UPnP XML and parse <friendlyName> ──────────────────────
    if (locationUrl.isEmpty()) return {};

    // Parse host and path from the LOCATION URL (plain HTTP only)
    // Example: http://192.168.8.1:49152/description.xml
    static const QRegularExpression reUrl(
        QStringLiteral("http://([^:/]+)(?::(\\d+))?(/[^\\s]*)"),
        QRegularExpression::CaseInsensitiveOption);
    auto mu = reUrl.match(locationUrl);
    if (!mu.hasMatch()) return {};

    QString xmlHost = mu.captured(1);
    quint16 xmlPort = mu.captured(2).isEmpty() ? 80 : mu.captured(2).toUShort();
    QString xmlPath = mu.captured(3);

    // QTcpSocket must run on a thread that has an event loop OR use blocking
    // connect. We are in a worker thread, so use a blocking approach with
    // waitForXxx() calls.
    QTcpSocket sock;
    sock.connectToHost(xmlHost, xmlPort);
    if (!sock.waitForConnected(timeoutMs)) return {};

    QString req = QString("GET %1 HTTP/1.0\r\nHost: %2\r\nConnection: close\r\n\r\n")
                  .arg(xmlPath, xmlHost);
    sock.write(req.toLatin1());
    if (!sock.waitForBytesWritten(1000)) return {};

    QByteArray xmlData;
    while (sock.waitForReadyRead(timeoutMs))
        xmlData += sock.readAll();
    sock.close();

    // Parse <friendlyName>…</friendlyName> from the XML body
    static const QRegularExpression reName(
        QStringLiteral("<friendlyName>([^<]+)</friendlyName>"),
        QRegularExpression::CaseInsensitiveOption);
    auto mn = reName.match(QString::fromUtf8(xmlData));
    if (mn.hasMatch()) {
        QString friendly = mn.captured(1).trimmed();
        if (!friendly.isEmpty()) return friendly;
    }

    return {};
}

// ═════════════════════════════════════════════════════════════════════════════
// PerDeviceRunnable — runs all 4 layers IN PARALLEL for one IP
// Wall-clock time ≈ max(fastest_layer_timeout, fastest_layer_response)
// ═════════════════════════════════════════════════════════════════════════════
class PerDeviceRunnable : public QRunnable {
public:
    PerDeviceRunnable(const QString &ip, int timeoutMs,
                      QMap<QString, QString> &results, QMutex &resultsMutex)
        : m_ip(ip), m_tms(timeoutMs), m_results(results), m_mutex(resultsMutex)
    { setAutoDelete(true); }

    void run() override {
        // Shared state between the 5 sub-threads
        SharedResult sr;
        sr.total = 5;

        // Layer functors
        using LayerFn = QString(*)(const QString &, int);
        LayerFn layers[5] = {
            HostnameResolver::tryMDNS,
            HostnameResolver::tryNBNS,
            HostnameResolver::tryLLMNR,
            HostnameResolver::tryReverseDNS,
            HostnameResolver::trySSDPUPnP,
        };

        // Spin up 5 sub-threads, one per layer
        QThreadPool sub;
        sub.setMaxThreadCount(5);
        for (int i = 0; i < 5; ++i) {
            LayerFn fn = layers[i];
            QString ip = m_ip;
            int tms = m_tms;
            SharedResult *srp = &sr;

            auto *r = QThreadPool::globalInstance(); // dummy ref, not used
            Q_UNUSED(r)

            struct LayerRunnable : public QRunnable {
                LayerFn fn; QString ip; int tms; SharedResult *sr;
                LayerRunnable(LayerFn f, const QString &ip, int t, SharedResult *s)
                    : fn(f), ip(ip), tms(t), sr(s) { setAutoDelete(true); }
                void run() override {
                    QString name = fn(ip, tms);
                    QMutexLocker lk(&sr->mutex);
                    if (!name.isEmpty() && !sr->resolved) {
                        sr->hostname = name;
                        sr->resolved = true;
                    }
                    ++sr->done;
                    sr->cond.wakeAll();
                }
            };
            sub.start(new LayerRunnable(fn, ip, tms, srp));
        }

        // Wait until first success OR all layers finish, with hard timeout
        QMutexLocker lk(&sr.mutex);
        int waitMs = m_tms + 200; // a bit beyond per-layer timeout
        
        // Qt's QElapsedTimer is perfect for loop-based timeouts
        QElapsedTimer timer;
        timer.start();
        while (!sr.resolved && sr.done < sr.total) {
            int remaining = waitMs - timer.elapsed();
            if (remaining <= 0) break; // hard timeout
            sr.cond.wait(&sr.mutex, remaining);
        }
        
        // Drain any still-running sub-threads
        lk.unlock();
        sub.waitForDone(m_tms + 500);

        if (!sr.hostname.isEmpty()) {
            QMutexLocker rl(&m_mutex);
            m_results.insert(m_ip, sr.hostname);
        }
    }

private:
    QString  m_ip;
    int      m_tms;
    QMap<QString, QString> &m_results;
    QMutex  &m_mutex;
};

// ═════════════════════════════════════════════════════════════════════════════
// resolveAll — submit all devices to a thread pool
// ═════════════════════════════════════════════════════════════════════════════
QMap<QString, QString> HostnameResolver::resolveAll(const QList<QString> &ips, int timeoutMs)
{
    if (ips.isEmpty()) return {};

    QMap<QString, QString> results;
    QMutex mutex;

    QThreadPool pool;
    pool.setMaxThreadCount(qMin(ips.size(), 32));

    for (const QString &ip : ips)
        pool.start(new PerDeviceRunnable(ip, timeoutMs, results, mutex));

    // Each device wall-clock ≈ timeoutMs; give a 1-second buffer
    pool.waitForDone(timeoutMs + 1000);
    return results;
}

QString HostnameResolver::resolveOne(const QString &ip, int timeoutMs)
{
    return resolveAll({ip}, timeoutMs).value(ip);
}

} // namespace core
