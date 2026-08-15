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
#include "RouterPage.h"
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
    // Context Menu / Expansion Logic
    connect(m_monitorPage->getDeviceTable(), &gui::DeviceTable::aliasRequested, m_networkManager, &core::NetworkManager::updateDeviceAlias);
    connect(m_monitorPage->getDeviceTable(), &gui::DeviceTable::whitelistRequested, m_networkManager, &core::NetworkManager::addWhitelistedMAC);
    
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
    logoIcon->setStyleSheet(
        "background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #4f7fff, stop:0.5 #4f7fff, "
        "stop:0.5 #ff9142, stop:1 #ff9142); border-radius: 5px;");
    QLabel *logoText = new QLabel("LAN Monitor", this);
    logoText->setStyleSheet("font-size: 13px; font-weight: bold; color: #e8eaf0;");
    hLayout->addWidget(logoIcon);
    hLayout->addWidget(logoText);
    hLayout->addSpacing(8);
    hLayout->addWidget(createDivider());

    m_navGroup = new QButtonGroup(this);
    m_navGroup->setExclusive(true);

    auto createNavBtn = [this](QString text, QString iconPath, int pageIndex) {
        QPushButton *btn = new QPushButton(Theme::tintedIcon(iconPath, 16, Theme::AccentBlue), text, this);
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
        btn->setIcon(Theme::tintedIcon(iconPath, 16, Theme::AccentBlue));
        btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        btn->setPopupMode(QToolButton::InstantPopup);
        btn->setFixedHeight(26);
        btn->setStyleSheet("QToolButton { border-radius: 6px; font-size: 12px; padding: 0 10px; color: #7c8299; font-weight: 500; } "
                           "QToolButton:hover { background: rgba(255,255,255,0.05); color: #e8eaf0; } "
                           "QToolButton::menu-indicator { width: 0px; }");
        
        QMenu *m = new QMenu(btn);
        m->setStyleSheet("QMenu { background: #0d1117; border: 1px solid rgba(79,127,255,0.25); border-radius: 6px; padding: 4px; } "
                         "QMenu::item { padding: 6px 20px 6px 30px; border-radius: 4px; color: #7c8299; } "
                         "QMenu::item:selected { background: rgba(79,127,255,0.15); color: #4f7fff; }");
        
        for (const auto &item : items) {
            // DHCP-related entries get the orange half of the duotone, matching the startup wizard
            bool isDhcp = std::get<0>(item).contains("DHCP", Qt::CaseInsensitive);
            QAction *act = m->addAction(Theme::tintedIcon(std::get<1>(item), 15, isDhcp ? Theme::AccentOrange : Theme::AccentBlue), std::get<0>(item));
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

    // 4. ROUTER button
    hLayout->addWidget(createNavBtn("Router", ":/resources/router.svg", 5));
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



    // Apply Global Toolbar Style
    m_customToolBar->setStyleSheet(
        "QWidget#Toolbar { background-color: #0d1117; border-bottom: 1px solid rgba(79,127,255,0.15); }"
        "QPushButton { color: #8b949e; border-radius: 6px; font-size: 12px; padding: 0 9px; }"
        "QPushButton:hover { background: rgba(79,127,255,0.1); color: #4f7fff; }"
        "QPushButton:checked { background: rgba(79,127,255,0.08); border: 1px solid rgba(79,127,255,0.35); "
        "   border-bottom: 2px solid #ff9142; color: #4f7fff; font-weight: bold; }"
        "QPushButton#ScanBtn { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #4f7fff, stop:1 #6d5cff); "
        "   color: white; font-size: 11px; font-weight: bold; padding: 0 12px; border-radius: 4px; }"
        "QPushButton#ScanBtn:hover { background: #3d6ef0; }"
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
