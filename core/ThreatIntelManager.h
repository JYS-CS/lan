#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QTimer>
#include <QNetworkAccessManager>

namespace core {

// Fetches and periodically refreshes a known-malicious-destination-IP
// blocklist, handing the parsed entries off to FirewallManager for actual
// enforcement. Source: bitwire-it/ipblocklist's outbound.txt — destination
// IPs/CIDRs seen as C2 servers, malware drop sites, and phishing hosts.
// This complements (not replaces) DNS-level visibility — it catches
// malicious contact by IP regardless of protocol, including hardcoded-IP
// malware that never does a DNS lookup at all.
class ThreatIntelManager : public QObject {
    Q_OBJECT
public:
    explicit ThreatIntelManager(QObject *parent = nullptr);

    bool isEnabled() const { return m_enabled; }
    int entryCount() const { return m_entryCount; }
    QDateTime lastUpdated() const { return m_lastUpdated; }
    // Checks the in-memory copy of the same list handed to the firewall —
    // lets other features (like an IP lookup tool) ask "is this address
    // known-malicious?" without touching nftables.
    bool isKnownMalicious(const QString &ip) const;

public slots:
    void setEnabled(bool enabled);
    void refreshNow();

signals:
    void refreshStarted();
    void blocklistUpdated(const QStringList &entries);
    void refreshFailed(const QString &error);
    void statusChanged();

private slots:
    void onReplyFinished();

private:
    struct IpRange { quint32 base; quint32 mask; };

    QNetworkAccessManager *m_nam;
    QTimer *m_refreshTimer;
    bool m_enabled = false;
    int m_entryCount = 0;
    QDateTime m_lastUpdated;
    QList<IpRange> m_ranges;

    static constexpr const char *kBlocklistUrl =
        "https://raw.githubusercontent.com/bitwire-it/ipblocklist/main/outbound.txt";
};

} // namespace core
