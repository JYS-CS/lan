#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include "DnsResponder.h"
#include "BlockHttpServer.h"

namespace core {

class CaptivePortalManager : public QObject {
    Q_OBJECT
public:
    explicit CaptivePortalManager(QObject *parent = nullptr);
    ~CaptivePortalManager();

    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    void setServerIp(const QString &ip);

    // Per-device overrides
    void setDeviceBlockPage(const QString &mac, bool enabled);
    bool shouldShowBlockPage(const QString &mac) const;

    // Access to components for firewall sync
    QSet<QString> getBlockPageMACs() const { return m_deviceOverrides; }

signals:
    void statusChanged(bool enabled);
    void deviceStateChanged(const QString &mac, bool enabled);

private:
    DnsResponder *m_dns;
    BlockHttpServer *m_http;
    
    bool m_enabled = false;
    QString m_serverIp;
    QSet<QString> m_deviceOverrides; // MACs that explicitly want the block page
};

} // namespace core
