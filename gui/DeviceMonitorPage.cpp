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
        connect(m_networkManager, &core::NetworkManager::globalTrafficStatsUpdated,
                this, &DeviceMonitorPage::onGlobalTrafficStats);

        // Keep blocked count KPI current when blocks change
        connect(m_networkManager, &core::NetworkManager::deviceBlocked,
                this, [this](const QString &) {
                    int n = core::DatabaseManager::instance().getBlacklist(m_networkManager->gatewayMac()).size();
                    if (m_blockedCount) m_blockedCount->setText(QString::number(n));
                });
        connect(m_networkManager, &core::NetworkManager::deviceUnblocked,
                this, [this](const QString &) {
                    int n = core::DatabaseManager::instance().getBlacklist(m_networkManager->gatewayMac()).size();
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
    auto *title = new QLabel("NETWORK OPERATIONS", this);
    title->setStyleSheet(
        "color: #dbe4ee; font-family: 'Inter', sans-serif; font-size: 16px; font-weight: bold; letter-spacing: 0.05em;");
    hl->addWidget(title);
    hl->addSpacing(20);

    hl->addStretch();

    // ── Header Stats (Online, Offline, Unknown, Blocked) ──
    m_headerStatsContainer = new QWidget(this);
    auto *statsLayout = new QHBoxLayout(m_headerStatsContainer);
    statsLayout->setContentsMargins(0, 0, 0, 0);
    statsLayout->setSpacing(10);
    
    statsLayout->addWidget(createHeaderStat(":/resources/icon_devices_online.svg", "#34e4a0", &m_onlineCount, "Devices Online"));
    statsLayout->addWidget(createHeaderStat(":/resources/icon_devices_offline.svg", "#8892b0", &m_offlineCount, "Devices Offline"));
    statsLayout->addWidget(createHeaderStat(":/resources/icon_unknown_vendors.svg", "#f5a623", &m_unknownCount, "Unknown Vendors"));
    m_blockedCard = createHeaderStat(":/resources/icon_blocked_devices.svg", "#ff5c5c", &m_blockedCount, "Blocked Devices");
    m_blockedCard->setVisible(false);
    statsLayout->addWidget(m_blockedCard);
    
    hl->addWidget(m_headerStatsContainer);
    hl->addSpacing(16);

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

    auto createStaticIcon = [](const QString &color) {
        QPixmap base(28, 28);
        base.fill(Qt::transparent);
        QPainter p(&base);
        p.drawPixmap(5, 5, Theme::tintedIcon(":/resources/refresh.svg", 17, color).pixmap(17, 17));
        return QIcon(base);
    };

    m_refreshBtn->setIcon(createStaticIcon("#7c8798")); // Theme::OpsTextDim
    m_refreshBtn->setIconSize(QSize(28, 28));

    m_spinAnim = new QVariantAnimation(this);
    m_spinAnim->setDuration(600);
    m_spinAnim->setStartValue(0.0);
    m_spinAnim->setEndValue(360.0);
    connect(m_spinAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        QPixmap base(28, 28);
        base.fill(Qt::transparent);
        QPainter p(&base);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.translate(14, 14);
        p.rotate(value.toReal());
        p.drawPixmap(-8, -8, Theme::tintedIcon(":/resources/refresh.svg", 17, "#34e4a0").pixmap(17, 17));
        m_refreshBtn->setIcon(QIcon(base));
    });
    connect(m_spinAnim, &QVariantAnimation::finished, this, [this, createStaticIcon]() {
        m_refreshBtn->setIcon(createStaticIcon("#7c8798"));
    });

    connect(m_searchBtn,  &QPushButton::clicked, this, &DeviceMonitorPage::onSearchToggled);
    connect(m_refreshBtn, &QPushButton::clicked, this, [this]() {
        if (m_spinAnim->state() != QAbstractAnimation::Running) {
            m_spinAnim->start();
        }
        onRefreshRequested();
    });

    hl->addWidget(m_searchEdit, 10);
    hl->addWidget(m_searchBtn);
    hl->addWidget(m_refreshBtn);
    hl->addSpacing(12);

    // Topology widget (host / gateway / internet indicators)
    m_topologyWidget = new TopologyWidget(this);
    hl->addWidget(m_topologyWidget);

    root->addWidget(headerBar);





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

QWidget* DeviceMonitorPage::createHeaderStat(const QString &iconPath, const QString &color,
                                             QLabel **countPtr, const QString &tooltip) {
    auto *container = new QWidget(this);
    container->setObjectName("HeaderStat");
    container->setToolTip(tooltip);
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(6);

    auto *iconLabel = new QLabel(container);
    iconLabel->setPixmap(Theme::tintedIcon(iconPath, 16, color).pixmap(16, 16));
    iconLabel->setFixedSize(16, 16);

    auto *valLabel = new QLabel("0", container);
    valLabel->setStyleSheet(QString("color: %1; font-weight: bold; font-family: 'Inter', sans-serif; font-size: 14px; background: transparent;").arg(color));

    layout->addWidget(iconLabel);
    layout->addWidget(valLabel);

    if (countPtr) *countPtr = valLabel;
    return container;
}

// ─────────────────────────────────────────────────────────────────────────────
// Gateway mode toggle — the core gating mechanism
// ─────────────────────────────────────────────────────────────────────────────

void DeviceMonitorPage::setGatewayModeActive(bool active) {
    m_gatewayActive = active;

    // blocked KPI cards
    m_blockedCard->setVisible(active);

    // Seed blocked count from DB when coming online
    if (active) {
        int n = core::DatabaseManager::instance().getBlacklist(m_networkManager->gatewayMac()).size();
        if (m_blockedCount) m_blockedCount->setText(QString::number(n));
    }

    // Gateway badge


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



void DeviceMonitorPage::updateDevices(const QList<core::Device> &devices) {
    m_deviceTable->updateDevices(devices);
    m_lastUpdate = QDateTime::currentDateTime();

    int online = 0, offline = 0, unknown = 0;

    for (const auto &d : devices) {
        QString s = d.status().toLower();
        if (s.contains("online") || s.contains("self")) ++online;
        else ++offline;
        
        if (d.vendor().toLower().contains("unknown")) ++unknown;
    }

    if (m_onlineCount)   m_onlineCount->setText(QString::number(online));
    if (m_offlineCount)  m_offlineCount->setText(QString::number(offline));
    if (m_unknownCount)  m_unknownCount->setText(QString::number(unknown));
    m_totalHostCountLabel->setText(QString("%1 HOSTS SCANNED").arg(devices.size()));
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
        if (m_headerStatsContainer) m_headerStatsContainer->setVisible(false);
        QTimer::singleShot(150, m_searchEdit, [this]() { m_searchEdit->setFocus(); });
        m_searchBtn->setIcon(Theme::tintedIcon(":/resources/cross.svg", 16, Theme::OpsAccentAmber));
    } else {
        if (m_headerStatsContainer) m_headerStatsContainer->setVisible(true);
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

        // Header stat
        "QWidget#HeaderStat {"
        "  background: rgba(255,255,255,0.03);"
        "  border: 1px solid #1c232c;"
        "  border-radius: 6px;"
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

    );
}

} // namespace gui
