#include "DnsProxyServer.h"
#include <QNetworkDatagram>
#include <QRandomGenerator>
#include <QDebug>
#include <cstring>

namespace core {

// Cache is intentionally simple: a flat TTL rather than parsing each
// answer record's own TTL (which requires walking compressed RR names).
// Short enough that this never meaningfully staleness a real change,
// long enough to absorb the burst of repeat lookups a single page load
// generates.
static constexpr int kCacheTtlMs = 30000;
static constexpr int kUpstreamTimeoutMs = 2000;
static constexpr int kPendingMaxAgeMs   = 6000; // drop if neither upstream ever answers

DnsProxyServer::DnsProxyServer(QObject *parent) : QObject(parent) {
    m_clock.start();
}

DnsProxyServer::~DnsProxyServer() {
    stop();
}

bool DnsProxyServer::start(const QString &listenIp, const QString &localSubnetCidr,
                            const QString &upstream1, const QString &upstream2) {
    stop();

    // Parse "a.b.c.d/nn" into a base+mask for fast source-IP validation.
    QStringList parts = localSubnetCidr.split('/');
    if (parts.size() != 2) {
        qWarning() << "[DnsProxy] invalid subnet CIDR:" << localSubnetCidr;
        return false;
    }
    QHostAddress subnetAddr(parts[0]);
    bool ok = false;
    int prefixLen = parts[1].toInt(&ok);
    if (subnetAddr.isNull() || !ok || prefixLen < 0 || prefixLen > 32) {
        qWarning() << "[DnsProxy] invalid subnet CIDR:" << localSubnetCidr;
        return false;
    }
    m_subnetMask = prefixLen == 0 ? 0 : (0xFFFFFFFFu << (32 - prefixLen));
    m_subnetBase = subnetAddr.toIPv4Address() & m_subnetMask;

    m_upstream1 = QHostAddress(upstream1);
    m_upstream2 = QHostAddress(upstream2);

    m_clientSocket = new QUdpSocket(this);
    if (!m_clientSocket->bind(QHostAddress(listenIp), 53, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        qWarning() << "[DnsProxy] failed to bind" << listenIp << ":53 —" << m_clientSocket->errorString();
        delete m_clientSocket;
        m_clientSocket = nullptr;
        return false;
    }
    connect(m_clientSocket, &QUdpSocket::readyRead, this, &DnsProxyServer::onClientDatagram);

    m_upstreamSocket = new QUdpSocket(this);
    m_upstreamSocket->bind(QHostAddress::AnyIPv4, 0);
    connect(m_upstreamSocket, &QUdpSocket::readyRead, this, &DnsProxyServer::onUpstreamDatagram);

    m_cleanupTimer = new QTimer(this);
    connect(m_cleanupTimer, &QTimer::timeout, this, &DnsProxyServer::onCleanupTick);
    m_cleanupTimer->start(1000);

    m_running = true;
    qDebug() << "[DnsProxy] listening on" << listenIp << ":53, subnet" << localSubnetCidr
              << "upstreams" << upstream1 << upstream2;
    return true;
}

void DnsProxyServer::stop() {
    if (m_clientSocket)   { m_clientSocket->close();   m_clientSocket->deleteLater();   m_clientSocket = nullptr; }
    if (m_upstreamSocket) { m_upstreamSocket->close();  m_upstreamSocket->deleteLater(); m_upstreamSocket = nullptr; }
    if (m_cleanupTimer)   { m_cleanupTimer->stop();     m_cleanupTimer->deleteLater();   m_cleanupTimer = nullptr; }
    m_pending.clear();
    m_cache.clear();
    m_rateCounters.clear();
    m_running = false;
}

void DnsProxyServer::setFilteringEnabled(bool enabled) { m_filtering = enabled; }

void DnsProxyServer::setBlockedDomains(const QStringList &domains) {
    m_blockedDomains = QSet<QString>(domains.constBegin(), domains.constEnd());
}

bool DnsProxyServer::isBlocked(const QString &domain) const {
    if (!m_filtering || m_blockedDomains.isEmpty()) return false;
    if (m_blockedDomains.contains(domain)) return true;
    // Also block subdomains of a blocked domain (e.g. blocking "ads.example.com"
    // should also catch "tracker.ads.example.com").
    QString d = domain;
    int dot;
    while ((dot = d.indexOf('.')) != -1) {
        d = d.mid(dot + 1);
        if (d.isEmpty()) break;
        if (m_blockedDomains.contains(d)) return true;
    }
    return false;
}

bool DnsProxyServer::isRateLimited(const QString &clientIp) {
    qint64 now = m_clock.elapsed();
    auto &entry = m_rateCounters[clientIp];
    if (now - entry.second > 1000) {
        entry.first = 0;
        entry.second = now;
    }
    entry.first++;
    return entry.first > kMaxQueriesPerSecond;
}

QString DnsProxyServer::parseQName(const QByteArray &data, int &offset) {
    QStringList labels;
    int guard = 0;
    while (offset < data.size() && guard++ < 128) {
        quint8 len = static_cast<quint8>(data[offset]);
        if (len == 0) { offset += 1; break; }
        if ((len & 0xC0) == 0xC0) { offset += 2; break; } // compression pointer — queries rarely use these, just stop
        offset += 1;
        if (offset + len > data.size()) break;
        labels << QString::fromLatin1(data.mid(offset, len));
        offset += len;
    }
    return labels.join('.');
}

QString DnsProxyServer::qtypeToString(quint16 type) {
    switch (type) {
        case 1:  return "A";
        case 28: return "AAAA";
        case 5:  return "CNAME";
        case 15: return "MX";
        case 16: return "TXT";
        case 2:  return "NS";
        case 6:  return "SOA";
        case 12: return "PTR";
        case 33: return "SRV";
        case 65: return "HTTPS";
        case 64: return "SVCB";
        default: return QString("TYPE%1").arg(type);
    }
}

void DnsProxyServer::onClientDatagram() {
    while (m_clientSocket && m_clientSocket->hasPendingDatagrams()) {
        QNetworkDatagram dgram = m_clientSocket->receiveDatagram();
        QByteArray data = dgram.data();
        QHostAddress sender = dgram.senderAddress();
        quint16 senderPort = dgram.senderPort();

        if (data.size() < 12) continue; // shorter than a DNS header — malformed, drop

        quint32 senderIpInt = sender.toIPv4Address();
        if (senderIpInt == 0 || (senderIpInt & m_subnetMask) != m_subnetBase) {
            continue; // outside our LAN — never answer, never forward (anti-reflection)
        }
        QString clientIp = sender.toString();
        if (isRateLimited(clientIp)) continue;

        quint16 origTxId = (static_cast<quint8>(data[0]) << 8) | static_cast<quint8>(data[1]);
        int offset = 12;
        QString domain = parseQName(data, offset);
        if (domain.isEmpty()) continue;
        quint16 qtypeNum = 1;
        if (offset + 2 <= data.size()) {
            qtypeNum = (static_cast<quint8>(data[offset]) << 8) | static_cast<quint8>(data[offset + 1]);
        }
        QString qtype = qtypeToString(qtypeNum);
        QString lowerDomain = domain.toLower();

        bool blocked = isBlocked(lowerDomain);
        if (blocked) {
            sendNxDomain(data, sender, senderPort);
            emit queryObserved(clientIp, lowerDomain, qtype, true, false);
            continue;
        }

        QString cacheKey = lowerDomain + "|" + qtype;
        auto cacheIt = m_cache.constFind(cacheKey);
        if (cacheIt != m_cache.constEnd() && cacheIt->expiresAtMs > m_clock.elapsed()) {
            relayFromCache(cacheIt.value(), origTxId, sender, senderPort);
            emit queryObserved(clientIp, lowerDomain, qtype, false, true);
            continue;
        }

        // Forward upstream with a randomized transaction ID of our own,
        // remembering how to route the reply back.
        quint16 upstreamTxId;
        int guard = 0;
        do {
            upstreamTxId = static_cast<quint16>(QRandomGenerator::global()->bounded(1, 65535));
        } while (m_pending.contains(upstreamTxId) && ++guard < 20);

        PendingQuery pq;
        pq.originalTxId = origTxId;
        pq.clientAddr   = sender;
        pq.clientPort   = senderPort;
        pq.domain       = lowerDomain;
        pq.qtype        = qtype;
        pq.sentAtMs     = m_clock.elapsed();

        QByteArray forwardPkt = data;
        forwardPkt[0] = static_cast<char>((upstreamTxId >> 8) & 0xFF);
        forwardPkt[1] = static_cast<char>(upstreamTxId & 0xFF);
        pq.forwardPacket = forwardPkt;

        m_pending.insert(upstreamTxId, pq);
        m_upstreamSocket->writeDatagram(forwardPkt, m_upstream1, 53);

        emit queryObserved(clientIp, lowerDomain, qtype, false, false);
    }
}

void DnsProxyServer::onUpstreamDatagram() {
    while (m_upstreamSocket && m_upstreamSocket->hasPendingDatagrams()) {
        QNetworkDatagram dgram = m_upstreamSocket->receiveDatagram();
        QByteArray data = dgram.data();
        QHostAddress sender = dgram.senderAddress();

        // Only trust replies actually coming from an upstream we queried —
        // defense against off-path spoofed UDP responses.
        if (sender != m_upstream1 && sender != m_upstream2) continue;
        if (data.size() < 12) continue;

        quint16 replyTxId = (static_cast<quint8>(data[0]) << 8) | static_cast<quint8>(data[1]);
        auto it = m_pending.find(replyTxId);
        if (it == m_pending.end()) continue; // unmatched/late/duplicate reply — drop

        PendingQuery pq = it.value();
        m_pending.erase(it);

        QByteArray reply = data;
        reply[0] = static_cast<char>((pq.originalTxId >> 8) & 0xFF);
        reply[1] = static_cast<char>(pq.originalTxId & 0xFF);
        m_clientSocket->writeDatagram(reply, pq.clientAddr, pq.clientPort);

        CacheEntry ce;
        ce.rawResponse = reply;
        ce.expiresAtMs = m_clock.elapsed() + kCacheTtlMs;
        m_cache.insert(pq.domain + "|" + pq.qtype, ce);
    }
}

void DnsProxyServer::sendNxDomain(const QByteArray &originalQuery, const QHostAddress &to, quint16 port) {
    // Build a minimal, valid NXDOMAIN response: reuse the client's own
    // header+question section verbatim (ID, QNAME, QTYPE, QCLASS all
    // match exactly what they asked), just flip it into a response.
    QByteArray resp = originalQuery;
    if (resp.size() < 12) return;

    quint8 flagsHi = 0x81; // QR=1 (response), Opcode=0, AA=0, TC=0, RD=1 (mirror request's RD, assume set)
    quint8 flagsLo = 0x83; // RA=1, Z=0, RCODE=3 (NXDOMAIN)
    resp[2] = static_cast<char>(flagsHi);
    resp[3] = static_cast<char>(flagsLo);
    // ANCOUNT/NSCOUNT/ARCOUNT stay 0 — no records, just the error code.
    resp[6] = 0; resp[7] = 0;
    resp[8] = 0; resp[9] = 0;
    resp[10] = 0; resp[11] = 0;

    m_clientSocket->writeDatagram(resp, to, port);
}

void DnsProxyServer::relayFromCache(const CacheEntry &entry, quint16 desiredTxId, const QHostAddress &to, quint16 port) {
    QByteArray reply = entry.rawResponse;
    if (reply.size() < 2) return;
    reply[0] = static_cast<char>((desiredTxId >> 8) & 0xFF);
    reply[1] = static_cast<char>(desiredTxId & 0xFF);
    m_clientSocket->writeDatagram(reply, to, port);
}

void DnsProxyServer::onCleanupTick() {
    qint64 now = m_clock.elapsed();

    // Retry once against the secondary upstream if the primary hasn't
    // answered within the timeout; drop entirely past the max age.
    for (auto it = m_pending.begin(); it != m_pending.end(); ) {
        qint64 age = now - it->sentAtMs;
        if (age > kPendingMaxAgeMs) {
            it = m_pending.erase(it);
        } else if (age > kUpstreamTimeoutMs && !it->triedFallback && !m_upstream2.isNull()) {
            it->triedFallback = true;
            m_upstreamSocket->writeDatagram(it->forwardPacket, m_upstream2, 53);
            ++it;
        } else {
            ++it;
        }
    }

    for (auto it = m_cache.begin(); it != m_cache.end(); ) {
        if (it->expiresAtMs <= now) it = m_cache.erase(it);
        else ++it;
    }
}

} // namespace core
