#include "DHCPPage.h"
#include "StaticLeaseDialog.h"
#include "StartupModePage.h"
#include "Theme.h"
#include <QHeaderView>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <QScrollBar>
#include <QStyle>
#include <QDebug>
#include <QDialog>
#include <QTextEdit>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>

namespace gui {

static void showErrorModal(QWidget *parent, const QString &title, const QString &msg) {
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setFixedSize(380, 180);
    dialog.setStyleSheet(
        "QDialog { background-color: #12151f; }"
        "QLabel { color: #ff5c5c; font-size: 14px; font-weight: 500; margin-bottom: 5px; }"
    );
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 24, 24, 24);
    
    QLabel *lbl = new QLabel(msg, &dialog);
    lbl->setWordWrap(true);
    lbl->setAlignment(Qt::AlignCenter);
    layout->addWidget(lbl, 1);
    
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    QPushButton *closeBtn = new QPushButton("Close", &dialog);
    closeBtn->setObjectName("DangerBtn");
    closeBtn->setCursor(Qt::PointingHandCursor);
    QObject::connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    btnLayout->addWidget(closeBtn);
    btnLayout->addStretch();
    
    layout->addWidget(closeBtn, 0, Qt::AlignCenter);
    Theme::fadeIn(&dialog, 150);
    dialog.exec();
}

static void showSuccessModal(QWidget *parent, const QString &title, const QString &msg) {
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setFixedSize(380, 180);
    dialog.setStyleSheet(
        "QDialog { background-color: #12151f; }"
        "QLabel { color: #2dd98f; font-size: 14px; font-weight: 500; margin-bottom: 5px; }"
    );
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 24, 24, 24);
    
    QLabel *lbl = new QLabel(msg, &dialog);
    lbl->setWordWrap(true);
    lbl->setAlignment(Qt::AlignCenter);
    layout->addWidget(lbl, 1);
    
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    QPushButton *closeBtn = new QPushButton("Awesome", &dialog);
    closeBtn->setStyleSheet("background-color: #2dd98f; color: #12151f; border-radius: 6px; padding: 8px 16px; font-weight: bold; border: none;");
    closeBtn->setCursor(Qt::PointingHandCursor);
    QObject::connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    btnLayout->addWidget(closeBtn);
    btnLayout->addStretch();
    
    layout->addWidget(closeBtn, 0, Qt::AlignCenter);
    Theme::fadeIn(&dialog, 150);
    dialog.exec();
}

static void enableIpAutoFormat(QLineEdit *edit) {
    if (!edit) return;
    QObject::connect(edit, &QLineEdit::textEdited, edit, [edit, lastText = QString()](const QString &t) mutable {
        if (t.length() < lastText.length()) { lastText = t; return; }
        QString res = t;
        QStringList parts = res.split('.');
        if (!parts.isEmpty() && parts.last().length() == 3 && parts.size() < 4) res += '.';
        lastText = res;
        edit->setText(res);
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

DHCPPage::DHCPPage(core::NetworkManager *networkManager, QWidget *parent)
    : QWidget(parent),
      m_networkManager(networkManager),
      m_dhcpManager(networkManager ? networkManager->getDHCPManager() : nullptr) {
    setupUi();
    applyTheme();

    if (m_dhcpManager) {
        connect(m_dhcpManager, &core::DHCPManager::dhcpStatusChanged, this, &DHCPPage::dhcpStatusChanged);
        connect(m_dhcpManager, &core::DHCPManager::operationSuccess,  this, &DHCPPage::onDHCPSuccess);
        connect(m_dhcpManager, &core::DHCPManager::dhcpError,         this, &DHCPPage::onDHCPError);
        connect(m_dhcpManager, &core::DHCPManager::logEvent,          this, &DHCPPage::onDHCPLogEvent);
        connect(m_dhcpManager, &core::DHCPManager::leaseDiscovered,   this, &DHCPPage::onRefreshLeases);
    }

    m_leaseTimer = new QTimer(this);
    connect(m_leaseTimer, &QTimer::timeout, this, &DHCPPage::onRefreshLeases);
    m_leaseTimer->start(2000); // 2 s — fast enough for snappy updates without hammering the mutex

    QTimer::singleShot(100, this, &DHCPPage::autoFillNetworkInfo);
}

// Called by MainWindow when the startup dialog result is DHCPServer
void DHCPPage::setStartupMode(bool intercept) {
    m_interceptCheck->setChecked(intercept);
}

void DHCPPage::applyWizardSettingsAndStart(const gui::DhcpWizardSettings &settings) {
    m_ifaceCombo->setCurrentText(settings.interface);
    m_myIpEdit->setText(settings.hostIp);
    m_gatewayEdit->setText(settings.gatewayIp);
    m_subnetEdit->setText(settings.subnetMask);
    m_rangeStartEdit->setText(settings.rangeStart);
    m_rangeEndEdit->setText(settings.rangeEnd);

    QString dns = settings.dns1;
    if (!settings.dns2.isEmpty()) dns += (dns.isEmpty() ? "" : ", ") + settings.dns2;
    m_dnsEdit->setText(dns);

    m_leaseEdit->setText(QString::number(settings.leaseTimeSeconds / 3600) + "h");
    m_authCheck->setChecked(settings.authoritative);
    m_interceptCheck->setChecked(settings.intercept);

    if (!m_serverActive) startDhcpWithCurrentConfig();
}

// ─────────────────────────────────────────────────────────────────────────────
// UI Construction
// ─────────────────────────────────────────────────────────────────────────────

void DHCPPage::setupUi() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(16);

    // ── Header bar ────────────────────────────────────────────────────────────
    QWidget *headerWidget = new QWidget(this);
    headerWidget->setObjectName("MetricCard");
    QHBoxLayout *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(18, 14, 18, 14);

    QLabel *headerTitle = new QLabel("DHCP Server Dashboard", this);
    headerTitle->setObjectName("CardTitle");

    m_statusLabel = new QLabel("● Stopped", this);

    m_detectBtn = new QPushButton("Detect Network", this);
    m_detectBtn->setObjectName("GhostBtn");
    m_detectBtn->setCursor(Qt::PointingHandCursor);

    m_healthCheckBtn = new QPushButton("Health Check", this);
    m_healthCheckBtn->setObjectName("GhostBtn");
    m_healthCheckBtn->setCursor(Qt::PointingHandCursor);

    m_startStopBtn = new QPushButton("Start Server", this);
    m_startStopBtn->setObjectName("PrimaryBtn");
    m_startStopBtn->setCursor(Qt::PointingHandCursor);

    headerLayout->addWidget(headerTitle);
    headerLayout->addStretch();
    headerLayout->addWidget(m_statusLabel);
    headerLayout->addSpacing(10);
    headerLayout->addWidget(m_detectBtn);
    headerLayout->addSpacing(6);
    headerLayout->addWidget(m_healthCheckBtn);
    headerLayout->addSpacing(6);
    headerLayout->addWidget(m_startStopBtn);

    connect(m_startStopBtn,   &QPushButton::clicked, this, &DHCPPage::onStartStopClicked);
    connect(m_healthCheckBtn, &QPushButton::clicked, this, &DHCPPage::onHealthCheckRequested);
    connect(m_detectBtn,      &QPushButton::clicked, this, &DHCPPage::autoFillNetworkInfo);

    mainLayout->addWidget(headerWidget, 0);

    // ── Config Grid ───────────────────────────────────────────────────────────
    m_configWidget = new QWidget(this);
    m_configWidget->setObjectName("MetricCard");
    QVBoxLayout *configVLayout = new QVBoxLayout(m_configWidget);
    configVLayout->setContentsMargins(18, 16, 18, 16);

    QLabel *configTitle = new QLabel("NETWORK CONFIGURATION", this);
    configTitle->setObjectName("SectionLabel");
    configVLayout->addWidget(configTitle);
    configVLayout->addSpacing(5);

    QGridLayout *gridLayout = new QGridLayout();
    gridLayout->setVerticalSpacing(15);
    gridLayout->setHorizontalSpacing(24);

    auto createLabel = [](const QString &text) {
        QLabel *lbl = new QLabel(text.toUpper());
        lbl->setObjectName("SectionLabel");
        return lbl;
    };
    auto wrapInput = [](QWidget *lbl, QWidget *input) {
        QVBoxLayout *v = new QVBoxLayout();
        v->setSpacing(6);
        v->addWidget(lbl);
        v->addWidget(input);
        return v;
    };

    m_ifaceCombo = new QComboBox(this);
    m_ifaceCombo->setCursor(Qt::PointingHandCursor);
    for (const auto &iface : QNetworkInterface::allInterfaces()) {
        if ((iface.flags() & QNetworkInterface::IsUp) && !(iface.flags() & QNetworkInterface::IsLoopBack)) {
            m_ifaceCombo->addItem(iface.name()); // Only append active interfaces
        }
    }
    m_myIpEdit       = new QLineEdit(this);
    m_myIpEdit->setReadOnly(true);
    m_myIpEdit->setToolTip("Your machine's IP on this interface — read-only.");
    m_gatewayEdit    = new QLineEdit(this);
    enableIpAutoFormat(m_gatewayEdit);

    m_rangeStartEdit = new QLineEdit(this);
    enableIpAutoFormat(m_rangeStartEdit);
    
    m_rangeEndEdit   = new QLineEdit(this);
    enableIpAutoFormat(m_rangeEndEdit);
    
    m_subnetEdit     = new QLineEdit(this);
    enableIpAutoFormat(m_subnetEdit);

    m_dnsEdit  = new QLineEdit("8.8.8.8, 8.8.4.4", this);
    m_leaseEdit= new QLineEdit("24h", this);

    gridLayout->addLayout(wrapInput(createLabel("Interface"),  m_ifaceCombo),      0, 0);
    gridLayout->addLayout(wrapInput(createLabel("Host IP"),    m_myIpEdit),       0, 1);
    gridLayout->addLayout(wrapInput(createLabel("Real Gateway"), m_gatewayEdit),  0, 2);
    gridLayout->addLayout(wrapInput(createLabel("Range Start"),m_rangeStartEdit), 1, 0);
    gridLayout->addLayout(wrapInput(createLabel("Range End"),  m_rangeEndEdit),   1, 1);
    gridLayout->addLayout(wrapInput(createLabel("Subnet Mask"),m_subnetEdit),     1, 2);
    gridLayout->addLayout(wrapInput(createLabel("DNS Servers"),m_dnsEdit),        2, 0, 1, 2);
    gridLayout->addLayout(wrapInput(createLabel("Lease Time"), m_leaseEdit),      2, 2);

    m_authCheck = new QCheckBox("Authoritative (NAK clients using another DHCP)", this);
    m_interceptCheck = new QCheckBox("Intercept all traffic (route through this machine via NAT)", this);
    m_authCheck->setChecked(true);
    m_interceptCheck->setChecked(false);
    QHBoxLayout *checkLayout = new QHBoxLayout();
    checkLayout->setSpacing(24);
    checkLayout->addWidget(m_authCheck);
    checkLayout->addWidget(m_interceptCheck);
    checkLayout->addStretch();
    gridLayout->addLayout(checkLayout, 3, 0, 1, 3);

    configVLayout->addLayout(gridLayout);
    mainLayout->addWidget(m_configWidget, 0);

    // ── Leases Split ──────────────────────────────────────────────────────────

    QWidget *leasesWidget = new QWidget(this);
    QHBoxLayout *leasesLayout = new QHBoxLayout(leasesWidget);
    leasesLayout->setContentsMargins(0, 0, 0, 0);
    leasesLayout->setSpacing(16);

    // Active Leases card
    QWidget *activeCard = new QWidget(this);
    activeCard->setObjectName("MetricCard");
    QVBoxLayout *activeLayout = new QVBoxLayout(activeCard);
    activeLayout->setContentsMargins(18, 16, 18, 16);

    QLabel *activeTitle = new QLabel("ACTIVE LEASES", this);
    activeTitle->setObjectName("SectionLabel");
    activeLayout->addWidget(activeTitle);
    activeLayout->addSpacing(5);

    m_activeLeasesTable = new QTableWidget(0, 4, this);
    m_activeLeasesTable->setHorizontalHeaderLabels({"IP ADDRESS", "MAC ADDRESS", "HOSTNAME", "EXPIRY"});
    m_activeLeasesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_activeLeasesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_activeLeasesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_activeLeasesTable->setShowGrid(false);
    activeLayout->addWidget(m_activeLeasesTable);

    m_feedbackLabel = new QLabel("", this);
    m_feedbackLabel->setObjectName("SectionLabel");
    QHBoxLayout *activeBtnRow = new QHBoxLayout();
    activeBtnRow->addWidget(m_feedbackLabel);
    activeBtnRow->addStretch();
    activeLayout->addLayout(activeBtnRow);

    connect(m_activeLeasesTable, &QTableWidget::itemSelectionChanged,
            this, &DHCPPage::onActiveLeaseSelectionChanged);

    // Static Leases card
    QWidget *staticCard = new QWidget(this);
    staticCard->setObjectName("MetricCard");
    QVBoxLayout *staticLayout = new QVBoxLayout(staticCard);
    staticLayout->setContentsMargins(18, 16, 18, 16);

    QLabel *staticTitle = new QLabel("STATIC LEASES", this);
    staticTitle->setObjectName("SectionLabel");
    staticLayout->addWidget(staticTitle);
    staticLayout->addSpacing(5);

    m_staticLeasesTable = new QTableWidget(0, 3, this);
    m_staticLeasesTable->setHorizontalHeaderLabels({"MAC ADDRESS", "IP ADDRESS", "HOSTNAME"});
    m_staticLeasesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_staticLeasesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_staticLeasesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_staticLeasesTable->setShowGrid(false);
    staticLayout->addWidget(m_staticLeasesTable);

    QHBoxLayout *staticBtnLayout = new QHBoxLayout();
    m_addStaticBtn       = new QPushButton("Add Config...", this);
    m_addStaticBtn->setObjectName("GhostBtn");
    m_removeStaticBtn    = new QPushButton("Remove", this);
    m_removeStaticBtn->setObjectName("GhostBtn");
    m_addStaticBtn->setEnabled(false); // Default to off since server is offline
    m_removeStaticBtn->setEnabled(false);
    staticBtnLayout->addWidget(m_addStaticBtn);
    staticBtnLayout->addWidget(m_removeStaticBtn);
    staticBtnLayout->addStretch();
    staticLayout->addLayout(staticBtnLayout);

    connect(m_addStaticBtn,    &QPushButton::clicked, this, &DHCPPage::onAddStaticLeaseClicked);
    connect(m_removeStaticBtn, &QPushButton::clicked, this, &DHCPPage::onRemoveStaticLeaseClicked);

    leasesLayout->addWidget(activeCard, 3);
    leasesLayout->addWidget(staticCard, 2);
    mainLayout->addWidget(leasesWidget, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Server Start / Stop
// ─────────────────────────────────────────────────────────────────────────────

void DHCPPage::startDhcpWithCurrentConfig() {
    if (!m_dhcpManager) return;

    m_interceptMode = m_interceptCheck->isChecked();

    core::DHCPServerConfig config;
    config.interface    = m_ifaceCombo->currentText();
    config.enabled      = true;
    config.authoritative= m_authCheck->isChecked();
    config.hostMac      = m_networkManager->property("hostMac").toString();
    config.hostIp       = m_myIpEdit->text();  // always self-filter the host laptop

    // Gateway given to clients depends on the mode the user chose
    if (m_interceptMode) {
        // Intercept: clients route their traffic through this machine.
        // Tell the DHCP server to advertise our IP as the gateway.
        config.routerIp = m_myIpEdit->text();

        // Enable kernel IP forwarding so forwarded packets are not silently dropped.
        // We save the previous value so we can restore it exactly on stop.
        QProcess proc;
        proc.start("sh", {"-c", "cat /proc/sys/net/ipv4/ip_forward"});
        proc.waitForFinished();
        m_prevIpForward = proc.readAllStandardOutput().trimmed().toInt();

        QProcess::execute("sh", {"-c", "sysctl -w net.ipv4.ip_forward=1"});
        QProcess::execute("sh", {"-c", "sysctl -w net.ipv4.conf.all.send_redirects=0"});

        // NAT rule: masquerade only forwarded traffic from the DHCP pool range,
        // NOT the laptop's own connections. This keeps the laptop's internet intact.
        // We match on the source range that we hand out to clients.
        const QString poolRange = m_rangeStartEdit->text() + "/"
                                + m_subnetEdit->text();
        // Use nftables for the NAT rule (cleaner, scoped, easy to remove).
        QProcess::execute("sh", {"-c",
            "nft add table ip lan_monitor_nat 2>/dev/null; "
            "nft add chain ip lan_monitor_nat postrouting "
            "  '{ type nat hook postrouting priority 100; }' 2>/dev/null; "
            "nft add rule ip lan_monitor_nat postrouting "
            "  ip saddr != " + m_myIpEdit->text().toUtf8() + " "
            "  oif " + m_ifaceCombo->currentText().toUtf8() + " "
            "  masquerade 2>/dev/null"
        });
        qDebug() << "[DHCP] Intercept mode: ip_forward=1, scoped NAT MASQUERADE enabled";
    } else {
        // Transparent — real router stays as gateway
        config.routerIp = m_gatewayEdit->text();
        m_prevIpForward = -1; // not set by us
    }

    config.rangeStart       = m_rangeStartEdit->text();
    config.rangeEnd         = m_rangeEndEdit->text();
    config.subnetMask       = m_subnetEdit->text();

    QString dnsText = m_dnsEdit->text();
    QStringList dnsList = dnsText.split(",", Qt::SkipEmptyParts);
    if (dnsList.size() > 0) config.dns1 = dnsList[0].trimmed();
    if (dnsList.size() > 1) config.dns2 = dnsList[1].trimmed();

    QString leaseStr = m_leaseEdit->text().toLower();
    int seconds = 86400; // Default to 24 hours if parsing completely fails
    if      (leaseStr.endsWith("h")) seconds = leaseStr.left(leaseStr.length()-1).toInt() * 3600;
    else if (leaseStr.endsWith("m")) seconds = leaseStr.left(leaseStr.length()-1).toInt() * 60;
    else if (leaseStr.endsWith("d")) seconds = leaseStr.left(leaseStr.length()-1).toInt() * 86400;
    else                             seconds = leaseStr.toInt() * 3600; // Treat naked numbers as hours
    config.leaseTimeSeconds = seconds;

    m_statusLabel->setText("Starting DHCP Server...");
    m_dhcpManager->configureDHCPServer(config);
}

void DHCPPage::stopDhcpAndCleanup() {
    if (!m_dhcpManager) return;
    m_dhcpManager->stopServer();

    // Remove the scoped NAT table we added for intercept mode.
    QProcess::execute("sh", {"-c", "nft delete table ip lan_monitor_nat 2>/dev/null"});

    // Only restore ip_forward if WE turned it on — don't touch it otherwise.
    if (m_prevIpForward == 0) {
        QProcess::execute("sh", {"-c", "sysctl -w net.ipv4.ip_forward=0"});
        qDebug() << "[DHCP] Restored ip_forward=0 (was off before we started)";
    }
    QProcess::execute("sh", {"-c", "sysctl -w net.ipv4.conf.all.send_redirects=1"});

    m_prevIpForward = -1;
    m_interceptMode = false;
    qDebug() << "[DHCP] Server stopped. Intercept mode cleaned up.";
}

void DHCPPage::onStartStopClicked() {
    if (!m_dhcpManager) return;
    if (m_serverActive) {
        QDialog dialog(this);
        dialog.setWindowTitle("Stop DHCP Server");
        dialog.setFixedSize(400, 220);
        dialog.setStyleSheet(
            "QDialog { background-color: #12151f; }"
            "QLabel { color: #b5bad0; font-size: 13px; }"
            "QPushButton { background: rgba(79, 127, 255, 0.1); border: 1px solid #4f7fff; "
            "   color: #4f7fff; padding: 6px 16px; border-radius: 8px; font-weight: bold; margin-top: 10px; }"
            "QPushButton#DangerBtn { background: rgba(255, 92, 92, 0.1); border: 1px solid #ff5c5c; color: #ff5c5c; }"
            "QPushButton:hover { background: rgba(79, 127, 255, 0.25); }"
            "QPushButton#DangerBtn:hover { background: rgba(255, 92, 92, 0.25); }"
        );

        QVBoxLayout *layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(24, 24, 24, 24);
        layout->setSpacing(14);

        QLabel *title = new QLabel("Stop DHCP Server?", &dialog);
        title->setStyleSheet("font-size: 18px; font-weight: bold; color: #ffffff;");
        title->setAlignment(Qt::AlignCenter);
        layout->addWidget(title);
        
        QLabel *msg = new QLabel("Active clients and discovered devices may lose\ntheir network configurations or leases.\n\nAre you sure you want to stop?", &dialog);
        msg->setAlignment(Qt::AlignCenter);
        layout->addWidget(msg);
        
        layout->addStretch();

        QHBoxLayout *btnLayout = new QHBoxLayout();
        btnLayout->addStretch();
        
        QPushButton *cancelBtn = new QPushButton("Cancel", &dialog);
        connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
        btnLayout->addWidget(cancelBtn);
        
        QPushButton *stopBtn = new QPushButton("Stop Server", &dialog);
        stopBtn->setObjectName("DangerBtn");
        connect(stopBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
        btnLayout->addWidget(stopBtn);
        
        btnLayout->addStretch();
        layout->addLayout(btnLayout);

        Theme::fadeIn(&dialog, 150);
        if (dialog.exec() == QDialog::Accepted) {
            stopDhcpAndCleanup();
        }
    } else {
        // Validate required fields
        if (m_gatewayEdit->text().isEmpty()) {
            showErrorModal(this, "Missing Gateway",
                "Real Gateway IP is required.\nRun 'Detect Network' or fill it in manually.");
            return;
        }
        startDhcpWithCurrentConfig();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Status / state
// ─────────────────────────────────────────────────────────────────────────────

void DHCPPage::dhcpStatusChanged(bool active) {
    m_serverActive = active;
    bool intercept = m_interceptMode;

    // Hide the config section while the server is running;
    // only show it when the server is stopped so the user can edit settings.
    m_configWidget->setVisible(!active);

    if (active) {
        QString modeTag = intercept ? "  [INTERCEPT]" : "  [TRANSPARENT]";
        m_startStopBtn->setText("Stop Server");
        m_startStopBtn->setObjectName("StopBtn");
        m_statusLabel->setText("● Running" + modeTag);
        m_statusLabel->setStyleSheet(
            "QLabel { background-color: rgba(45,217,143,0.12); color: #2dd98f; "
            "border: 0.5px solid rgba(45,217,143,0.2); border-radius: 12px; "
            "padding: 4px 12px; font-weight: 500; font-size: 11px; margin-right: 15px; }");

        m_detectBtn->setVisible(false);
        m_healthCheckBtn->setVisible(false);
        m_addStaticBtn->setEnabled(true);
        m_removeStaticBtn->setEnabled(true);
    } else {
        m_startStopBtn->setText("Start Server");
        m_startStopBtn->setObjectName("PrimaryBtn");
        m_statusLabel->setText("● Stopped");
        m_statusLabel->setStyleSheet(
            "QLabel { background-color: #1e2230; color: #7c8299; "
            "border: 0.5px solid rgba(255,255,255,0.07); border-radius: 12px; "
            "padding: 4px 12px; font-weight: 500; font-size: 11px; margin-right: 15px; }");

        const QList<QLineEdit*> fields = {m_rangeStartEdit,
            m_rangeEndEdit, m_subnetEdit, m_gatewayEdit, m_dnsEdit, m_leaseEdit};
        for (auto *f : fields) f->setReadOnly(false);
        m_ifaceCombo->setEnabled(true);
        m_authCheck->setEnabled(true);
        m_interceptCheck->setEnabled(true);
        m_detectBtn->setVisible(true);
        m_healthCheckBtn->setVisible(true);
        m_detectBtn->setEnabled(true);
        m_addStaticBtn->setEnabled(false);
        m_removeStaticBtn->setEnabled(false);
    }
    Theme::pulse(m_statusLabel);
    m_startStopBtn->style()->unpolish(m_startStopBtn);
    m_startStopBtn->style()->polish(m_startStopBtn);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lease handling
// ─────────────────────────────────────────────────────────────────────────────

void DHCPPage::onRefreshLeases() {
    if (m_dhcpManager) updateActiveLeases(m_dhcpManager->readActiveLeases());
}

void DHCPPage::updateActiveLeases(const QList<core::DHCPLease> &leases) {
    bool countChanged = (m_activeLeasesTable->rowCount() != leases.size());

    m_activeLeasesTable->setRowCount(0);
    for (int i = 0; i < leases.size(); ++i) {
        m_activeLeasesTable->insertRow(i);
        m_activeLeasesTable->setRowHeight(i, 36);
        auto makeCell = [](const QString &text) {
            auto *item = new QTableWidgetItem(text);
            item->setTextAlignment(Qt::AlignCenter);
            return item;
        };
        m_activeLeasesTable->setItem(i, 0, makeCell(leases[i].ip));
        m_activeLeasesTable->setItem(i, 1, makeCell(leases[i].mac));
        m_activeLeasesTable->setItem(i, 2, makeCell(leases[i].hostname));
        m_activeLeasesTable->setItem(i, 3, makeCell(leases[i].expiry.toString("yyyy/MM/dd hh:mm:ss")));
    }

    if (countChanged) Theme::fadeIn(m_activeLeasesTable);
}

// ─────────────────────────────────────────────────────────────────────────────
// Misc slots
// ─────────────────────────────────────────────────────────────────────────────

void DHCPPage::onHealthCheckRequested() {
    if (!m_dhcpManager) return;
    
    QDialog dialog(this);
    dialog.setWindowTitle("DHCP Health Diagnostics");
    dialog.setFixedSize(400, 220);
    dialog.setStyleSheet(
        "QDialog { background-color: #12151f; }"
        "QLabel { color: #e8eaf0; font-size: 13px; }"
        "QPushButton { background: rgba(79, 127, 255, 0.1); border: 1px solid #4f7fff; "
        "   color: #4f7fff; padding: 6px 16px; border-radius: 8px; font-weight: bold; margin-top: 10px; }"
        "QPushButton:hover { background: rgba(79, 127, 255, 0.25); }"
    );

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(14);

    QLabel *title = new QLabel("Health Check Results", &dialog);
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff;");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);
    
    QString healthText = m_dhcpManager->checkConflicts();
    healthText.replace("OK", "<span style='background-color: rgba(45,217,143,0.15); color: #2dd98f; padding: 2px 6px; border-radius: 4px;'>OK</span>");
    
    QLabel *textLabel = new QLabel(healthText, &dialog);
    textLabel->setTextFormat(Qt::RichText);
    textLabel->setStyleSheet("background-color: #161b26; border: 1px solid rgba(255,255,255,0.08); "
                             "border-radius: 8px; color: #b5bad0; font-family: monospace; font-size: 13px; padding: 12px;");
    textLabel->setAlignment(Qt::AlignCenter);
    textLabel->setWordWrap(true);
    layout->addWidget(textLabel);

    layout->addStretch();

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    QPushButton *closeBtn = new QPushButton("Understood", &dialog);
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    btnLayout->addWidget(closeBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    Theme::fadeIn(&dialog, 200);
    dialog.exec();
}

void DHCPPage::onAddStaticLeaseClicked() {
    QString suggestedIp = "";
    if (m_dhcpManager && m_dhcpManager->isServerRunning()) {
        quint32 start = QHostAddress(m_rangeStartEdit->text()).toIPv4Address();
        quint32 end   = QHostAddress(m_rangeEndEdit->text()).toIPv4Address();
        
        QSet<quint32> usedIps;
        for (const auto& l : m_dhcpManager->readActiveLeases()) usedIps.insert(QHostAddress(l.ip).toIPv4Address());
        for (int i = 0; i < m_staticLeasesTable->rowCount(); ++i) {
            if (m_staticLeasesTable->item(i, 1)) usedIps.insert(QHostAddress(m_staticLeasesTable->item(i, 1)->text()).toIPv4Address());
        }
        
        // Find the absolute first available IP in the subnet pool!
        for (quint32 ip = start; ip <= end; ++ip) {
            if (!usedIps.contains(ip)) {
                suggestedIp = QHostAddress(ip).toString();
                break;
            }
        }
    }

    StaticLeaseDialog dialog("", suggestedIp, "", this);
    if (dialog.exec() == QDialog::Accepted && m_dhcpManager) {
        quint32 ipInt = QHostAddress(dialog.ip()).toIPv4Address();
        quint32 subInt = QHostAddress(m_subnetEdit->text()).toIPv4Address();
        quint32 gwInt = QHostAddress(m_myIpEdit->text()).toIPv4Address();
        
        if (ipInt == 0 || subInt == 0 || (ipInt & subInt) != (gwInt & subInt)) {
            showErrorModal(this, "Invalid IP Address", "The static IP must fall strictly within the active subnetwork block of the DHCP server.");
            return;
        }

        QString proposedMac = dialog.mac().toUpper();
        QString proposedIp = dialog.ip();
        
        for (int i = 0; i < m_staticLeasesTable->rowCount(); ++i) {
            if (m_staticLeasesTable->item(i, 0) && m_staticLeasesTable->item(i, 0)->text().toUpper() == proposedMac) {
                showErrorModal(this, "Duplicate MAC", "This MAC address is already rigidly bound to an existing static lease.");
                return;
            }
            if (m_staticLeasesTable->item(i, 1) && m_staticLeasesTable->item(i, 1)->text() == proposedIp) {
                showErrorModal(this, "Duplicate IP", "This IP address is already permanently reserved by another device in the table.");
                return;
            }
        }

        bool ok = m_dhcpManager->addStaticLease(proposedMac, proposedIp, dialog.hostname());
        if (ok) {
            int row = m_staticLeasesTable->rowCount();
            m_staticLeasesTable->insertRow(row);
            m_staticLeasesTable->setRowHeight(row, 36);
            auto makeCell = [](const QString &t) {
                auto *item = new QTableWidgetItem(t);
                item->setTextAlignment(Qt::AlignCenter);
                return item;
            };
            m_staticLeasesTable->setItem(row, 0, makeCell(proposedMac));
            m_staticLeasesTable->setItem(row, 1, makeCell(proposedIp));
            m_staticLeasesTable->setItem(row, 2, makeCell(dialog.hostname()));
            showSuccessModal(this, "Lease Bound!", "Successfully locked " + dialog.hostname() + "\nto static IP address " + proposedIp + ".");
        } else {
            showErrorModal(this, "Action Failed", "The internal DHCP tracking daemon refused to accept the static lease binding.");
        }
    }
}

void DHCPPage::onRemoveStaticLeaseClicked() {}
void DHCPPage::onActiveLeasesContextMenu(const QPoint &) {}
void DHCPPage::onActiveLeaseSelectionChanged() { m_feedbackLabel->setText(""); }

void DHCPPage::onStatusUpdate(const QString &msg) { m_statusLabel->setText(msg); }
void DHCPPage::onDHCPLogEvent(const QString &msg) { m_statusLabel->setText(msg); }
void DHCPPage::onDHCPError(const QString &msg)    { m_statusLabel->setText(msg); }
void DHCPPage::onDHCPSuccess(const QString &msg)  { m_statusLabel->setText(msg); }

// ─────────────────────────────────────────────────────────────────────────────
// Auto-fill
// ─────────────────────────────────────────────────────────────────────────────

void DHCPPage::autoFillNetworkInfo() {
    if (!m_networkManager) return;
    QString iface = m_networkManager->getActiveInterface();
    if (iface.isEmpty()) {
        m_statusLabel->setText("Error: No active network interface detected");
        return;
    }

    int idx = m_ifaceCombo->findText(iface);
    if (idx >= 0) m_ifaceCombo->setCurrentIndex(idx);
    QHostAddress ip   = m_networkManager->getInterfaceAddress(iface);
    QHostAddress mask = m_networkManager->getInterfaceNetmask(iface);

    if (ip.isNull() || mask.isNull()) {
        m_statusLabel->setText("Error: Could not detect network configuration");
        return;
    }

    m_myIpEdit->setText(ip.toString());
    m_subnetEdit->setText(mask.toString());

    // Detect real gateway
    QProcess proc;
    proc.start("sh", {"-c", QString("ip route show dev %1 | grep default | awk '{print $3}'").arg(iface)});
    proc.waitForFinished();
    QString gw = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    if (!gw.isEmpty()) {
        m_gatewayEdit->setText(gw);
    } else {
        m_gatewayEdit->setText("");
        m_statusLabel->setText("⚠ Gateway not detected — please fill in manually.");
    }

    // Calculate DHCP pool
    quint32 ipInt   = ip.toIPv4Address();
    quint32 maskInt = mask.toIPv4Address();
    quint32 network = ipInt & maskInt;
    quint32 bcast   = network | (~maskInt);

    quint32 poolStart = network + 100;
    quint32 poolEnd   = bcast - 10;
    if (poolStart <= ipInt) poolStart = ipInt + 1;
    if (poolEnd   <= ipInt) poolEnd   = ipInt + 254;

    m_rangeStartEdit->setText(QHostAddress(poolStart).toString());
    m_rangeEndEdit->setText(QHostAddress(poolEnd).toString());

    if (!gw.isEmpty()) {
        m_statusLabel->setText(QString("✓ Network: %1  |  Gateway: %2").arg(ip.toString(), gw));
        m_statusLabel->setStyleSheet(
            "QLabel { background-color: rgba(45,217,143,0.12); color: #2dd98f; "
            "border: 0.5px solid rgba(45,217,143,0.2); border-radius: 12px; "
            "padding: 4px 12px; font-weight: 500; font-size: 11px; margin-right: 15px; }");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Theme
// ─────────────────────────────────────────────────────────────────────────────

void DHCPPage::applyTheme() {
    setStyleSheet(
        // Base
        "DHCPPage { background-color: #111318; }"
        "QWidget#MetricCard { background-color: #181b22; border: 0.5px solid rgba(255,255,255,0.07); border-radius: 10px; }"
        "QLabel { color: #e8eaf0; border: none; }"
        "QLabel#SectionLabel { font-size: 11px; letter-spacing: 0.06em; color: #4a5068; background: transparent; border: none; }"
        "QLabel#CardTitle { font-size: 13px; font-weight: 500; color: #e8eaf0; background: transparent; border: none; }"

        // Mode Select panel
        "QWidget#ModeCard { background-color: #181b22; border: 0.5px solid rgba(255,255,255,0.07); border-radius: 14px; }"
        "QLabel#ModeTitle { font-size: 22px; font-weight: 700; color: #e8eaf0; background: transparent; border: none; }"
        "QLabel#ModeSubtitle { font-size: 13px; color: #5a6175; background: transparent; border: none; }"
        "QLabel#ModeIcon { font-size: 36px; background: transparent; border: none; }"
        "QLabel#ModeCardTitle { font-size: 16px; font-weight: 600; color: #e8eaf0; background: transparent; border: none; }"
        "QLabel#ModeCardDesc { font-size: 12px; color: #6b7db3; line-height: 1.6; background: transparent; border: none; }"

        // Gateway radio cards
        "QWidget#RadioCard { background-color: #1a1e28; border: 0.5px solid rgba(255,255,255,0.08); border-radius: 8px; }"
        "QWidget#RadioCard[selected=true] { border: 1px solid rgba(79,127,255,0.5); background-color: rgba(79,127,255,0.06); }"
        "QLabel#RadioHeading { font-size: 13px; font-weight: 600; color: #d0d4e0; background: transparent; border: none; }"
        "QLabel#RadioDesc { font-size: 11px; color: #5a6175; background: transparent; border: none; }"
        "QRadioButton { color: #e8eaf0; background: transparent; }"
        "QRadioButton::indicator { width: 16px; height: 16px; border-radius: 8px; border: 1.5px solid #4a5068; background: #1a1e28; }"
        "QRadioButton::indicator:checked { background: #4f7fff; border: 1.5px solid #4f7fff; }"

        // Inputs
        "QLineEdit { background-color: #1e2230; color: #e8eaf0; border: 0.5px solid rgba(255,255,255,0.12); border-radius: 6px; padding: 7px 10px; }"
        "QLineEdit:focus { border: 0.5px solid #4f7fff; }"
        "QLineEdit:read-only { color: #4a5068; background: #14171f; }"

        // Buttons
        "QPushButton { border-radius: 6px; font-size: 12px; font-weight: 500; padding: 6px 14px; }"
        "QPushButton#PrimaryBtn { background-color: #4f7fff; color: white; border: none; padding: 8px 16px; }"
        "QPushButton#PrimaryBtn:hover { background-color: #3d6ef0; }"
        "QPushButton#StopBtn { background-color: rgba(240,82,82,0.12); color: #f05252; border: 0.5px solid rgba(240,82,82,0.25); padding: 8px 16px; }"
        "QPushButton#GhostBtn { background-color: transparent; border: 0.5px solid rgba(255,255,255,0.1); color: #7c8299; padding: 8px 14px; }"
        "QPushButton#GhostBtn:hover { background-color: #1e2230; color: #e8eaf0; }"

        // Tables
        "QTableWidget { background-color: #181b22; color: #e8eaf0; gridline-color: transparent; border: none; font-size: 13px; }"
        "QTableWidget::item { border-bottom: 0.5px solid rgba(255,255,255,0.05); padding: 4px; }"
        "QTableWidget::item:selected { background-color: rgba(79,127,255,0.15); }"
        "QHeaderView::section { background-color: #181b22; color: #6b7db3; font-size: 11px; font-weight: 600; letter-spacing: 0.05em; border: none; border-bottom: 0.5px solid rgba(255,255,255,0.1); padding: 10px 4px; qproperty-alignment: AlignCenter; }"
        "QScrollBar:vertical { background: #111318; width: 6px; margin: 0; }"
        "QScrollBar::handle:vertical { background: #1e2230; border-radius: 3px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { border: none; background: none; }"
    );
}

} // namespace gui
