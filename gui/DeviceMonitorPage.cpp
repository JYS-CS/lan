#include "DeviceMonitorPage.h"
#include <QHeaderView>
#include <QLineEdit>
#include <QIcon>
#include <QAction>

namespace gui {

DeviceMonitorPage::DeviceMonitorPage(core::NetworkManager *networkManager, QWidget *parent) 
    : QWidget(parent), m_networkManager(networkManager) {
    setupUi();
    applyTheme();
    
    connect(m_deviceTable, &DeviceTable::deviceSelected, this, &DeviceMonitorPage::onSelectionChanged);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &DeviceMonitorPage::onSearchChanged);
    connect(m_refreshBtn, &QPushButton::clicked, this, &DeviceMonitorPage::onRefreshRequested);
    connect(m_exportBtn, &QPushButton::clicked, this, &DeviceMonitorPage::onExportRequested);
    
    // (device actions are wired through MainWindow)

    if (m_networkManager) {
        connect(m_networkManager, &core::NetworkManager::dhcpStatusUpdate, this, &DeviceMonitorPage::updateGatewayStatus);
        // Initial state
        updateGatewayStatus(m_networkManager->getDHCPManager() && m_networkManager->getDHCPManager()->isServerRunning());
    }
}

void DeviceMonitorPage::setupUi() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 1. Action Bar (Top)
    QWidget *actionBar = new QWidget(this);
    actionBar->setObjectName("ActionBar");
    QHBoxLayout *hLayout = new QHBoxLayout(actionBar);
    hLayout->setContentsMargins(20, 12, 20, 12);
    hLayout->setSpacing(15);
    
    // Logo & Title
    QLabel *icon = new QLabel(this);
    icon->setPixmap(QIcon(":/resources/monitor.svg").pixmap(18, 18));
    QLabel *title = new QLabel("Device Monitor", this);
    title->setStyleSheet("font-size: 14px; font-weight: 500; color: #e8eaf0;");
    
    hLayout->addWidget(icon);
    hLayout->addWidget(title);
    hLayout->addSpacing(10);

    // Search Box
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Search devices...");
    m_searchEdit->setFixedWidth(220);
    
    QAction *searchIcon = new QAction(QIcon(":/resources/search.svg"), "", m_searchEdit); 
    m_searchEdit->addAction(searchIcon, QLineEdit::LeadingPosition);
    
    hLayout->addWidget(m_searchEdit);
    
    hLayout->addStretch();

    // Stats
    hLayout->addWidget(createStatPill("Online", "#2dd98f", &m_onlineCount));
    hLayout->addWidget(createStatPill("Idle", "#f5a623", &m_idleCount));
    hLayout->addWidget(createStatPill("Total", "#7c8299", &m_totalCount));

    // Gateway Active Status
    m_gatewayStatus = new QLabel("GATEWAY PASSIVE", this);
    m_gatewayStatus->setObjectName("GatewayStatus");
    m_gatewayStatus->setAlignment(Qt::AlignCenter);
    m_gatewayStatus->setStyleSheet(
        "QLabel#GatewayStatus { background-color: #1e2230; color: #4a5068; border: 0.5px solid rgba(255,255,255,0.07); "
        "border-radius: 7px; font-size: 9px; font-weight: bold; padding: 5px 12px; letter-spacing: 0.05em; }"
    );
    hLayout->addWidget(m_gatewayStatus);
    
    hLayout->addSpacing(10);

    // Action Buttons
    m_refreshBtn = new QPushButton("Refresh", this);
    m_refreshBtn->setObjectName("GhostBtn");
    
    m_exportBtn = new QPushButton("Export CSV", this);
    m_exportBtn->setObjectName("PrimaryBtn");
    
    hLayout->addWidget(m_refreshBtn);
    hLayout->addWidget(m_exportBtn);
    
    mainLayout->addWidget(actionBar);

    // 2. Table
    m_deviceTable = new DeviceTable(this);
    mainLayout->addWidget(m_deviceTable);

    // 3. Status Bar (Bottom)
    QWidget *statusBar = new QWidget(this);
    statusBar->setObjectName("StatusBar");
    QHBoxLayout *statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(20, 8, 20, 8);
    
    m_hintLabel = new QLabel("Right-click a device for more options", this);
    m_hintLabel->setStyleSheet("font-size: 11px; color: #4a5068;");
    
    m_selLabel = new QLabel("No selection", this);
    m_selLabel->setStyleSheet("font-size: 11px; color: #4a5068;");
    
    statusLayout->addWidget(m_hintLabel);
    statusLayout->addStretch();
    statusLayout->addWidget(m_selLabel);
    
    mainLayout->addWidget(statusBar);
}

QWidget* DeviceMonitorPage::createStatPill(const QString &label, const QString &color, QLabel **countPtr) {
    QWidget *pill = new QWidget(this);
    pill->setObjectName("StatPill");
    QHBoxLayout *l = new QHBoxLayout(pill);
    l->setContentsMargins(12, 5, 12, 5);
    l->setSpacing(8);
    
    QWidget *dot = new QWidget();
    dot->setFixedSize(7, 7);
    dot->setStyleSheet(QString("background-color: %1; border-radius: 3px;").arg(color));
    
    *countPtr = new QLabel("0", this);
    (*countPtr)->setStyleSheet("font-size: 13px; font-weight: 600; color: #e8eaf0; background: transparent;");
    
    QLabel *lbl = new QLabel(label.toUpper(), this);
    lbl->setStyleSheet("font-size: 9px; font-weight: bold; color: #4a5068; letter-spacing: 0.05em; background: transparent;");
    
    l->addWidget(dot);
    l->addWidget(*countPtr);
    l->addWidget(lbl);
    
    return pill;
}

void DeviceMonitorPage::updateDevices(const QList<core::Device> &devices) {
    m_deviceTable->updateDevices(devices);
    
    int online = 0;
    int idle = 0;
    for (const auto &d : devices) {
        QString s = d.status().toLower();
        if (s.contains("online")) online++;
        else if (s.contains("idle")) idle++;
    }
    
    m_onlineCount->setText(QString::number(online));
    m_idleCount->setText(QString::number(idle));
    m_totalCount->setText(QString::number(devices.size()));
}

void DeviceMonitorPage::updateGatewayStatus(bool active) {
    if (active) {
        m_gatewayStatus->setText("GATEWAY ACTIVE");
        m_gatewayStatus->setStyleSheet(
            "QLabel#GatewayStatus { background-color: rgba(45,217,143,0.12); color: #2dd98f; "
            "border: 0.5px solid rgba(45,217,143,0.25); border-radius: 7px; font-size: 9px; "
            "font-weight: bold; padding: 5px 12px; letter-spacing: 0.05em; }"
        );
    } else {
        m_gatewayStatus->setText("GATEWAY PASSIVE");
        m_gatewayStatus->setStyleSheet(
            "QLabel#GatewayStatus { background-color: #1e2230; color: #4a5068; "
            "border: 0.5px solid rgba(255,255,255,0.07); border-radius: 7px; font-size: 9px; "
            "font-weight: bold; padding: 5px 12px; letter-spacing: 0.05em; }"
        );
    }
}

void DeviceMonitorPage::onSearchChanged(const QString &text) {
    m_deviceTable->filterDevices(text);
}

void DeviceMonitorPage::onSelectionChanged(const QString &ip) {
    m_selLabel->setText(ip + " selected");
}

void DeviceMonitorPage::onExportRequested() {
    m_hintLabel->setText("Exporting current view to CSV...");
}

void DeviceMonitorPage::onRefreshRequested() {
    QMetaObject::invokeMethod(m_networkManager, "runScan", Qt::QueuedConnection);
}

void DeviceMonitorPage::applyTheme() {
    setStyleSheet(
        "gui--DeviceMonitorPage { background-color: #111318; }"
        "QWidget#ActionBar { background-color: #181b22; border-bottom: 0.5px solid rgba(255,255,255,0.07); }"
        "QWidget#StatusBar { background-color: #181b22; border-top: 0.5px solid rgba(255,255,255,0.07); }"
        "QWidget#StatPill { background: #1e2230; border: 0.5px solid rgba(255,255,255,0.07); border-radius: 7px; }"
        
        "QLineEdit { background: #1e2230; border: 0.5px solid rgba(255,255,255,0.12); border-radius: 6px; padding: 7px 10px; color: #e8eaf0; font-size: 12px; }"
        "QLineEdit:focus { border-color: #4f7fff; }"
        
        "QPushButton { border-radius: 6px; font-size: 12px; font-weight: 500; padding: 7px 15px; border: none; }"
        "QPushButton#PrimaryBtn { background: #4f7fff; color: #fff; }"
        "QPushButton#PrimaryBtn:hover { background: #6b90ff; }"
        "QPushButton#GhostBtn:hover { color: #e8eaf0; }"
        
        "QTableWidget { background: transparent; border: none; gridline-color: transparent; color: #e8eaf0; }"
        "QTableWidget::item { border-bottom: 0.5px solid rgba(255,255,255,0.07); padding-left: 15px; }"
        "QTableWidget::item:hover { background: #1e2230; }"
        "QTableWidget::item:selected { background: rgba(79,127,255,0.15); color: #e8eaf0; }"
        
        "QHeaderView::section { background: #181b22; color: #4a5068; font-size: 10px; font-weight: bold; text-transform: uppercase; letter-spacing: 0.05em; padding: 12px 15px; border: none; border-bottom: 0.5px solid rgba(255,255,255,0.07); }"
        
        "QScrollBar:vertical { background: #111318; width: 6px; margin: 0; }"
        "QScrollBar::handle:vertical { background: #1e2230; border-radius: 3px; min-height: 20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { border: none; background: none; }"
    );
}

} // namespace gui
