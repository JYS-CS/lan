#include "DeviceMonitorPage.h"
#include "Theme.h"
#include <QRegularExpression>
#include <QAction>
#include <QFontDatabase>

namespace gui {

DeviceMonitorPage::DeviceMonitorPage(core::NetworkManager *networkManager, QWidget *parent) 
    : QWidget(parent), m_networkManager(networkManager) {
    setupUi();
    applyTheme();
    
    connect(m_deviceTable, &DeviceTable::deviceSelected, this, &DeviceMonitorPage::onSelectionChanged);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &DeviceMonitorPage::onSearchChanged);

    if (m_networkManager) {
        connect(m_networkManager, &core::NetworkManager::dhcpStatusUpdate, this, &DeviceMonitorPage::updateGatewayStatus);
        updateGatewayStatus(m_networkManager->getDHCPManager() && m_networkManager->getDHCPManager()->isServerRunning());
    }
    
    m_lastUpdate = QDateTime::currentDateTime();
    m_footerTimer = new QTimer(this);
    connect(m_footerTimer, &QTimer::timeout, this, [this]() {
        m_liveDotState = !m_liveDotState;
        m_footerLiveDot->setStyleSheet(m_liveDotState ? 
            "background-color: #34e4a0; border-radius: 4px;" : 
            "background-color: rgba(52,228,160,0.3); border-radius: 4px;");
        
        qint64 secs = m_lastUpdate.secsTo(QDateTime::currentDateTime());
        m_lastUpdatedLabel->setText(QString("updated %1s ago").arg(secs));
    });
    m_footerTimer->start(1000);
}

void DeviceMonitorPage::setupUi() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 1. Header Bar
    QWidget *headerBar = new QWidget(this);
    headerBar->setObjectName("HeaderBar");
    QHBoxLayout *headerLayout = new QHBoxLayout(headerBar);
    headerLayout->setContentsMargins(24, 20, 24, 20);
    
    QVBoxLayout *titleLayout = new QVBoxLayout();
    titleLayout->setSpacing(2);
    
    QLabel *eyebrow = new QLabel("LOCAL SEGMENT · DEVICE MAP", this);
    eyebrow->setStyleSheet("color: #7c8798; font-family: 'JetBrains Mono', monospace; font-size: 10px; font-weight: bold; letter-spacing: 0.15em;");
    
    QLabel *title = new QLabel("Network Operations", this);
    title->setStyleSheet("color: #dbe4ee; font-size: 22px; font-weight: bold; font-family: 'Inter', sans-serif;");
    
    titleLayout->addWidget(eyebrow);
    titleLayout->addWidget(title);
    
    headerLayout->addLayout(titleLayout);
    headerLayout->addStretch();
    
    m_topologyWidget = new TopologyWidget(this);
    headerLayout->addWidget(m_topologyWidget);
    
    mainLayout->addWidget(headerBar);

    // 2. Stat Strip
    QWidget *statStrip = new QWidget(this);
    statStrip->setObjectName("StatStrip");
    QHBoxLayout *statLayout = new QHBoxLayout(statStrip);
    statLayout->setContentsMargins(24, 0, 24, 20);
    statLayout->setSpacing(16);
    
    statLayout->addWidget(createStatCard("DEVICES ONLINE", "#34e4a0", &m_onlineCount));
    statLayout->addWidget(createStatCard("TOTAL UPLOAD", "#4f7fff", &m_uploadTotal));
    statLayout->addWidget(createStatCard("TOTAL DOWNLOAD", "#4f7fff", &m_downloadTotal));
    statLayout->addWidget(createStatCard("UNKNOWN VENDORS", "#f5a623", &m_unknownCount));
    
    mainLayout->addWidget(statStrip);

    // 3. Toolbar Row
    QWidget *toolbar = new QWidget(this);
    toolbar->setObjectName("Toolbar");
    QHBoxLayout *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(24, 0, 24, 16);
    toolbarLayout->setSpacing(16);
    
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Filter by IP, MAC, Hostname...");
    m_searchEdit->setFixedWidth(280);
    m_searchEdit->setObjectName("SearchEdit");
    QAction *searchIcon = new QAction(Theme::tintedIcon(":/resources/search.svg", 16, Theme::OpsTextDim), "", m_searchEdit); 
    m_searchEdit->addAction(searchIcon, QLineEdit::LeadingPosition);
    
    toolbarLayout->addWidget(m_searchEdit);
    toolbarLayout->addStretch();
    
    // Legend
    auto createLegendItem = [this](const QString &color, const QString &text) {
        QWidget *w = new QWidget(this);
        QHBoxLayout *l = new QHBoxLayout(w);
        l->setContentsMargins(0,0,0,0);
        l->setSpacing(6);
        QLabel *dot = new QLabel();
        dot->setFixedSize(8,8);
        dot->setStyleSheet(QString("background-color: %1; border-radius: 4px;").arg(color));
        QLabel *lbl = new QLabel(text);
        lbl->setStyleSheet("color: #7c8798; font-size: 11px; font-family: 'Inter', sans-serif;");
        l->addWidget(dot);
        l->addWidget(lbl);
        return w;
    };
    
    toolbarLayout->addWidget(createLegendItem("#34e4a0", "Online/Self"));
    toolbarLayout->addWidget(createLegendItem("#5eead4", "Gateway"));
    toolbarLayout->addWidget(createLegendItem("#f5a623", "Unknown Vendor"));
    
    mainLayout->addWidget(toolbar);

    // 4. Main Table
    QWidget *tableContainer = new QWidget(this);
    tableContainer->setObjectName("TableContainer");
    QVBoxLayout *tableLayout = new QVBoxLayout(tableContainer);
    tableLayout->setContentsMargins(24, 0, 24, 0);
    
    m_deviceTable = new DeviceTable(this);
    tableLayout->addWidget(m_deviceTable);
    
    mainLayout->addWidget(tableContainer, 1);

    // 5. Footer Strip
    QWidget *footerBar = new QWidget(this);
    footerBar->setObjectName("FooterBar");
    QHBoxLayout *footerLayout = new QHBoxLayout(footerBar);
    footerLayout->setContentsMargins(24, 12, 24, 12);
    
    m_footerLiveDot = new QLabel(this);
    m_footerLiveDot->setFixedSize(8,8);
    m_footerLiveDot->setStyleSheet("background-color: #34e4a0; border-radius: 4px;");
    
    m_lastUpdatedLabel = new QLabel("updated 0s ago", this);
    m_lastUpdatedLabel->setStyleSheet("color: #7c8798; font-size: 11px; font-family: 'Inter', sans-serif;");
    
    m_totalHostCountLabel = new QLabel("0 HOSTS SCANNED", this);
    m_totalHostCountLabel->setStyleSheet("color: #4d5666; font-size: 10px; font-weight: bold; font-family: 'JetBrains Mono', monospace; letter-spacing: 0.1em;");
    
    footerLayout->addWidget(m_footerLiveDot);
    footerLayout->addWidget(m_lastUpdatedLabel);
    footerLayout->addStretch();
    footerLayout->addWidget(m_totalHostCountLabel);
    
    mainLayout->addWidget(footerBar);
}

QWidget* DeviceMonitorPage::createStatCard(const QString &label, const QString &color, QLabel **countPtr) {
    QWidget *card = new QWidget(this);
    card->setObjectName("StatCard");
    QVBoxLayout *l = new QVBoxLayout(card);
    l->setContentsMargins(16, 16, 16, 16);
    l->setSpacing(4);
    
    QLabel *lbl = new QLabel(label, this);
    lbl->setStyleSheet(QString("font-family: 'JetBrains Mono', monospace; font-size: 10px; font-weight: bold; color: %1; letter-spacing: 0.1em; background: transparent;").arg(color));
    
    *countPtr = new QLabel("0", this);
    (*countPtr)->setStyleSheet("font-family: 'JetBrains Mono', monospace; font-size: 24px; font-weight: 300; color: #dbe4ee; background: transparent;");
    
    l->addWidget(lbl);
    l->addWidget(*countPtr);
    
    return card;
}

qreal DeviceMonitorPage::parseBw(const QString &bwStr) {
    QRegularExpression re(R"(^([\d\.]+)\s*([KMGB]*)/s)");
    QRegularExpressionMatch match = re.match(bwStr);
    if (!match.hasMatch()) return 0.0;
    qreal val = match.captured(1).toDouble();
    QString unit = match.captured(2);
    if (unit == "K" || unit == "KB") val *= 1024;
    else if (unit == "M" || unit == "MB") val *= 1024 * 1024;
    else if (unit == "G" || unit == "GB") val *= 1024 * 1024 * 1024;
    return val;
}

QString DeviceMonitorPage::formatBw(qreal bytesPerSec) {
    if (bytesPerSec < 1024) return QString::number((int)bytesPerSec) + " B/s";
    if (bytesPerSec < 1024*1024) return QString::number(bytesPerSec / 1024.0, 'f', 1) + " KB/s";
    return QString::number(bytesPerSec / (1024.0*1024.0), 'f', 1) + " MB/s";
}

void DeviceMonitorPage::updateDevices(const QList<core::Device> &devices) {
    m_deviceTable->updateDevices(devices);
    
    m_lastUpdate = QDateTime::currentDateTime();
    
    int online = 0;
    int unknown = 0;
    qreal totalUp = 0;
    qreal totalDown = 0;
    
    for (const auto &d : devices) {
        QString s = d.status().toLower();
        if (s.contains("online") || s.contains("self")) online++;
        if (d.vendor().toLower().contains("unknown")) unknown++;
        
        totalUp += parseBw(d.upBandwidth());
        totalDown += parseBw(d.downBandwidth());
    }
    
    m_onlineCount->setText(QString::number(online));
    m_unknownCount->setText(QString::number(unknown));
    m_uploadTotal->setText(formatBw(totalUp));
    m_downloadTotal->setText(formatBw(totalDown));
    
    m_totalHostCountLabel->setText(QString("%1 HOSTS SCANNED").arg(devices.size()));
}

void DeviceMonitorPage::updateGatewayStatus(bool active) {
    m_topologyWidget->setMode(active);
}

void DeviceMonitorPage::onSearchChanged(const QString &text) {
    m_deviceTable->filterDevices(text);
}

void DeviceMonitorPage::onSelectionChanged(const QString &ip) {
    // Optionally show a detailed panel or log it
    Q_UNUSED(ip);
}

void DeviceMonitorPage::onExportRequested() {
    // Handled in parent
}

void DeviceMonitorPage::onRefreshRequested() {
    QMetaObject::invokeMethod(m_networkManager, "runScan", Qt::QueuedConnection);
}

void DeviceMonitorPage::applyTheme() {
    setStyleSheet(
        "gui--DeviceMonitorPage { background-color: #0a0d12; }"
        "QWidget#HeaderBar { background-color: transparent; border-bottom: 1px solid #1c232c; }"
        "QWidget#StatStrip { background-color: transparent; }"
        "QWidget#Toolbar { background-color: transparent; }"
        "QWidget#TableContainer { background-color: transparent; }"
        "QWidget#FooterBar { background-color: #0f141b; border-top: 1px solid #1c232c; }"
        
        "QWidget#StatCard { background-color: #0f141b; border: 1px solid #1c232c; border-radius: 8px; }"
        
        "QLineEdit#SearchEdit { background: #0f141b; border: 1px solid #1c232c; border-radius: 6px; padding: 8px 10px; color: #dbe4ee; font-size: 13px; font-family: 'Inter', sans-serif; }"
        "QLineEdit#SearchEdit:focus { border-color: #34e4a0; background: #12181f; }"
    );
}

} // namespace gui
