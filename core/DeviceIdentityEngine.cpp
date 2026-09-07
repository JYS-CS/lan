#include "DeviceIdentityEngine.h"
#include "DatabaseManager.h"
#include <QUuid>
#include <QRegularExpression>

namespace core {

DeviceIdentityEngine::DeviceIdentityEngine(QObject *parent) : QObject(parent) {}

QString DeviceIdentityEngine::fingerprintOf(const QString &vendorClass, const QString &paramRequestList) {
    if (vendorClass.isEmpty() && paramRequestList.isEmpty()) return {};
    return vendorClass.toLower() + "|" + paramRequestList;
}

bool DeviceIdentityEngine::isGenericHostname(const QString &hostname) {
    if (hostname.isEmpty()) return true;
    static const QStringList generic = {"unknown", "android", "localhost", "device", "*"};
    QString h = hostname.toLower().trimmed();
    return generic.contains(h);
}

// Many DHCP clients set option 61 to type-byte 0x01 followed by the MAC
// itself — in that case it carries no information beyond the MAC and is
// useless for surviving a MAC change. Detect and ignore that case.
bool DeviceIdentityEngine::clientIdLooksMacDerived(const QString &clientId, const QString &mac) {
    if (clientId.isEmpty()) return true;
    QString cidHex = clientId;
    cidHex.remove(':');
    QString macHex = mac;
    macHex.remove(':');
    // type byte (usually 01) + the MAC bytes
    return cidHex.toLower().endsWith(macHex.toLower());
}

void DeviceIdentityEngine::observe(const QString &networkId, const QString &mac, const QString &hostname,
                                    const QString &vendorClass, const QString &clientId, const QString &paramRequestList) {
    if (networkId.isEmpty() || mac.isEmpty()) return;
    QString lMac = mac.toLower();

    auto &db = DatabaseManager::instance();
    QString fp = fingerprintOf(vendorClass, paramRequestList);
    bool hasUsableHostname = !isGenericHostname(hostname);
    bool hasStableClientId = !clientIdLooksMacDerived(clientId, lMac);

    // Try the strongest signal first: a genuinely stable client identifier.
    QString identityId;
    QString reason;
    if (hasStableClientId) {
        identityId = db.findIdentityByClientId(networkId, clientId);
        if (!identityId.isEmpty()) reason = "matched a stable DHCP Client Identifier";
    }
    // Fall back to fingerprint + hostname (only if both are meaningful —
    // neither a blank fingerprint nor a generic hostname is trustworthy
    // enough on its own to avoid false-correlating two different devices).
    if (identityId.isEmpty() && !fp.isEmpty() && hasUsableHostname) {
        identityId = db.findIdentityByFingerprint(networkId, fp, hostname);
        if (!identityId.isEmpty()) reason = "matched device fingerprint (vendor class + requested options) and hostname";
    }

    if (!identityId.isEmpty()) {
        QString previousMac = db.mostRecentMacForIdentity(networkId, identityId);
        db.linkMacToIdentity(networkId, identityId, lMac);
        if (!previousMac.isEmpty() && previousMac != lMac) {
            emit possibleMacRotation(networkId, previousMac, lMac, identityId, reason);
        }
        return;
    }

    // No match — this is either a genuinely new device, or one we don't
    // have enough signal to correlate (generic hostname, no fingerprint,
    // MAC-derived client id). Either way, record it as a fresh identity so
    // future MAC changes *can* be correlated once it has a real hostname.
    QString newId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    db.createIdentity(networkId, newId, fp, hasUsableHostname ? hostname : QString(), hasStableClientId ? clientId : QString());
    db.linkMacToIdentity(networkId, newId, lMac);
}

} // namespace core
