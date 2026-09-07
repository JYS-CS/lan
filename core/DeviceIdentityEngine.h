#pragma once

#include <QObject>
#include <QString>

namespace core {

// Best-effort correlation of a device's persistent "identity" across MAC
// address changes (iOS/Android per-network private-address rotation being
// the main real-world case).
//
// IMPORTANT CAVEAT, stated plainly rather than overpromising: DHCP option
// 60 (vendor class) and option 55 (parameter request list) only fingerprint
// the device *model and OS* — e.g. "an iPhone on iOS 17" — not the
// individual physical unit. Two identical phones on the same network look
// identical on those signals alone. This engine therefore only treats a
// new MAC as "the same device as before" when that fingerprint AND the
// hostname both match (hostnames are usually device-specific, e.g.
// "Johns-iPhone"), or when a genuinely stable, non-MAC-derived DHCP Client
// Identifier matches on its own. This is a heuristic:
//   - False negative: a device that also changes its hostname won't be
//     recognized as the same identity.
//   - False positive (rarer): two identical phones sharing the exact same
//     generic hostname could be merged incorrectly — for this reason,
//     empty/generic hostnames are never used for correlation.
class DeviceIdentityEngine : public QObject {
    Q_OBJECT
public:
    explicit DeviceIdentityEngine(QObject *parent = nullptr);

public slots:
    // Call for every DHCP lease seen. networkId scopes identities to a
    // given gateway (same convention as the blacklist/whitelist), so
    // identities never leak across different networks.
    void observe(const QString &networkId, const QString &mac, const QString &hostname,
                 const QString &vendorClass, const QString &clientId, const QString &paramRequestList);

signals:
    // Fired when a MAC is newly correlated to an *existing* identity that
    // was previously seen under a different MAC — i.e. suspected MAC
    // rotation. previousMac is the most recently seen MAC for that
    // identity before this one.
    void possibleMacRotation(const QString &networkId, const QString &previousMac,
                              const QString &newMac, const QString &identityId, const QString &reason);

private:
    static QString fingerprintOf(const QString &vendorClass, const QString &paramRequestList);
    static bool isGenericHostname(const QString &hostname);
    static bool clientIdLooksMacDerived(const QString &clientId, const QString &mac);
};

} // namespace core
