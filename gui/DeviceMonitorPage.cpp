#include "DeviceMonitorPage.h"
#include "Theme.h"
#include "AppSettings.h"
#include "../core/DatabaseManager.h"

#include <QRegularExpression>
#include <QFontDatabase>
#include <QGraphicsOpacityEffect>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>

namespace gui {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

DeviceMonitorPage::DeviceMonitorPage(core::NetworkManager *networkManager, QWidget *parent)
    : QWidget(parent), m_networkManager(networkManager)
{
    setupUi();
    applyTheme();
    applySettings();

    connect(AppSettings::instance(), &AppSettings::settingsChanged,
            this, &DeviceMonitorPage::applySettings);
    connect(m_deviceTable, &DeviceTable::deviceSelected,
            this, &DeviceMonitorPage::onSelectionChanged);
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &DeviceMonitorPage::onSearchChanged);

    if (m_networkManager) {
        // Gateway badge / action column gating
        connect(m_networkManager, &core::NetworkManager::gatewayModeChanged,
                this, &DeviceMonitorPage::setGatewayModeActive);

        // Traffic feeds into chart + top talkers
        connect(m_networkManager, &core::NetworkManager::trafficUpdated,
                this, &DeviceMonitorPage::onTrafficUpdated);
        connect(m_networkManager, &core::NetworkManager::globalTrafficStatsUpdated,
                this, &DeviceMonitorPage::onGlobalTrafficStats);

        // Keep blocked count KPI current when blocks change
        connect(m_networkManager, &core::NetworkManager::deviceBlocked,
                this, [this](const QString &) {
                    int n = core::DatabaseManager::instance().getBlacklist().size();
                    if (m_blockedCount) m_blockedCount->setText(QString::number(n));
                });
        connect(m_networkManager, &core::NetworkManager::deviceUnblocked,
                this, [this](const QString &) {
                    int n = core::DatabaseManager::instance().getBlacklist().size();
                    if (m_blockedCount) m_blockedCount->setText(QString::number(n));
                });

        // DHCP status badge (legacy path used by topology widget)
        connect(m_networkManager, &core::NetworkManager::dhcpStatusUpdate,
                this, &DeviceMonitorPage::updateGatewayStatus);
        bool running = m_networkManager->getDHCPManager()
                       && m_networkManager->getDHCPManager()->isServerRunning();
        updateGatewayStatus(running);
    }

    m_lastUpdate = QDateTime::currentDateTime();
    m_footerTimer = new QTimer(this);
    connect(m_footerTimer, &QTimer::timeout, this, &DeviceMonitorPage::tickFooter);
    m_footerTimer->start(1000);
}

// ─────────────────────────────────────────────────────────────────────────────
// UI Construction
// ─────────────────────────────────────────────────────────────────────────────

void DeviceMonitorPage::setupUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ══════════════════════════════════════════════════════════════════════
    // HEADER BAR
    // ══════════════════════════════════════════════════════════════════════
    auto *headerBar = new QWidget(this);
    headerBar->setObjectName("HeaderBar");
    auto *hl = new QHBoxLayout(headerBar);
    hl->setContentsMargins(24, 18, 24, 18);
    hl->setSpacing(10);

    // Title
    auto *titleCol = new QVBoxLayout();
    titleCol->setSpacing(2);
    auto *eyebrow = new QLabel("NETWORK OPERATIONS CENTER", this);
    eyebrow->setStyleSheet(
        "color: #4d5666; font-family: 'JetBrains Mono', monospace; "
        "font-size: 9px; font-weight: bold; letter-spacing: 0.18em;");
    auto *title = new QLabel("LAN Monitor", this);
    title->setStyleSheet(
        "color: #dbe4ee; font-size: 20px; font-weight: bold; font-family: 'Inter', sans-serif;");
    titleCol->addWidget(eyebrow);
    titleCol->addWidget(title);
    hl->addLayout(titleCol);
    hl->addSpacing(20);

    // Gateway badge — hidden until active
    m_gatewayBadge = new QLabel("⬡  GATEWAY ACTIVE", this);
    m_gatewayBadge->setObjectName("GatewayBadge");
    m_gatewayBadge->setVisible(false);
    hl->addWidget(m_gatewayBadge);

    hl->addStretch();

    // Search bar
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName("SearchEdit");
    m_searchEdit->setPlaceholderText("Filter by IP, MAC, hostname…");
    m_searchEdit->setFixedHeight(34);
    m_searchEdit->setMaximumWidth(0);
    m_searchEdit->setMinimumWidth(0);
    m_searchEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_searchAnim = new QPropertyAnimation(m_searchEdit, "maximumWidth", this);
    m_searchAnim->setDuration(280);
    m_searchAnim->setEasingCurve(QEasingCurve::OutCubic);

    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
        if (m_searchEdit->text().isEmpty()) onSearchToggled();
    });

    m_searchBtn = new QPushButton(this);
    m_searchBtn->setObjectName("IconBtn");
    m_searchBtn->setFixedSize(34, 34);
    m_searchBtn->setCursor(Qt::PointingHandCursor);
    m_searchBtn->setToolTip("Search devices");
    m_searchBtn->setIcon(Theme::tintedIcon(":/resources/search.svg", 17, Theme::OpsAccentGreen));
    m_searchBtn->setIconSize(QSize(17, 17));

    m_refreshBtn = new QPushButton(this);
    m_refreshBtn->setObjectName("IconBtn");
    m_refreshBtn->setFixedSize(34, 34);
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    m_refreshBtn->setToolTip("Rescan network");
    m_refreshBtn->setIcon(Theme::tintedIcon(":/resources/refresh.svg", 17, Theme::OpsTextDim));
    m_refreshBtn->setIconSize(QSize(17, 17));

    connect(m_searchBtn,  &QPushButton::clicked, this, &DeviceMonitorPage::onSearchToggled);
    connect(m_refreshBtn, &QPushButton::clicked, this, &DeviceMonitorPage::onRefreshRequested);

    hl->addWidget(m_searchEdit);
    hl->addWidget(m_searchBtn);
    hl->addWidget(m_refreshBtn);
    hl->addSpacing(12);

    // Topology widget (host / gateway / internet indicators)
    m_topologyWidget = new TopologyWidget(this);
    hl->addWidget(m_topologyWidget);

    root->addWidget(headerBar);

    // ══════════════════════════════════════════════════════════════════════
    // KPI STRIP  (stat cards)
    // ══════════════════════════════════════════════════════════════════════
    auto *statStrip = new QWidget(this);
    statStrip->setObjectName("StatStrip");
    auto *sl = new QHBoxLayout(statStrip);
    sl->setContentsMargins(24, 0, 24, 16);
    sl->setSpacing(12);

    sl->addWidget(createStatCard("DEVICES ONLINE",  "#34e4a0", &m_onlineCount));
    sl->addWidget(createStatCard("UNKNOWN VENDORS", "#f5a623", &m_unknownCount));

    // These three cards are DHCP-gateway-only — hidden initially
    m_bwUpCard   = createStatCard("TOTAL UPLOAD ↑",   "#4f7fff", &m_uploadTotal);
    m_bwDownCard = createStatCard("TOTAL DOWNLOAD ↓", "#5eead4", &m_downloadTotal);
    m_blockedCard= createStatCard("BLOCKED DEVICES",  "#ff5c5c", &m_blockedCount);
    m_bwUpCard->setVisible(false);
    m_bwDownCard->setVisible(false);
    m_blockedCard->setVisible(false);
    sl->addWidget(m_bwUpCard);
    sl->addWidget(m_bwDownCard);
    sl->addWidget(m_blockedCard);
    sl->addStretch();

    root->addWidget(statStrip);

    // ══════════════════════════════════════════════════════════════════════
    // ANALYTICS PANEL  (DHCP-gateway-only — hidden until active)
    // ══════════════════════════════════════════════════════════════════════
    m_analyticsPanel = new QWidget(this);
    m_analyticsPanel->setObjectName("AnalyticsPanel");
    m_analyticsPanel->setVisible(false);

    auto *al = new QHBoxLayout(m_analyticsPanel);
    al->setContentsMargins(24, 0, 24, 14);
    al->setSpacing(12);

    m_bandwidthChart   = new BandwidthChartWidget(m_analyticsPanel);
    m_topTalkersWidget = new TopTalkersWidget(m_analyticsPanel);
    m_bandwidthChart->setMinimumHeight(160);
    m_topTalkersWidget->setMinimumHeight(160);

    al->addWidget(m_bandwidthChart, 6);   // 60% width
    al->addWidget(m_topTalkersWidget, 4); // 40% width

    root->addWidget(m_analyticsPanel);

    // ══════════════════════════════════════════════════════════════════════
    // DEVICE TABLE
    // ══════════════════════════════════════════════════════════════════════
    auto *tableWrap = new QWidget(this);
    tableWrap->setObjectName("TableContainer");
    auto *tl = new QVBoxLayout(tableWrap);
    tl->setContentsMargins(24, 0, 24, 0);

    m_deviceTable = new DeviceTable(this);
    tl->addWidget(m_deviceTable);

    root->addWidget(tableWrap, 1);

    // ══════════════════════════════════════════════════════════════════════
    // FOOTER
    // ══════════════════════════════════════════════════════════════════════
    auto *footer = new QWidget(this);
    footer->setObjectName("FooterBar");
    auto *fl = new QHBoxLayout(footer);
    fl->setContentsMargins(24, 10, 24, 10);
    fl->setSpacing(8);

    m_footerLiveDot = new QLabel(this);
    m_footerLiveDot->setFixedSize(8, 8);
    m_footerLiveDot->setStyleSheet("background-color: #34e4a0; border-radius: 4px;");

    m_lastUpdatedLabel = new QLabel("updated 0s ago", this);
    m_lastUpdatedLabel->setStyleSheet(
        "color: #7c8798; font-size: 11px; font-family: 'Inter', sans-serif;");

    // PPS readout — shown when gateway captures traffic
    m_ppsLabel = new QLabel("", this);
    m_ppsLabel->setStyleSheet(
        "color: #4d5666; font-size: 10px; font-family: 'JetBrains Mono', monospace;");
    m_ppsLabel->setVisible(false);

    m_totalHostCountLabel = new QLabel("0 HOSTS SCANNED", this);
    m_totalHostCountLabel->setStyleSheet(
        "color: #4d5666; font-size: 10px; font-weight: bold; "
        "font-family: 'JetBrains Mono', monospace; letter-spacing: 0.1em;");

    fl->addWidget(m_footerLiveDot);
    fl->addSpacing(4);
    fl->addWidget(m_lastUpdatedLabel);
    fl->addSpacing(16);
    fl->addWidget(m_ppsLabel);
    fl->addStretch();
    fl->addWidget(m_totalHostCountLabel);

    root->addWidget(footer);
}

// ─────────────────────────────────────────────────────────────────────────────
// Stat card factory
// ─────────────────────────────────────────────────────────────────────────────

QWidget* DeviceMonitorPage::createStatCard(const QString &label, const QString &color,
                                            QLabel **countPtr, const QString &objName) {
    auto *card = new QWidget(this);
    card->setObjectName(objName);
    auto *l = new QVBoxLayout(card);
    l->setContentsMargins(16, 14, 16, 14);
    l->setSpacing(4);

    auto *lbl = new QLabel(label, this);
    lbl->setStyleSheet(
        QString("font-family: 'JetBrains Mono', monospace; font-size: 9px; font-weight: bold; "
                "color: %1; letter-spacing: 0.12em; background: transparent;").arg(color));

    *countPtr = new QLabel("0", this);
    (*countPtr)->setStyleSheet(
        "font-family: 'JetBrains Mono', monospace; font-size: 22px; font-weight: 300; "
        "color: #dbe4ee; background: transparent;");

    l->addWidget(lbl);
    l->addWidget(*countPtr);
    return card;
}

// ─────────────────────────────────────────────────────────────────────────────
// Gateway mode toggle — the core gating mechanism
// ─────────────────────────────────────────────────────────────────────────────

void DeviceMonitorPage::setGatewayModeActive(bool active) {
    if (m_gatewayActive == active) return;
    m_gatewayActive = active;

    // Analytics panel (chart + top talkers)
    m_analyticsPanel->setVisible(active);
    if (active) Theme::fadeIn(m_analyticsPanel, 300);

    // BW + blocked KPI cards
    m_bwUpCard->setVisible(active);
    m_bwDownCard->setVisible(active);
    m_blockedCard->setVisible(active);

    // Seed blocked count from DB when coming online
    if (active) {
        int n = core::DatabaseManager::instance().getBlacklist().size();
        if (m_blockedCount) m_blockedCount->setText(QString::number(n));
    }

    // Gateway badge
    m_gatewayBadge->setVisible(active);
    if (active) Theme::pulse(m_gatewayBadge);

    // PPS label
    m_ppsLabel->setVisible(active);

    // ACTION column in device table
    m_deviceTable->setGatewayModeActive(active);
}

// ─────────────────────────────────────────────────────────────────────────────
// Data slots
// ─────────────────────────────────────────────────────────────────────────────

void DeviceMonitorPage::updateGatewayStatus(bool active) {
    if (m_topologyWidget) m_topologyWidget->setMode(active);
}

qreal DeviceMonitorPage::parseBw(const QString &bwStr) {
    static const QRegularExpression re(R"(^([\d\.]+)\s*([KMGB]*)/s)");
    QRegularExpressionMatch m = re.match(bwStr);
    if (!m.hasMatch()) return 0.0;
    qreal val = m.captured(1).toDouble();
    QString u = m.captured(2);
    if (u == "K" || u == "KB") val *= 1024;
    else if (u == "M" || u == "MB") val *= 1024 * 1024;
    else if (u == "G" || u == "GB") val *= 1024LL * 1024 * 1024;
    return val;
}

QString DeviceMonitorPage::formatBw(qreal bps) {
    if (bps < 1024)       return QString::number((int)bps) + " B/s";
    if (bps < 1024*1024)  return QString::number(bps/1024.0,       'f', 1) + " KB/s";
    return                       QString::number(bps/(1024.0*1024), 'f', 1) + " MB/s";
}

void DeviceMonitorPage::updateDevices(const QList<core::Device> &devices) {
    m_deviceTable->updateDevices(devices);
    m_lastUpdate = QDateTime::currentDateTime();

    int online = 0, unknown = 0;
    qreal totalUp = 0, totalDown = 0;

    for (const auto &d : devices) {
        QString s = d.status().toLower();
        if (s.contains("online") || s.contains("self")) ++online;
        if (d.vendor().toLower().contains("unknown")) ++unknown;
        totalUp   += parseBw(d.upBandwidth());
        totalDown += parseBw(d.downBandwidth());
    }

    if (m_onlineCount)   m_onlineCount->setText(QString::number(online));
    if (m_unknownCount)  m_unknownCount->setText(QString::number(unknown));
    if (m_uploadTotal)   m_uploadTotal->setText(formatBw(totalUp));
    if (m_downloadTotal) m_downloadTotal->setText(formatBw(totalDown));
    m_totalHostCountLabel->setText(QString("%1 HOSTS SCANNED").arg(devices.size()));

    // Feed top talkers (only meaningful when gateway is on, but widget is hidden anyway)
    if (m_topTalkersWidget) m_topTalkersWidget->updateDevices(devices);
}

void DeviceMonitorPage::onTrafficUpdated(const QMap<QString, core::TrafficStats> &stats) {
    // Aggregate for bandwidth chart
    quint64 upTotal = 0, downTotal = 0;
    for (const auto &ts : stats) {
        upTotal   += ts.currentRateUp;
        downTotal += ts.currentRateDown;
    }
    if (m_bandwidthChart) m_bandwidthChart->addSample(upTotal, downTotal);
}

void DeviceMonitorPage::onGlobalTrafficStats(int /*packetCount*/, double pps,
                                              quint64 /*totalIn*/, quint64 /*totalOut*/) {
    if (m_ppsLabel && m_gatewayActive) {
        m_ppsLabel->setText(QString("● %1 pkts/s").arg((int)pps));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Search
// ─────────────────────────────────────────────────────────────────────────────

void DeviceMonitorPage::onSearchToggled() {
    m_searchOpen = !m_searchOpen;

    m_searchAnim->stop();
    m_searchAnim->setStartValue(m_searchEdit->maximumWidth());
    m_searchAnim->setEndValue(m_searchOpen ? QWIDGETSIZE_MAX : 0);
    m_searchAnim->start();

    if (m_searchOpen) {
        QTimer::singleShot(150, m_searchEdit, [this]() { m_searchEdit->setFocus(); });
        m_searchBtn->setIcon(Theme::tintedIcon(":/resources/cross.svg", 16, Theme::OpsAccentAmber));
    } else {
        m_searchEdit->clear();
        m_deviceTable->filterDevices("");
        m_searchBtn->setIcon(Theme::tintedIcon(":/resources/search.svg", 17, Theme::OpsAccentGreen));
    }
}

void DeviceMonitorPage::onSearchChanged(const QString &text) {
    m_deviceTable->filterDevices(text);
}

// ─────────────────────────────────────────────────────────────────────────────
// Footer
// ─────────────────────────────────────────────────────────────────────────────

void DeviceMonitorPage::tickFooter() {
    m_liveDotState = !m_liveDotState;
    m_footerLiveDot->setStyleSheet(m_liveDotState
        ? "background-color: #34e4a0; border-radius: 4px;"
        : "background-color: rgba(52,228,160,0.25); border-radius: 4px;");

    qint64 secs = m_lastUpdate.secsTo(QDateTime::currentDateTime());
    m_lastUpdatedLabel->setText(secs < 5
        ? "updated just now"
        : QString("updated %1s ago").arg(secs));
}

// ─────────────────────────────────────────────────────────────────────────────
// Misc slots
// ─────────────────────────────────────────────────────────────────────────────

void DeviceMonitorPage::onRefreshRequested() {
    QMetaObject::invokeMethod(m_networkManager, "clearDevices", Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_networkManager, "runScan",      Qt::QueuedConnection);
}

void DeviceMonitorPage::onSelectionChanged(const QString &ip) { Q_UNUSED(ip); }

void DeviceMonitorPage::applySettings() {
    AppSettings *cfg = AppSettings::instance();
    // BW cards are gated on gateway AND user pref
    if (m_bwUpCard)   m_bwUpCard->setVisible(m_gatewayActive && cfg->showUploadColumn());
    if (m_bwDownCard) m_bwDownCard->setVisible(m_gatewayActive && cfg->showDownloadColumn());
}

// ─────────────────────────────────────────────────────────────────────────────
// Theme
// ─────────────────────────────────────────────────────────────────────────────

void DeviceMonitorPage::applyTheme() {
    setStyleSheet(
        "gui--DeviceMonitorPage { background-color: #0a0d12; }"

        "QWidget#HeaderBar {"
        "  background-color: transparent;"
        "  border-bottom: 1px solid #1c232c;"
        "}"
        "QWidget#StatStrip { background-color: transparent; }"
        "QWidget#TableContainer { background-color: transparent; }"
        "QWidget#AnalyticsPanel { background-color: transparent; }"
        "QWidget#FooterBar {"
        "  background-color: #0a0d12;"
        "  border-top: 1px solid #1c232c;"
        "}"

        // KPI stat card
        "QWidget#StatCard {"
        "  background-color: #0f141b;"
        "  border: 1px solid #1c232c;"
        "  border-radius: 8px;"
        "}"

        // Icon buttons (search/refresh) — ghost circle
        "QPushButton#IconBtn {"
        "  background: transparent;"
        "  border: 1px solid #1c232c;"
        "  border-radius: 17px;"
        "}"
        "QPushButton#IconBtn:hover {"
        "  background: rgba(52,228,160,0.07);"
        "  border-color: rgba(52,228,160,0.4);"
        "}"
        "QPushButton#IconBtn:pressed {"
        "  background: rgba(52,228,160,0.14);"
        "}"

        // Animated search edit
        "QLineEdit#SearchEdit {"
        "  background: #0f141b;"
        "  border: 1px solid #1c232c;"
        "  border-radius: 8px;"
        "  padding: 0 12px;"
        "  color: #dbe4ee;"
        "  font-size: 12px;"
        "  font-family: 'Inter', sans-serif;"
        "}"
        "QLineEdit#SearchEdit:focus { border-color: #34e4a0; background: #12181f; }"

        // Gateway active badge
        "QLabel#GatewayBadge {"
        "  background-color: rgba(52,228,160,0.10);"
        "  color: #34e4a0;"
        "  border: 1px solid rgba(52,228,160,0.30);"
        "  border-radius: 10px;"
        "  padding: 3px 12px;"
        "  font-size: 10px;"
        "  font-weight: bold;"
        "  font-family: 'JetBrains Mono', monospace;"
        "  letter-spacing: 0.10em;"
        "}"
    );
}

} // namespace gui
