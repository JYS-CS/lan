#include "MainWindow.h"
#include <QVBoxLayout>
#include <QStatusBar>
#include <QProcess>
#include <QCloseEvent>
#include <QGroupBox>
#include <QMessageBox>
#include <QIcon>
#include <QNetworkInterface>
#include <QMenu>
#include <QToolButton>
#include "DHCPPage.h"
#include "IPCalculatorPage.h"
#include "DeviceMonitorPage.h"
#include "TrafficPage.h"
#include "PortScanDialog.h"
#include "StartupModePage.h"
#include <QButtonGroup>
#include <QFrame>

namespace gui {

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("LAN Monitor");
    setMinimumSize(1000, 600);
    resize(1200, 800); // Fallback size
    setWindowState(Qt::WindowMaximized);
    
    // Sync theme beautifully with SplashScreen
    this->setStyleSheet("QMainWindow { background-color: #0d1117; } QStackedWidget { background-color: #0d1117; }");

    // Initialize Network Manager in background thread
    m_networkManager = new core::NetworkManager();
    m_networkManager->moveToThread(&m_networkThread);

    setupUI();

    // Wire up signals between Core and GUI
    connect(&m_networkThread, &QThread::finished, m_networkManager, &QObject::deleteLater);
    connect(m_networkManager, &core::NetworkManager::devicesUpdated, m_monitorPage, &DeviceMonitorPage::updateDevices);
    connect(m_networkManager, &core::NetworkManager::scanError, this, &MainWindow::handleScanError);
    connect(m_networkManager, &core::NetworkManager::statusMessage, this, &MainWindow::updateStatusBar);
    connect(m_networkManager, &core::NetworkManager::globalTrafficStatus, this, &MainWindow::updateStatusBar);
    connect(m_networkManager, &core::NetworkManager::dhcpStatusUpdate, this, [this](bool running) {
        updateDhcpBadge(running ? "ACTIVE" : "OFFLINE");
    });
    connect(m_networkManager, &core::NetworkManager::dhcpOperationSuccess, this, &MainWindow::updateStatusBar);
    connect(m_networkManager, &core::NetworkManager::dhcpOperationError,   this, &MainWindow::handleScanError);

    m_networkThread.start();

    // Connect Traffic logic
    connect(m_networkManager, &core::NetworkManager::trafficUpdated, m_trafficPage, &TrafficPage::updateTraffic);
    connect(m_networkManager, &core::NetworkManager::globalTrafficStatsUpdated, m_trafficPage, &TrafficPage::updateGlobalStats);
    // Context Menu / Expansion Logic
    connect(m_monitorPage->getDeviceTable(), &gui::DeviceTable::aliasRequested, m_networkManager, &core::NetworkManager::updateDeviceAlias);
    connect(m_monitorPage->getDeviceTable(), &gui::DeviceTable::whitelistRequested, m_networkManager, &core::NetworkManager::addWhitelistedMAC);
    
    connect(m_monitorPage->getDeviceTable(), &gui::DeviceTable::portScanRequested, this, [this](const QString &ip) {
        auto *dialog = new gui::PortScanDialog(ip, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });

    // Auto-Refresh Timer
    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::onRefreshRequested);
    m_refreshTimer->start(10000);

    // Don't auto-scan yet; wait until mode is selected
}

MainWindow::~MainWindow() {
    m_networkThread.quit();
    m_networkThread.wait();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    // Ensure we fully clean up before the process exits so that:
    // 1. nftables blocking rules don't survive in the kernel
    // 2. Client devices aren't left pointing at us as gateway/DNS
    qDebug() << "[MainWindow] Clean shutdown — flushing nftables and stopping DHCP...";

    // Stop DHCP first so no more leases are handed out
    QMetaObject::invokeMethod(m_networkManager, "stopDHCPServer", Qt::BlockingQueuedConnection);

    // Flush all nftables rules we created — this is blocking and safe here
    QProcess::execute("sh", {"-c", "nft delete table inet lan_monitor 2>/dev/null"});
    QProcess::execute("sh", {"-c", "nft delete table netdev lan_monitor_layer2 2>/dev/null"});
    QProcess::execute("sh", {"-c", "nft delete table ip lan_monitor_nat 2>/dev/null"});

    // Flush our own ARP cache so the OS doesn't keep routing through stale entries
    QProcess::execute("sh", {"-c", "ip neigh flush all 2>/dev/null"});

    qDebug() << "[MainWindow] Cleanup complete.";
    QMainWindow::closeEvent(event);
}

void MainWindow::setupUI() {
    setupToolBar();
    m_customToolBar->setVisible(false); // Hidden until mode selected
    
    m_centralStacked = new QStackedWidget(this);
    
    // Page 0: Startup Mode Selection
    auto *startupPage = new StartupModePage(this);
    m_centralStacked->addWidget(startupPage);

    // Page 1: Device Monitor
    m_monitorPage = new DeviceMonitorPage(m_networkManager, this);
    m_centralStacked->addWidget(m_monitorPage);

    // Page 2: Traffic
    m_trafficPage = new TrafficPage(m_networkManager, this);
    m_centralStacked->addWidget(m_trafficPage);

    // Page 3: DHCP Page
    m_dhcpPage = new DHCPPage(m_networkManager, this);
    m_centralStacked->addWidget(m_dhcpPage);

    // Page 4: IP Calculator Page
    m_ipCalcPage = new IPCalculatorPage(this);
    m_centralStacked->addWidget(m_ipCalcPage);

    setCentralWidget(m_centralStacked);
    setupStatusBar();
    statusBar()->setVisible(false); // Hidden until mode selected

    // Wire up mode selection
    connect(startupPage, &StartupModePage::modeSelected, this, [this](StartupModePage::Mode mode, bool intercept) {
        m_customToolBar->setVisible(true);
        statusBar()->setVisible(true);
        if (mode == StartupModePage::Mode::Normal) {
            m_centralStacked->setCurrentIndex(1); // Devices
            if (m_navGroup->button(1)) m_navGroup->button(1)->setChecked(true);
        } else {
            m_dhcpPage->setStartupMode(intercept);
            m_centralStacked->setCurrentIndex(3); // DHCP
        }
        // Start first scan now
        onRefreshRequested();
    });
}

void MainWindow::setupToolBar() {
    m_customToolBar = new QWidget(this);
    m_customToolBar->setFixedHeight(42);
    m_customToolBar->setObjectName("Toolbar");
    
    QHBoxLayout *hLayout = new QHBoxLayout(m_customToolBar);
    hLayout->setContentsMargins(15, 0, 15, 0);
    hLayout->setSpacing(12);

    auto createDivider = [this]() {
        QFrame *f = new QFrame(this);
        f->setFrameShape(QFrame::VLine);
        f->setFixedWidth(1);
        f->setFixedHeight(16);
        f->setStyleSheet("background-color: rgba(0,0,0,0.12); border: none;");
        return f;
    };

    // 1. Logo Section
    QLabel *logoIcon = new QLabel(this);
    logoIcon->setFixedSize(20, 20);
    logoIcon->setStyleSheet("background-color: #1a6fbf; border-radius: 4px;");
    QLabel *logoText = new QLabel("LAN Monitor", this);
    logoText->setStyleSheet("font-size: 13px; font-weight: bold; color: #e8eaf0;");
    hLayout->addWidget(logoIcon);
    hLayout->addWidget(logoText);
    hLayout->addSpacing(8);
    hLayout->addWidget(createDivider());

    m_navGroup = new QButtonGroup(this);
    m_navGroup->setExclusive(true);

    auto createNavBtn = [this](QString text, QString iconPath, int pageIndex) {
        QPushButton *btn = new QPushButton(QIcon(iconPath), text, this);
        btn->setCheckable(true);
        btn->setFixedHeight(26);
        btn->setFlat(true);
        m_navGroup->addButton(btn, pageIndex);
        connect(btn, &QPushButton::clicked, this, [this, pageIndex]() {
            m_centralStacked->setCurrentIndex(pageIndex);
        });
        return btn;
    };

    auto createGroupDropdown = [this](QString text, QString iconPath, QList<std::tuple<QString, QString, int>> items) {
        QToolButton *btn = new QToolButton(this);
        btn->setText(text);
        btn->setIcon(QIcon(iconPath));
        btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        btn->setPopupMode(QToolButton::InstantPopup);
        btn->setFixedHeight(26);
        btn->setStyleSheet("QToolButton { border-radius: 6px; font-size: 12px; padding: 0 10px; color: #7c8299; font-weight: 500; } "
                           "QToolButton:hover { background: rgba(255,255,255,0.05); color: #e8eaf0; } "
                           "QToolButton::menu-indicator { width: 0px; }");
        
        QMenu *m = new QMenu(btn);
        m->setStyleSheet("QMenu { background: #0d1117; border: 1px solid rgba(0,229,255,0.2); border-radius: 6px; padding: 4px; } "
                         "QMenu::item { padding: 6px 20px 6px 30px; border-radius: 4px; color: #7c8299; } "
                         "QMenu::item:selected { background: rgba(0,229,255,0.15); color: #00e5ff; }");
        
        for (const auto &item : items) {
            QAction *act = m->addAction(QIcon(std::get<1>(item)), std::get<0>(item));
            connect(act, &QAction::triggered, this, [this, item]() {
                m_centralStacked->setCurrentIndex(std::get<2>(item));
            });
        }
        btn->setMenu(m);
        return btn;
    };

    // 2. MONITOR Dropdown
    hLayout->addWidget(createGroupDropdown("Monitor", ":/resources/monitor.svg", {
        {"Devices", ":/resources/monitor.svg", 1}, 
        {"Traffic", ":/resources/traffic.svg", 2}
    }));
    hLayout->addWidget(createDivider());

    // 3. TOOLS Dropdown
    hLayout->addWidget(createGroupDropdown("Tools", ":/resources/tools.svg", {
        {"DHCP", ":/resources/router.svg", 3}, 
        {"IP Calculator", ":/resources/calculator.svg", 4}
    }));
    hLayout->addWidget(createDivider());

    // Will select button when mode is chosen, but add one just in case
    if (m_navGroup->button(1)) m_navGroup->button(1)->setChecked(true);

    hLayout->addStretch();

    // 5. Right Side
    m_dhcpBadge = new QLabel("DHCP off", this);
    m_dhcpBadge->setStyleSheet(
        "QLabel { background: #1e2230; color: #4a5068; border-radius: 5px; "
        "padding: 2px 10px; font-size: 11px; font-weight: bold; "
        "border: 0.5px solid rgba(255,255,255,0.1); }"
    );
    hLayout->addWidget(m_dhcpBadge);
    
    hLayout->addWidget(createDivider());

    QPushButton *refreshBtn = new QPushButton(QIcon(":/resources/refresh.svg"), "", this);
    refreshBtn->setFixedSize(26, 26);
    refreshBtn->setIconSize(QSize(16, 16));
    refreshBtn->setFlat(true);
    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::onRefreshRequested);
    hLayout->addWidget(refreshBtn);

    QPushButton *settingsBtn = new QPushButton(QIcon(":/resources/settings.svg"), "", this);
    settingsBtn->setFixedSize(26, 26);
    settingsBtn->setIconSize(QSize(16, 16));
    settingsBtn->setFlat(true);
    hLayout->addWidget(settingsBtn);

    QPushButton *scanNow = new QPushButton("Scan now", this);
    scanNow->setObjectName("ScanBtn");
    connect(scanNow, &QPushButton::clicked, this, &MainWindow::onRefreshRequested);
    hLayout->addWidget(scanNow);

    // Apply Global Toolbar Style
    m_customToolBar->setStyleSheet(
        "QWidget#Toolbar { background-color: #0d1117; border-bottom: 1px solid rgba(0,229,255,0.15); }"
        "QPushButton { color: #8b949e; border-radius: 6px; font-size: 12px; padding: 0 9px; }"
        "QPushButton:hover { background: rgba(0,229,255,0.1); color: #00e5ff; }"
        "QPushButton:checked { background: rgba(0,229,255,0.05); border: 1px solid rgba(0,229,255,0.3); color: #00e5ff; font-weight: bold; }"
        "QPushButton#ScanBtn { background: #00e5ff; color: #0d1117; font-size: 11px; font-weight: bold; padding: 0 12px; border-radius: 4px; }"
        "QPushButton#ScanBtn:hover { background: #00cce6; }"
    );

    setMenuWidget(m_customToolBar);
}

void MainWindow::setupStatusBar() {
    m_statusTextLabel = new QLabel("● DHCP initialization · 0.0.0.0 · Next scan in --s", this);
    m_statusTextLabel->setStyleSheet("font-size: 11px; color: #8b949e; margin-left: 15px;");
    
    statusBar()->addWidget(m_statusTextLabel);
    statusBar()->setStyleSheet("QStatusBar { background-color: #0d1117; border-top: 1px solid rgba(0,229,255,0.15); }");
}

void MainWindow::updateDhcpBadge(const QString &status) {
    if (status == "Running") {
        m_dhcpBadge->setText("DHCP on");
        m_dhcpBadge->setStyleSheet(
            "QLabel { background: rgba(45,217,143,0.12); color: #2dd98f; border-radius: 5px; "
            "padding: 2px 10px; font-size: 11px; font-weight: bold; "
            "border: 0.5px solid rgba(45,217,143,0.25); }"
        );
    } else {
        m_dhcpBadge->setText("DHCP off");
        m_dhcpBadge->setStyleSheet(
            "QLabel { background: #1e2230; color: #4a5068; border-radius: 5px; "
            "padding: 2px 10px; font-size: 11px; font-weight: bold; "
            "border: 0.5px solid rgba(255,255,255,0.1); }"
        );
    }
}

void MainWindow::onRefreshRequested() {
    QMetaObject::invokeMethod(m_networkManager, "runScan", Qt::QueuedConnection);
}

void MainWindow::handleScanError(const QString &message) {
    m_statusTextLabel->setText("⚠ " + message);
    statusBar()->setStyleSheet("QStatusBar { background-color: rgba(240,82,82,0.15); border-top: 0.5px solid rgba(240,82,82,0.3); }");
    m_statusTextLabel->setStyleSheet("font-size: 11px; color: #f05252; margin-left: 15px;");
}

void MainWindow::updateStatusBar(const QString &message) {
    m_statusTextLabel->setText("● " + message);
    statusBar()->setStyleSheet("QStatusBar { background-color: #0d1117; border-top: 1px solid rgba(0,229,255,0.15); }");
    m_statusTextLabel->setStyleSheet("font-size: 11px; color: #00e5ff; margin-left: 15px;");
}

} // namespace gui
