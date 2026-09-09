#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QTimer>
#include <QNetworkAccessManager>

namespace core {

// Fetches and periodically refreshes a domain-based malware/phishing/ad
// blocklist, handing parsed domains off to DnsProxyServer for DNS-level
// filtering. Source: StevenBlack/hosts (unified list) — the standard,
// widely-used combined hosts-file blocklist. Complements
// ThreatIntelManager: that one blocks by destination IP (catches
// hardcoded-IP malware), this one blocks by domain name at resolution
// time (catches everything that does a DNS lookup, before a connection
// is even attempted).
class DnsBlocklistManager : public QObject {
    Q_OBJECT
public:
    explicit DnsBlocklistManager(QObject *parent = nullptr);

    bool isEnabled() const { return m_enabled; }
    int entryCount() const { return m_entryCount; }
    QDateTime lastUpdated() const { return m_lastUpdated; }

public slots:
    void setEnabled(bool enabled);
    void refreshNow();

signals:
    void refreshStarted();
    void blocklistUpdated(const QStringList &domains);
    void refreshFailed(const QString &error);
    void statusChanged();

private slots:
    void onReplyFinished();

private:
    QNetworkAccessManager *m_nam;
    QTimer *m_refreshTimer;
    bool m_enabled = false;
    int m_entryCount = 0;
    QDateTime m_lastUpdated;

    static constexpr const char *kBlocklistUrl =
        "https://raw.githubusercontent.com/StevenBlack/hosts/master/hosts";
};

} // namespace core
