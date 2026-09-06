#include "mainwindow.h"
#include <QVBoxLayout>
#include <QStatusBar>
#include <QGroupBox>
#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("LAN Monitor");
    resize(1000, 600);

    setupToolBar();
    setupCentralTable();
    setupDockWidget();
    setupStatusBar();
    applyStyleSheet();

    // Setup Network Scanner Thread
    m_scanner = new NetworkScanner();
    m_scanner->moveToThread(&m_scannerThread);

    connect(&m_scannerThread, &QThread::finished, m_scanner, &QObject::deleteLater);
    connect(m_scanner, &NetworkScanner::devicesDiscovered, this, &MainWindow::updateDeviceTable);
    connect(m_scanner, &NetworkScanner::scanError, this, &MainWindow::handleScanError);
    connect(m_scanner, &NetworkScanner::statusMessage, this, &MainWindow::updateStatus);

    m_scannerThread.start();

    // Setup Auto-Refresh Timer
    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, m_scanner, &NetworkScanner::runScan);
    m_refreshTimer->start(10000); // 10 seconds

    // Selection Handling
    connect(m_deviceTable, &QTableWidget::itemSelectionChanged, this, &MainWindow::onSelectionChanged);

    // Initial Scan
    QMetaObject::invokeMethod(m_scanner, "runScan", Qt::QueuedConnection);
}

MainWindow::~MainWindow() {
    m_scannerThread.quit();
    m_scannerThread.wait();
}

void MainWindow::setupToolBar() {
    QToolBar *toolBar = addToolBar("Main Toolbar");
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    toolBar->addAction("Refresh Devices", this, &MainWindow::onRefreshDevices);
    toolBar->addSeparator();
    toolBar->addAction("Toggle DHCP", this, &MainWindow::onToggleDHCP);
    toolBar->addAction("Scan Network", this, &MainWindow::onScanNetwork);
    toolBar->addSeparator();
    toolBar->addAction("Clear Logs", this, &MainWindow::onClearLogs);
}

void MainWindow::setupCentralTable() {
    m_deviceTable = new QTableWidget(0, 9, this);
    QStringList headers = { "IP Address", "MAC Address", "Hostname", "Bandwidth Up", "Bandwidth Down", "Status", "Vendor", "Type", "Action" };
    m_deviceTable->setHorizontalHeaderLabels(headers);
    m_deviceTable->setAlternatingRowColors(true);
    m_deviceTable->setSortingEnabled(true);
    m_deviceTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_deviceTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_deviceTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_deviceTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_deviceTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_deviceTable->horizontalHeader()->setSectionResizeMode(8, QHeaderView::ResizeToContents);
    m_deviceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_deviceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    setCentralWidget(m_deviceTable);
}

void MainWindow::setupDockWidget() {
    QDockWidget *dock = new QDockWidget("Controls", this);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    QWidget *dockWidget = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(dockWidget);

    QGroupBox *actionsGroup = new QGroupBox("Device Actions");
    QVBoxLayout *groupLayout = new QVBoxLayout(actionsGroup);

    m_kickBtn = new QPushButton("Kick Selected Device");
    m_blockBtn = new QPushButton("Block Selected Device");
    m_staticBtn = new QPushButton("Add Static Lease");

    // Initially disabled
    m_kickBtn->setEnabled(false);
    m_blockBtn->setEnabled(false);
    m_staticBtn->setEnabled(false);

    groupLayout->addWidget(m_kickBtn);
    groupLayout->addWidget(m_blockBtn);
    groupLayout->addWidget(m_staticBtn);
    groupLayout->addStretch();

    layout->addWidget(actionsGroup);
    layout->addStretch();

    connect(m_kickBtn, &QPushButton::clicked, this, &MainWindow::onKickDevice);
    connect(m_blockBtn, &QPushButton::clicked, this, &MainWindow::onBlockDevice);
    connect(m_staticBtn, &QPushButton::clicked, this, &MainWindow::onAddStaticLease);

    dock->setWidget(dockWidget);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
}

void MainWindow::setupStatusBar() {
    m_statusLabel = new QLabel("Ready", this);
    m_interfaceLabel = new QLabel("Interface: Detecting...", this);
    
    statusBar()->addWidget(m_statusLabel);
    statusBar()->addPermanentWidget(m_interfaceLabel);
}

void MainWindow::updateDeviceTable(const QList<Device> &devices) {
    m_deviceTable->setSortingEnabled(false);
    m_deviceTable->setRowCount(0);
    
    for (const auto& dev : devices) {
        int row = m_deviceTable->rowCount();
        m_deviceTable->insertRow(row);
        m_deviceTable->setItem(row, 0, new QTableWidgetItem(dev.ip));
        m_deviceTable->setItem(row, 1, new QTableWidgetItem(dev.mac.toUpper()));
        m_deviceTable->setItem(row, 2, new QTableWidgetItem(dev.hostname));
        m_deviceTable->setItem(row, 3, new QTableWidgetItem(dev.upBandwidth));
        m_deviceTable->setItem(row, 4, new QTableWidgetItem(dev.downBandwidth));
        m_deviceTable->setItem(row, 5, new QTableWidgetItem(dev.status));
        m_deviceTable->setItem(row, 6, new QTableWidgetItem(dev.vendor));
        
        QString type = "Unknown";
        QString v = dev.vendor.toLower();
        QString h = dev.hostname.toLower();
        if (v.contains("apple") || h.contains("iphone") || h.contains("ipad") || h.contains("mac")) type = "Apple";
        else if (v.contains("android") || v.contains("samsung") || v.contains("xiaomi") || v.contains("huawei") || v.contains("oppo") || v.contains("vivo") || h.contains("android")) type = "Android";
        else if (v.contains("intel") || v.contains("dell") || v.contains("hp") || v.contains("lenovo") || v.contains("microsoft") || v.contains("asus") || v.contains("acer") || v.contains("msi") || h.contains("pc") || h.contains("laptop") || h.contains("desktop")) type = "PC";
        
        m_deviceTable->setItem(row, 7, new QTableWidgetItem(type));
        
        QPushButton* blockBtn = new QPushButton("Block");
        connect(blockBtn, &QPushButton::clicked, this, [this, ip = dev.ip]() {
            this->updateStatus("Blocking device " + ip + "...");
        });
        m_deviceTable->setCellWidget(row, 8, blockBtn);
    }
    m_deviceTable->setSortingEnabled(true);
}

void MainWindow::onSelectionChanged() {
    bool selected = !m_deviceTable->selectedItems().isEmpty();
    m_kickBtn->setEnabled(selected);
    m_blockBtn->setEnabled(selected);
    m_staticBtn->setEnabled(selected);
}

void MainWindow::handleScanError(const QString &message) {
    m_statusLabel->setText("Error: " + message);
    statusBar()->setStyleSheet("QStatusBar { background-color: #b22222; color: white; }");
}

void MainWindow::updateStatus(const QString &message) {
    m_statusLabel->setText(message);
    statusBar()->setStyleSheet("");
}

void MainWindow::applyStyleSheet() {
    setStyleSheet(
        "QMainWindow { background-color: #2b2b2b; color: #ffffff; }"
        "QTableWidget { background-color: #3c3f41; alternate-background-color: #323537; gridline-color: #555555; color: #ffffff; }"
        "QHeaderView::section { background-color: #4e5254; color: #ffffff; padding: 4px; border: 1px solid #555555; font-weight: bold; }"
        "QToolBar { background-color: #3c3f41; border-bottom: 1px solid #555555; }"
        "QPushButton { background-color: #4e5254; border: 1px solid #555555; padding: 5px; min-width: 80px; color: white; }"
        "QPushButton:disabled { background-color: #323537; color: #777777; }"
        "QPushButton:hover { background-color: #5e6264; }"
        "QDockWidget::title { background-color: #3c3f41; padding: 5px; }"
        "QStatusBar { background-color: #3c3f41; color: #aaaaaa; }"
    );
}

void MainWindow::onRefreshDevices() { 
    QMetaObject::invokeMethod(m_scanner, "runScan", Qt::QueuedConnection);
}

void MainWindow::onToggleDHCP() { updateStatus("Toggling DHCP server..."); }
void MainWindow::onScanNetwork() { onRefreshDevices(); }
void MainWindow::onClearLogs() { updateStatus("Logs cleared."); }
void MainWindow::onKickDevice() { updateStatus("Kicking device..."); }
void MainWindow::onBlockDevice() { updateStatus("Blocking device..."); }
void MainWindow::onAddStaticLease() { updateStatus("Adding static lease..."); }
