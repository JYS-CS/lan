#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QLineEdit>
#include <QVariantList>
#include <QVariantAnimation>
#include <QPainter>
#include "../core/NetworkManager.h"

namespace gui {

// Lists every device currently in the blacklist (DatabaseManager's
// blacklist table, mirrored into the firewall + DHCP server) and lets
// an admin unblock them. This is the only place devices can be
// unblocked from — blocking happens from the Devices page context menu.
class BlockedDevicesPage : public QWidget {
    Q_OBJECT
public:
    explicit BlockedDevicesPage(core::NetworkManager *nm, QWidget *parent = nullptr);

public slots:
    void refresh();

private slots:
    void onBlockedDevicesReady(const QVariantList &entries);
    void onSearchChanged(const QString &text);
    void onItemChanged(QTableWidgetItem *item);
    void onGatewayModeChanged(bool active);

private:
    void setupUi();
    void applyTheme();
    QWidget *createStatCard(const QString &label, const QString &color, QLabel **valuePtr);

    core::NetworkManager *m_nm;

    QLabel *m_statTotal;
    QTableWidget *m_table;
    QLabel *m_footerLiveDot;
    QLabel *m_footerCountLabel;
    QLineEdit *m_searchEdit;
    QPushButton *m_refreshBtn;
    QVariantAnimation *m_spinAnim;
    bool m_populating = false;
    QLabel *m_dhcpWarningLabel;
};

} // namespace gui
