#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QListWidget>
#include "../core/NetworkManager.h"

namespace gui {

// IP & Domain Intelligence — like submap.net, scoped to what a LAN admin
// actually needs: look up any domain or IP your devices have contacted
// (from the DNS Activity or Vulnerability pages) and see subdomains,
// reverse DNS, ASN/geolocation, open ports, and threat status.
class IntelLookupPage : public QWidget {
    Q_OBJECT
public:
    explicit IntelLookupPage(core::NetworkManager *nm, QWidget *parent = nullptr);

private slots:
    void onLookupClicked();
    void onStageChanged(const QString &stage);
    void onResolvedIps(const QStringList &ips);
    void onReverseDnsResult(const QString &hostname);
    void onGeoResult(const QString &country, const QString &city, const QString &isp, const QString &asn, const QString &org);
    void onGeoLookupFailed(const QString &error);
    void onSubdomainsFound(const QStringList &subdomains);
    void onPortsFound(const QList<int> &ports);
    void onThreatStatus(bool malicious);
    void onLookupFinished();
    void onLookupFailed(const QString &error);

private:
    void setupUi();
    void applyTheme();
    void resetResults();
    QWidget *createResultCard(const QString &title, QLabel **bodyLabel);

    core::NetworkManager *m_nm;

    QLineEdit   *m_inputEdit;
    QPushButton *m_lookupBtn;
    QLabel      *m_stageLabel;

    QLabel *m_ipsLabel;
    QLabel *m_reverseDnsLabel;
    QLabel *m_geoLabel;
    QLabel *m_portsLabel;
    QLabel *m_threatBadge;
    QListWidget *m_subdomainsList;
    QLabel *m_subdomainsCountLabel;

    QLabel *m_footerLabel;
};

} // namespace gui
