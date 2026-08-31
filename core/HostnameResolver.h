#pragma once

// HostnameResolver.h
// Multi-layer automatic hostname discovery:
//   Layer 1 — mDNS unicast   (PTR query, UDP 5353)
//   Layer 2 — NetBIOS/NBNS   (Node-Status request, UDP 137)
//   Layer 3 — LLMNR          (PTR query, UDP 5355, Windows Vista+)
//   Layer 4 — Reverse DNS    (raw DNS query to local nameserver, UDP 53)
//
// All layers run in PARALLEL per device via four sub-threads.
// resolveAll() runs all devices in a QThreadPool.

#include <QString>
#include <QMap>
#include <QList>
#include <QAtomicInt>

namespace core {

class HostnameResolver {
public:
    HostnameResolver() = default;

    // Resolve a batch of IPs. Returns ip→hostname for IPs that resolved.
    // timeoutMs is the per-layer socket timeout; all 4 layers run in parallel
    // so wall-clock ≈ timeoutMs (not 4×timeoutMs).
    QMap<QString, QString> resolveAll(const QList<QString> &ips, int timeoutMs = 1500);

    // Single-IP convenience wrapper.
    QString resolveOne(const QString &ip, int timeoutMs = 1000);

    // Layer implementations — public so internal helper classes can call them.
    static QString tryMDNS      (const QString &ip, int timeoutMs);
    static QString tryNBNS      (const QString &ip, int timeoutMs);
    static QString tryLLMNR     (const QString &ip, int timeoutMs);
    static QString tryReverseDNS(const QString &ip, int timeoutMs);
    static QString trySSDPUPnP  (const QString &ip, int timeoutMs); // repeaters, cameras, IoT // raw UDP/53, no getnameinfo

    static QString stripLocal(const QString &name);
};

} // namespace core
