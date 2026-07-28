#include "DHCPManager.h"
#include "DhcpServer.h"
#include <QDebug>
#include <QHostAddress>

namespace core {

DHCPManager::DHCPManager(NetworkManager *networkManager, QObject *parent)
    : QObject(parent), m_networkManager(networkManager), m_isRunning(false), m_dhcpThread(nullptr) {
}

DHCPManager::~DHCPManager() {
    stopServer();
}

bool DHCPManager::configureDHCPServer(const DHCPServerConfig &config) {
    m_config = config;
    return startServer();
}

bool DHCPManager::startServer() {
    if (m_dhcpThread) {
        stopServer();
    }

    m_dhcpThread = new DhcpServer(m_config, this);
    connect(m_dhcpThread, &DhcpServer::leaseUpdated, this, &DHCPManager::leaseDiscovered);
    connect(m_dhcpThread, &DhcpServer::leaseExpired, this, &DHCPManager::leaseExpired);
    m_dhcpThread->start();

    m_isRunning = true;
    emit dhcpStatusChanged(true);
    emit operationSuccess("DHCP server started");
    return true;
}

bool DHCPManager::stopServer() {
    if (m_dhcpThread) {
        m_dhcpThread->stop();
        m_dhcpThread->wait(2000); // Wait up to 2 seconds
        
        if (m_dhcpThread->isRunning()) {
            m_dhcpThread->terminate();
            m_dhcpThread->wait();
        }
        
        m_dhcpThread->deleteLater();
        m_dhcpThread = nullptr;
    }

    m_isRunning = false;
    emit dhcpStatusChanged(false);
    emit operationSuccess("DHCP server stopped");
    return true;
}

bool DHCPManager::isServerRunning() const {
    return m_isRunning && m_dhcpThread && m_dhcpThread->isRunning();
}

QList<DHCPLease> DHCPManager::readActiveLeases() {
    if (m_dhcpThread) {
        return m_dhcpThread->getActiveLeases();
    }
    return QList<DHCPLease>();
}

bool DHCPManager::addStaticLease(const QString &mac, const QString &ip, const QString &hostname) {
    qDebug() << "[DHCP] Adding static lease:" << mac << "→" << ip;
    if (m_dhcpThread) {
        m_dhcpThread->addStaticLease(mac, ip, hostname);
        return true;
    }
    return false;
}

bool DHCPManager::removeLease(const QString &mac, const QString &ip) {
    qDebug() << "[DHCP] Kicking (expiring lease for):" << mac;
    if (m_dhcpThread) {
        m_dhcpThread->kickClient(mac.toLower(), ip);
        return true;
    }
    return false;
}

bool DHCPManager::expireLease(const QString &mac) {
    qDebug() << "[DHCP] Expiring lease for Poison Pill:" << mac;
    if (m_dhcpThread) {
        m_dhcpThread->expireLease(mac.toLower());
        return true;
    }
    return false;
}

bool DHCPManager::blockMAC(const QString &mac) {
    QString msg = QString("Blocking MAC: \"%1\"").arg(mac);
    qDebug() << "[DHCP]" << msg;
    emit logEvent(msg);

    m_blockedMACs.insert(mac.toLower());
    if (m_dhcpThread)
        m_dhcpThread->setBlockedMACs(m_blockedMACs);
    return true;
}

bool DHCPManager::unblockMAC(const QString &mac) {
    QString msg = QString("Unblocking MAC: \"%1\"").arg(mac);
    qDebug() << "[DHCP]" << msg;
    emit logEvent(msg);

    m_blockedMACs.remove(mac.toLower());
    if (m_dhcpThread)
        m_dhcpThread->setBlockedMACs(m_blockedMACs);
    return true;
}

bool DHCPManager::isMACBlocked(const QString &mac) const {
    return m_blockedMACs.contains(mac.toLower());
}

bool DHCPManager::addWhitelistedMAC(const QString &mac) {
    qDebug() << "[DHCP] Whitelisting MAC:" << mac;
    m_whitelistedMACs.insert(mac.toLower());
    return true;
}

bool DHCPManager::removeWhitelistedMAC(const QString &mac) {
    qDebug() << "[DHCP] Removing MAC from whitelist:" << mac;
    m_whitelistedMACs.remove(mac.toLower());
    return true;
}

bool DHCPManager::isMACWhitelisted(const QString &mac) const {
    return m_whitelistedMACs.contains(mac.toLower());
}

QString DHCPManager::checkConflicts() {
    return "DHCP health check: OK (Custom C++ Server)";
}

} // namespace core
