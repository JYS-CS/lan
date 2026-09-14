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
#include "../core/DatabaseManager.h"
#include <QToolButton>
#include "DHCPPage.h"
#include "IPCalculatorPage.h"
#include "DeviceMonitorPage.h"
#include "TrafficPage.h"
#include "PortScanDialog.h"
#include "StartupModePage.h"
#include "RouterPage.h"
#include "BlockedDevicesPage.h"
#include "VulnerabilityPage.h"
#include "TopologyPage.h"
#include "DnsActivityPage.h"
#include "IntelLookupPage.h"
#include "AppSettings.h"
#include "Theme.h"
#include <QButtonGroup>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>

namespace gui {

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("LAN Monitor");
    setMinimumSize(1000, 600);
    resize(1200, 800); // Fallback size
    setWindowState(Qt::WindowMaximized);
    
    this->setStyleSheet("QMainWindow { background-color: #0d1117; } QStackedWidget { background-color: #0d1117; }");

    // Initialize Network Manager in background thread
    m_networkManager = new core::NetworkManager();
    m_networkManager->moveToThread(&m_networkThread);

    setupUI();

    // Wire up signals between Core and GUI
    connect(&m_networkThread, &QThread::finished, m_networkManager, &QObject::deleteLater);
    connect(m_networkManager, &core::NetworkManager::devicesUpdated, m_monitorPage, &DeviceMonitorPage::updateDevices);

    // Strict mode (default-deny for new devices) is a persisted app setting;
    // push its value into NetworkManager on startup and whenever it changes.
    QMetaObject::invokeMethod(m_networkManager, [this]() {
        m_networkManager->setStrictMode(AppSettings::instance()->blockNewDevicesByDefault());
    }, Qt::QueuedConnection);
    connect(AppSettings::instance(), &AppSettings::settingsChanged, this, [this]() {
        bool enabled = AppSettings::instance()->blockNewDevicesByDefault();
        QMetaObject::invokeMethod(m_networkManager, [this, enabled]() {
            m_networkManager->setStrictMode(enabled);
        }, Qt::QueuedConnection);
    });
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
    // Router detection
    connect(m_networkManager, &core::NetworkManager::routerInfoReady,     m_routerPage, &RouterPage::updateInfo);
    connect(m_networkManager, &core::NetworkManager::routerDetectionStage, m_routerPage, &RouterPage::setDetectionStage);
    // Bandwidth page (MAC-keyed, from BandwidthEngine)
    connect(m_networkManager, &core::NetworkManager::bandwidthUpdated,
            m_bandwidthPage, &BandwidthPage::onBandwidthUpdated);
    connect(m_networkManager, &core::NetworkManager::topTalkersUpdated,
            m_bandwidthPage, &BandwidthPage::onTopTalkersUpdated);
    connect(m_networkManager, &core::NetworkManager::lanStatsUpdated,
            m_bandwidthPage, &BandwidthPage::onLanStatsUpdated);
    connect(m_networkManager, &core::NetworkManager::topologyDetected,
            m_bandwidthPage, &BandwidthPage::onTopologyDetected);
    // Context Menu / Expansion Logic
    connect(m_monitorPage->getDeviceTable(), &gui::DeviceTable::aliasRequested, m_networkManager, &core::NetworkManager::updateDeviceAlias);
    connect(m_monitorPage->getDeviceTable(), &gui::DeviceTable::whitelistRequested, m_networkManager, &core::NetworkManager::addWhitelistedMAC);
    connect(m_monitorPage->getDeviceTable(), &gui::DeviceTable::blockRequested, m_networkManager, [this](const QString &mac) {
        m_networkManager->blockDevice(mac);
    });
    connect(m_networkManager, &core::NetworkManager::blockActionFailed, this, [this](const QString &reason) {
        QMessageBox::warning(this, "Cannot Block Device", reason);
    });
    connect(m_networkManager, &core::NetworkManager::deviceBlocked, this, [this](const QString &mac) {
        statusBar()->showMessage("Blocked device " + mac, 4000);
    });
    connect(m_networkManager, &core::NetworkManager::gatewayModeChanged, this, [this](bool active) {
        m_monitorPage->getDeviceTable()->setGatewayModeActive(active);
    });
    
    connect(m_monitorPage->getDeviceTable(), &gui::DeviceTable::portScanRequested, this, [this](const QString &ip) {
        auto *dialog = new gui::PortScanDialog(ip, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });

    // Auto-Refresh Timer — created here but NOT started yet.
    // It starts inside the mode-selection lambdas below, so nothing
    // fires while the startup wizard is still open.
    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::onRefreshRequested);

    // Don't auto-scan yet; wait until mode is selected
}

MainWindow::~MainWindow() {
    m_networkThread.quit();
    m_networkThread.wait();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    qDebug() << "[MainWindow] Clean shutdown — flushing all rules and stopping DHCP...";

    // Stop DHCP server (also removes guard table + scoped NAT via DHCPManager/DHCPPage)
    QMetaObject::invokeMethod(m_networkManager, "stopDHCPServer", Qt::BlockingQueuedConnection);

    // ── Hard-flush every nftables table we may have created ─────────────────
    // All commands use 2>/dev/null so they are silent no-ops when the table
    // doesn't exist (i.e. DHCP was never started, or already cleaned up).
    const QStringList flushCmds = {
        "nft delete table inet lan_monitor 2>/dev/null",
        "nft delete table netdev lan_monitor_layer2 2>/dev/null",
        "nft delete table ip lan_monitor_nat 2>/dev/null",
        "nft delete table inet lan_dhcp_guard 2>/dev/null",
    };
    for (const QString &cmd : flushCmds)
        QProcess::execute("sh", {"-c", cmd});

    // ── Flush stale ARP entries ──────────────────────────────────────────────
    QProcess::execute("sh", {"-c", "ip neigh flush all 2>/dev/null"});

    qDebug() << "[MainWindow] Cleanup complete.";
    QMainWindow::closeEvent(event);
}


void MainWindow::setupUI() {
    setupToolBar();
    m_customToolBar->setVisible(false); // Hidden until mode selected
    
    m_centralStacked = new QStackedWidget(this);
    
    // Page 0: Startup Mode Selection
    auto *startupPage = new StartupModePage(m_networkManager, this);
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

    // Page 5: Router Intelligence Page
    m_routerPage = new RouterPage(m_networkManager, this);
    m_centralStacked->addWidget(m_routerPage);

    // Page 6: Settings Page
    m_settingsPage = new SettingsPage(m_networkManager, this);
    m_centralStacked->addWidget(m_settingsPage);

    // Page 7: Blocked Devices Page
    m_blockedDevicesPage = new BlockedDevicesPage(m_networkManager, this);
    m_centralStacked->addWidget(m_blockedDevicesPage);

    // Page 8: Vulnerability Scanner Page
    m_vulnerabilityPage = new VulnerabilityPage(m_networkManager, this);
    m_centralStacked->addWidget(m_vulnerabilityPage);

    // Page 9: Bandwidth Monitor Page
    m_bandwidthPage = new BandwidthPage(m_networkManager, this);
    m_centralStacked->addWidget(m_bandwidthPage);

    // Page 10: Network Topology Page
    m_topologyPage = new TopologyPage(m_networkManager, this);
    m_centralStacked->addWidget(m_topologyPage);

    // Page 11: DNS Activity Page
    m_dnsActivityPage = new DnsActivityPage(m_networkManager, this);
    m_centralStacked->addWidget(m_dnsActivityPage);

    // Page 12: Intel Lookup Page
    m_intelLookupPage = new IntelLookupPage(m_networkManager, this);
    m_centralStacked->addWidget(m_intelLookupPage);

    connect(m_centralStacked, &QStackedWidget::currentChanged, this, &MainWindow::animatePageChange);

    setCentralWidget(m_centralStacked);
    setupStatusBar();
    statusBar()->setVisible(false); // Hidden until mode selected

    // Wire up mode selection
    connect(startupPage, &StartupModePage::modeSelected, this, [this](StartupModePage::Mode mode, bool intercept) {
        Q_UNUSED(intercept);
        if (mode != StartupModePage::Mode::Normal) return; // DHCP path finishes via dhcpWizardCompleted instead
        m_customToolBar->setVisible(true);
        statusBar()->setVisible(true);
        m_centralStacked->setCurrentIndex(1); // Devices
        if (m_navGroup->button(1)) m_navGroup->button(1)->setChecked(true);
        // Activate NetworkManager (starts sniffer, firewall, timers) then begin scanning
        QMetaObject::invokeMethod(m_networkManager, "activate", Qt::QueuedConnection);
        m_refreshTimer->start(10000);
    });

    connect(startupPage, &StartupModePage::dhcpWizardCompleted, this, [this](const gui::DhcpWizardSettings &settings) {
        m_customToolBar->setVisible(true);
        statusBar()->setVisible(true);
        m_dhcpPage->applyWizardSettingsAndStart(settings);
        m_centralStacked->setCurrentIndex(3); // DHCP
        if (m_navGroup->button(3)) m_navGroup->button(3)->setChecked(true);
        // Activate NetworkManager (starts sniffer, firewall, timers) then begin scanning
        QMetaObject::invokeMethod(m_networkManager, "activate", Qt::QueuedConnection);
        m_refreshTimer->start(10000);
    });
}

void MainWindow::setupToolBar() {
    m_customToolBar = new QWidget(this);
    m_customToolBar->setFixedHeight(64);
    m_customToolBar->setObjectName("Toolbar");
    
    QHBoxLayout *hLayout = new QHBoxLayout(m_customToolBar);
    hLayout->setContentsMargins(16, 0, 16, 0);
    hLayout->setSpacing(8);

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
    logoIcon->setStyleSheet(
        "background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #4f7fff, stop:0.5 #4f7fff, "
        "stop:0.5 #ff9142, stop:1 #ff9142); border-radius: 5px;");
    hLayout->addWidget(logoIcon);
    hLayout->addSpacing(8);
    hLayout->addWidget(createDivider());

    m_navGroup = new QButtonGroup(this);
    m_navGroup->setExclusive(true);

    auto createNavBtn = [this](QString text, QString iconPath, int pageIndex) {
        QPushButton *btn = new QPushButton(Theme::tintedIcon(iconPath, 80, Theme::AccentBlue), "", this);
        btn->setCheckable(true);
        btn->setFixedSize(60, 60);
        btn->setFlat(true);
        btn->setToolTip(text);
        m_navGroup->addButton(btn, pageIndex);
        connect(btn, &QPushButton::clicked, this, [this, pageIndex]() {
            m_centralStacked->setCurrentIndex(pageIndex);
        });
        return btn;
    };

    auto createGroupDropdown = [this](QString text, QString iconPath, QList<std::tuple<QString, QString, int>> items) {
        QWidget *container = new QWidget(this);
        QHBoxLayout *containerLayout = new QHBoxLayout(container);
        containerLayout->setContentsMargins(0, 0, 0, 0);
        containerLayout->setSpacing(0);
        
        QToolButton *btn = new QToolButton(this);
        btn->setIcon(Theme::tintedIcon(iconPath, 24, Theme::AccentBlue));
        btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        btn->setPopupMode(QToolButton::InstantPopup);
        btn->setFixedSize(56, 56);
        btn->setToolTip(text);
        btn->setStyleSheet("QToolButton { border-radius: 8px; border: none; background: transparent; } "
                           "QToolButton:hover { background: rgba(79,127,255,0.12); } "
                           "QToolButton::menu-indicator { width: 0px; }");
        
        // Add small dropdown arrow indicator
        QLabel *arrow = new QLabel("▼", this);
        arrow->setStyleSheet("color: #4f7fff; font-size: 8px; padding: 0; margin: 0;");
        arrow->setFixedSize(10, 10);
        arrow->setAlignment(Qt::AlignCenter);
        
        containerLayout->addWidget(btn);
        containerLayout->addWidget(arrow);
        
        QMenu *m = new QMenu(btn);
        m->setStyleSheet("QMenu { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #0d1117, stop:1 #080c10); border: 1px solid rgba(79,127,255,0.35); border-radius: 10px; padding: 8px; } "
                         "QMenu::item { padding: 10px 28px 10px 36px; border-radius: 8px; color: #8b949e; font-size: 13px; } "
                         "QMenu::item:selected { background: rgba(79,127,255,0.2); color: #4f7fff; border: 1px solid rgba(79,127,255,0.3); }");
        
        for (const auto &item : items) {
            // DHCP-related entries get the orange half of the duotone, matching the startup wizard
            bool isDhcp = std::get<0>(item).contains("DHCP", Qt::CaseInsensitive);
            QAction *act = m->addAction(Theme::tintedIcon(std::get<1>(item), 32, isDhcp ? Theme::AccentOrange : Theme::AccentBlue), std::get<0>(item));
            connect(act, &QAction::triggered, this, [this, item]() {
                m_centralStacked->setCurrentIndex(std::get<2>(item));
            });
        }
        btn->setMenu(m);
        
        // Show menu on hover
        btn->installEventFilter(new HoverMenuFilter(btn, m));
        
        return container;
    };

    // 2. MONITOR Dropdown (Devices | Traffic | Bandwidth)
    hLayout->addWidget(createGroupDropdown("Monitor", ":/resources/monitor.svg", {
        {"Devices",    ":/resources/monitor.svg",  1},
        {"Traffic",    ":/resources/traffic.svg",  2},
        {"Bandwidth",  ":/resources/traffic.svg",  9}
    }));

    // 3. TOOLS Dropdown
    hLayout->addWidget(createGroupDropdown("Tools", ":/resources/tools.svg", {
        {"DHCP", ":/resources/router.svg", 3},
        {"IP Calculator", ":/resources/calculator.svg", 4}
    }));

    // 4. ROUTER button
    hLayout->addWidget(createNavBtn("Router", ":/resources/router.svg", 5));

    // 4b. BLOCKED DEVICES button
    hLayout->addWidget(createNavBtn("Blocked", ":/resources/ban.svg", 7));

    // 4c. VULNERABILITY SCANNER button
    hLayout->addWidget(createNavBtn("Vulnerabilities", ":/resources/warning.svg", 8));

    // 4d. TOPOLOGY button
    hLayout->addWidget(createNavBtn("Topology", ":/resources/topology.svg", 10));
    hLayout->addWidget(createDivider());

    // 4e. DNS ACTIVITY button
    hLayout->addWidget(createNavBtn("DNS Activity", ":/resources/search.svg", 11));
    hLayout->addWidget(createDivider());

    // 4f. INTEL LOOKUP button
    hLayout->addWidget(createNavBtn("Intel Lookup", ":/resources/reference.svg", 12));
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
    hLayout->addSpacing(8);
    hLayout->addWidget(createDivider());
    
    // Settings Button
    hLayout->addWidget(createNavBtn("", ":/resources/settings.svg", 6));



    // Apply Global Toolbar Style
    m_customToolBar->setStyleSheet(
        "QWidget#Toolbar { background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #0d1117, stop:1 #080c10); border-bottom: 2px solid rgba(79,127,255,0.15); }"
        "QPushButton { border-radius: 10px; border: 1px solid transparent; background: transparent; }"
        "QPushButton:hover { background: rgba(79,127,255,0.12); border: 1px solid rgba(79,127,255,0.25); }"
        "QPushButton:checked { background: rgba(79,127,255,0.18); border: 1px solid rgba(79,127,255,0.4); }"
        "QToolButton { border-radius: 10px; border: 1px solid transparent; background: transparent; }"
        "QToolButton:hover { background: rgba(79,127,255,0.12); border: 1px solid rgba(79,127,255,0.25); }"
        "QPushButton#ScanBtn { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #4f7fff, stop:1 #6d5cff); "
        "   color: white; font-size: 11px; font-weight: bold; padding: 0 12px; border-radius: 8px; border: 1px solid rgba(79,127,255,0.3); }"
        "QPushButton#ScanBtn:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #3d6ef0, stop:1 #5a4ce8); }"
    );

    setMenuWidget(m_customToolBar);
}

void MainWindow::setupStatusBar() {
    m_statusTextLabel = new QLabel("● DHCP initialization · 0.0.0.0 · Next scan in --s", this);
    m_statusTextLabel->setStyleSheet("font-size: 11px; color: #8b949e; margin-left: 15px;");
    
    statusBar()->addWidget(m_statusTextLabel);
    statusBar()->setStyleSheet("QStatusBar { background-color: #0d1117; border-top: 1px solid rgba(79,127,255,0.15); }");
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
    statusBar()->setStyleSheet("QStatusBar { background-color: #0d1117; border-top: 1px solid rgba(79,127,255,0.15); }");
    m_statusTextLabel->setStyleSheet("font-size: 11px; color: #4f7fff; margin-left: 15px;");
}

void MainWindow::animatePageChange(int index) {
    QWidget *page = m_centralStacked->widget(index);
    if (!page) return;

    auto *effect = new QGraphicsOpacityEffect(page);
    page->setGraphicsEffect(effect);

    auto *anim = new QPropertyAnimation(effect, "opacity", page);
    anim->setDuration(220);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim, &QPropertyAnimation::finished, effect, [page]() {
        page->setGraphicsEffect(nullptr); // avoid the perf cost of a persistent opacity layer
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

} // namespace gui
