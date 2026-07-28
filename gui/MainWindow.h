#pragma once

#include <QMainWindow>
#include <QToolBar>
#include <QDockWidget>
#include <QLabel>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QStackedWidget>
#include "DeviceTable.h"
#include "DeviceMonitorPage.h"
#include "../core/NetworkManager.h"
#include "DHCPPage.h"
#include "IPCalculatorPage.h"
#include "TrafficPage.h"

namespace gui { 
    class IPCalculatorPage; 
    class DeviceMonitorPage;
    class DHCPPage;
    class TrafficPage;
}

namespace gui {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    virtual ~MainWindow();

private slots:
    void onRefreshRequested();
    void handleScanError(const QString &message);
    void updateStatusBar(const QString &message);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void setupUI();
    void setupToolBar();
    void setupStatusBar();
    void updateDhcpBadge(const QString &status);

    core::NetworkManager *m_networkManager;
    QThread m_networkThread;

    QStackedWidget *m_centralStacked = nullptr;
    DeviceMonitorPage *m_monitorPage = nullptr;
    IPCalculatorPage *m_ipCalcPage = nullptr;
    DHCPPage *m_dhcpPage = nullptr;
    TrafficPage *m_trafficPage = nullptr;

    // Custom UI Components
    QWidget        *m_customToolBar  = nullptr;
    class QButtonGroup *m_navGroup   = nullptr;
    QLabel         *m_dhcpBadge      = nullptr;
    QLabel         *m_statusTextLabel = nullptr;

    QLabel       *m_interfaceLabel = nullptr;
    QTimer       *m_refreshTimer;
};

} // namespace gui
