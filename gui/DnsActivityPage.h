#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QTableWidget>
#include <QComboBox>
#include "../core/NetworkManager.h"

namespace gui {

// Per-device DNS browsing visibility — every domain each device has
// looked up, with blocked queries flagged, plus a live feed as new
// queries come in while this page is open. Backed by DnsProxyServer.
class DnsActivityPage : public QWidget {
    Q_OBJECT
public:
    explicit DnsActivityPage(core::NetworkManager *nm, QWidget *parent = nullptr);

private slots:
    void onQueryLogUpdated(const core::DnsLogEntry &entry);
    void refresh();
    void onFilterChanged();

private:
    void setupUi();
    void applyTheme();
    QWidget *createStatCard(const QString &label, const QString &color, QLabel **valuePtr);
    void insertRow(const core::DnsLogEntry &entry, bool atTop);
    bool passesFilter(const core::DnsLogEntry &entry) const;

    core::NetworkManager *m_nm;

    QLabel *m_statTotal;
    QLabel *m_statBlocked;
    QLabel *m_statUnique;

    QLineEdit   *m_searchEdit;
    QComboBox   *m_statusFilter; // All / Allowed / Blocked
    QPushButton *m_refreshBtn;

    QTableWidget *m_table;
    QLabel *m_footerLiveDot;
    QLabel *m_footerLabel;

    QSet<QString> m_uniqueDomains;
    bool m_running = false;
};

} // namespace gui
