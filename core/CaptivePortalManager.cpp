#include "CaptivePortalManager.h"
#include <QDebug>

namespace core {

CaptivePortalManager::CaptivePortalManager(QObject *parent) : QObject(parent) {
    m_dns = new DnsResponder(this);
    m_http = new BlockHttpServer(this);
}

CaptivePortalManager::~CaptivePortalManager() {
    setEnabled(false);
}

void CaptivePortalManager::setEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    
    if (enabled) {
        if (!m_serverIp.isEmpty()) {
            m_dns->start(m_serverIp);
            m_http->start();
        }
    } else {
        m_dns->stop();
        m_http->stop();
    }
    
    m_enabled = enabled;
    emit statusChanged(m_enabled);
}

void CaptivePortalManager::setServerIp(const QString &ip) {
    m_serverIp = ip;
    if (m_enabled) {
        m_dns->stop();
        m_dns->start(m_serverIp);
    }
}

void CaptivePortalManager::setDeviceBlockPage(const QString &mac, bool enabled) {
    QString lMac = mac.toLower();
    if (enabled) {
        m_deviceOverrides.insert(lMac);
    } else {
        m_deviceOverrides.remove(lMac);
    }
    emit deviceStateChanged(lMac, enabled);
}

bool CaptivePortalManager::shouldShowBlockPage(const QString &mac) const {
    // If global is on, everyone sees it.
    // If global is off, only overrides see it.
    return m_enabled || m_deviceOverrides.contains(mac.toLower());
}

} // namespace core
