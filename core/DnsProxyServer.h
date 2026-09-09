#pragma once

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QHash>
#include <QSet>
#include <QString>
#include <QHostAddress>
#include <QElapsedTimer>

namespace core {

// A local, forwarding DNS resolver — this is what makes "which device
// visited what" actually observable, and lets known-bad domains be
// blocked before a connection is even attempted.
//
// How it fits in: when this is enabled, the DHCP server advertises this
// machine's own IP as the primary DNS server (option 6) instead of a
// public resolver. Every client's DNS query then arrives here first. We
// log it, check it against a domain blocklist, and either answer
// NXDOMAIN (blocked) or forward it to a real upstream resolver and relay
// the answer back — exactly the model Pi-hole uses.
//
// Scope note, stated plainly: this implements UDP forwarding, which
// covers essentially all real-world client DNS traffic. It does not
// implement a DNS-over-TCP listener (used for zone transfers and
// occasionally very large/DNSSEC-heavy responses) — a client that falls
// back to TCP will get a connection refused on port 53/tcp here and
// should fail over to its secondary DNS server instead.
class DnsProxyServer : public QObject {
    Q_OBJECT
public:
    explicit DnsProxyServer(QObject *parent = nullptr);
    ~DnsProxyServer();

public slots:
    // localSubnetCidr e.g. "192.168.1.0/24" — queries from outside this
    // range are ignored, so this can never be abused as an open resolver
    // even if something misconfigures external reachability.
    bool start(const QString &listenIp, const QString &localSubnetCidr,
               const QString &upstream1 = "1.1.1.1", const QString &upstream2 = "8.8.8.8");
    void stop();
    bool isRunning() const { return m_running; }

    void setFilteringEnabled(bool enabled);
    void setBlockedDomains(const QStringList &domains);

signals:
    // Emitted for every query, blocked or not — NetworkManager enriches
    // this with the client's MAC (it already tracks that) and persists it.
    void queryObserved(const QString &clientIp, const QString &domain, const QString &qtype, bool blocked, bool cached);

private slots:
    void onClientDatagram();
    void onUpstreamDatagram();
    void onCleanupTick();

private:
    struct PendingQuery {
        quint16 originalTxId;
        QHostAddress clientAddr;
        quint16 clientPort;
        QString domain;
        QString qtype;
        qint64 sentAtMs;
        bool triedFallback = false;   // retried against upstream2 once, if upstream1 timed out
        QByteArray forwardPacket;     // raw packet as sent upstream (tx id already rewritten), kept for the retry
    };
    struct CacheEntry {
        QByteArray rawResponse; // full response, ID field will be rewritten per-client
        qint64 expiresAtMs;
    };

    bool isBlocked(const QString &domain) const;
    bool isRateLimited(const QString &clientIp);
    void sendNxDomain(const QByteArray &originalQuery, const QHostAddress &to, quint16 port);
    void relayFromCache(const CacheEntry &entry, quint16 desiredTxId, const QHostAddress &to, quint16 port);

    static QString parseQName(const QByteArray &data, int &offset);
    static QString qtypeToString(quint16 type);

    QUdpSocket *m_clientSocket   = nullptr;
    QUdpSocket *m_upstreamSocket = nullptr;
    QTimer     *m_cleanupTimer   = nullptr;

    QHostAddress m_upstream1;
    QHostAddress m_upstream2;
    quint32 m_subnetBase = 0;
    quint32 m_subnetMask = 0;

    bool m_running   = false;
    bool m_filtering = true;

    QHash<quint16, PendingQuery> m_pending; // keyed by our randomized upstream tx id
    QHash<QString, CacheEntry> m_cache;     // keyed by "domain|qtype"
    QSet<QString> m_blockedDomains;

    QHash<QString, QPair<int, qint64>> m_rateCounters; // client IP -> {count, windowStartMs}
    static constexpr int kMaxQueriesPerSecond = 60;

    QElapsedTimer m_clock;
};

} // namespace core
