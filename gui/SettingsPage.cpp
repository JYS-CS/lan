#include "SettingsPage.h"
#include "AppSettings.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QFrame>
#include <QPushButton>

namespace gui {

SettingsPage::SettingsPage(core::NetworkManager *nm, QWidget *parent) : QWidget(parent), m_nm(nm) {
    setStyleSheet(
        "gui--SettingsPage { background: #0a0d12; }"
        "QWidget { background: #0a0d12; }"
        "QFrame#SectionCard { background: #0f141b; border: 1px solid #1c232c; border-radius: 10px; }"
        "QScrollArea { background: #0a0d12; border: none; }"
        "QScrollBar:vertical { background: transparent; width: 8px; }"
        "QScrollBar::handle:vertical { background: rgba(255,255,255,0.08); border-radius: 4px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    );

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Header
    auto *header = new QWidget(this);
    header->setObjectName("HeaderBar");
    header->setStyleSheet("QWidget#HeaderBar { background: transparent; border-bottom: 1px solid #1c232c; }");
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(24, 20, 24, 20);
    auto *titleLabel = new QLabel("Settings", header);
    titleLabel->setStyleSheet("color: #dbe4ee; font-size: 22px; font-weight: bold; font-family: 'Inter', sans-serif;");
    hl->addWidget(titleLabel);
    hl->addStretch();
    root->addWidget(header);

    // ── Scrollable content
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    auto *cv = new QVBoxLayout(content);
    cv->setContentsMargins(24, 24, 24, 24);
    cv->setSpacing(20);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    AppSettings *cfg = AppSettings::instance();

    // ── Device Table section
    cv->addWidget(makeSection("DEVICE TABLE"));

    ToggleSwitch *swSparklines = nullptr;
    cv->addWidget(makeRow(
        "Bandwidth Sparklines",
        "Show animated spike charts in the upload and download columns instead of plain text values.",
        &swSparklines
    ));
    swSparklines->setChecked(cfg->showSparklines());
    connect(swSparklines, &ToggleSwitch::toggled, cfg, &AppSettings::setShowSparklines);

    ToggleSwitch *swUpload = nullptr;
    cv->addWidget(makeRow(
        "Show Upload Column",
        "Display the upload speed column in the device table. Disable to reduce visual clutter.",
        &swUpload
    ));
    swUpload->setChecked(cfg->showUploadColumn());
    connect(swUpload, &ToggleSwitch::toggled, cfg, &AppSettings::setShowUploadColumn);

    ToggleSwitch *swDownload = nullptr;
    cv->addWidget(makeRow(
        "Show Download Column",
        "Display the download speed column in the device table.",
        &swDownload
    ));
    swDownload->setChecked(cfg->showDownloadColumn());
    connect(swDownload, &ToggleSwitch::toggled, cfg, &AppSettings::setShowDownloadColumn);

    cv->addWidget(makeSection("DATA MANAGEMENT"));
    
    ToggleSwitch *swAutoClear = nullptr;
    cv->addWidget(makeRow(
        "Auto-Clear Historical Blocked Devices",
        "Automatically clear blocked devices associated with other networks when joining a new network.",
        &swAutoClear
    ));
    swAutoClear->setChecked(cfg->autoClearHistoricalDevices());
    connect(swAutoClear, &ToggleSwitch::toggled, cfg, &AppSettings::setAutoClearHistoricalDevices);

    cv->addWidget(makeSection("SECURITY"));

    ToggleSwitch *swStrictMode = nullptr;
    cv->addWidget(makeRow(
        "Block New Devices By Default",
        "When a device is not on the whitelist, block it automatically the moment it's seen "
        "instead of allowing it by default. Requires the DHCP Server running in Gateway "
        "(Intercept) mode to actually take effect.",
        &swStrictMode
    ));
    swStrictMode->setChecked(cfg->blockNewDevicesByDefault());
    connect(swStrictMode, &ToggleSwitch::toggled, cfg, &AppSettings::setBlockNewDevicesByDefault);

    ToggleSwitch *swThreatIntel = nullptr;
    cv->addWidget(makeRow(
        "Block Known-Malicious Destinations",
        "Automatically block outbound connections to known malware/phishing/C2 IPs, using a "
        "regularly-updated threat list (bitwire-it/ipblocklist). Requires the DHCP Server "
        "running in Gateway (Intercept) mode.",
        &swThreatIntel
    ));
    if (m_nm) {
        swThreatIntel->setChecked(m_nm->isThreatBlocklistEnabled());
        connect(swThreatIntel, &ToggleSwitch::toggled, this, [this](bool on) {
            QMetaObject::invokeMethod(m_nm, [this, on]() { m_nm->setThreatBlocklistEnabled(on); }, Qt::QueuedConnection);
        });
        connect(m_nm, &core::NetworkManager::threatBlocklistStatusChanged, this, &SettingsPage::refreshThreatStatus);
        connect(m_nm, &core::NetworkManager::threatBlocklistRefreshFailed, this, [this](const QString &err) {
            m_threatStatusLabel->setText("Update failed: " + err);
            m_threatStatusLabel->setStyleSheet("color: #ff5c5c; font-size: 11px; font-family: 'Inter';");
        });
    } else {
        swThreatIntel->setEnabled(false);
    }

    m_threatStatusLabel = new QLabel(this);
    m_threatStatusLabel->setStyleSheet("color: #7c8798; font-size: 11px; font-family: 'Inter'; padding: 0 0 8px 0;");
    cv->addWidget(m_threatStatusLabel);
    refreshThreatStatus();

    QPushButton *refreshThreatBtn = new QPushButton("Update Blocklist Now", this);
    refreshThreatBtn->setStyleSheet(
        "QPushButton { background: #0f141b; border: 1px solid #1c232c; color: #dbe4ee; "
        "border-radius: 7px; padding: 8px 16px; font-size: 12px; }"
        "QPushButton:hover { border: 1px solid rgba(94,234,212,0.4); }");
    refreshThreatBtn->setCursor(Qt::PointingHandCursor);
    connect(refreshThreatBtn, &QPushButton::clicked, this, [this]() {
        if (!m_nm) return;
        QMetaObject::invokeMethod(m_nm, [this]() { m_nm->refreshThreatBlocklistNow(); }, Qt::QueuedConnection);
    });
    cv->addWidget(refreshThreatBtn, 0, Qt::AlignLeft);

    ToggleSwitch *swDnsVisibility = nullptr;
    cv->addWidget(makeRow(
        "DNS Visibility & Filtering",
        "Make this machine the DNS resolver for every device on the network — see every domain "
        "each device looks up, and automatically block known malware/phishing/ad domains before "
        "a connection is even attempted. Requires the DHCP Server running (either mode).",
        &swDnsVisibility
    ));
    if (m_nm) {
        swDnsVisibility->setChecked(cfg->dnsVisibilityEnabled());
        connect(swDnsVisibility, &ToggleSwitch::toggled, this, [this, cfg](bool on) {
            cfg->setDnsVisibilityEnabled(on);
            QMetaObject::invokeMethod(m_nm, [this, on]() { m_nm->setDnsVisibilityEnabled(on); }, Qt::QueuedConnection);
        });
        connect(m_nm, &core::NetworkManager::dnsVisibilityStatusChanged, this, &SettingsPage::refreshDnsStatus);
        connect(m_nm, &core::NetworkManager::dnsBlocklistRefreshFailed, this, [this](const QString &err) {
            m_dnsStatusLabel->setText("Blocklist update failed: " + err);
            m_dnsStatusLabel->setStyleSheet("color: #ff5c5c; font-size: 11px; font-family: 'Inter';");
        });
    } else {
        swDnsVisibility->setEnabled(false);
    }

    m_dnsStatusLabel = new QLabel(this);
    m_dnsStatusLabel->setStyleSheet("color: #7c8798; font-size: 11px; font-family: 'Inter'; padding: 0 0 8px 0;");
    cv->addWidget(m_dnsStatusLabel);
    refreshDnsStatus();

    cv->addStretch();
}

void SettingsPage::refreshDnsStatus() {
    if (!m_nm || !m_dnsStatusLabel) return;
    if (!m_nm->isDnsVisibilityEnabled()) {
        m_dnsStatusLabel->setText("Disabled");
        m_dnsStatusLabel->setStyleSheet("color: #4d5666; font-size: 11px; font-family: 'Inter'; padding: 0 0 8px 0;");
        return;
    }
    QString runState = m_nm->isDnsProxyRunning() ? "running" : "enabled, waiting for DHCP Server to start";
    int domains = m_nm->dnsBlocklistEntryCount();
    QString text = domains > 0
        ? QString("%1 — %2 domains blocked, last updated %3").arg(runState).arg(domains).arg(m_nm->dnsBlocklistLastUpdated().toString("hh:mm:ss"))
        : QString("%1 — fetching domain blocklist…").arg(runState);
    m_dnsStatusLabel->setText(text);
    m_dnsStatusLabel->setStyleSheet("color: #34e4a0; font-size: 11px; font-family: 'Inter'; padding: 0 0 8px 0;");
}

void SettingsPage::refreshThreatStatus() {
    if (!m_nm || !m_threatStatusLabel) return;
    if (!m_nm->isThreatBlocklistEnabled()) {
        m_threatStatusLabel->setText("Disabled");
        m_threatStatusLabel->setStyleSheet("color: #4d5666; font-size: 11px; font-family: 'Inter'; padding: 0 0 8px 0;");
        return;
    }
    int count = m_nm->threatBlocklistEntryCount();
    QDateTime updated = m_nm->threatBlocklistLastUpdated();
    QString text = count > 0
        ? QString("%1 entries loaded — last updated %2").arg(count).arg(updated.toString("hh:mm:ss"))
        : "Enabled — fetching blocklist…";
    m_threatStatusLabel->setText(text);
    m_threatStatusLabel->setStyleSheet("color: #34e4a0; font-size: 11px; font-family: 'Inter'; padding: 0 0 8px 0;");
}

QWidget* SettingsPage::makeSection(const QString &title) {
    auto *lbl = new QLabel(title, this);
    lbl->setStyleSheet(
        "font-family: 'JetBrains Mono', monospace; font-size: 10px; font-weight: bold;"
        " color: #4d5666; letter-spacing: 0.15em; background: transparent;");
    return lbl;
}

QWidget* SettingsPage::makeRow(const QString &label, const QString &desc, ToggleSwitch **sw) {
    auto *card = new QFrame(this);
    card->setObjectName("SectionCard");
    auto *row = new QHBoxLayout(card);
    row->setContentsMargins(20, 18, 20, 18);
    row->setSpacing(16);

    // Text side
    auto *textCol = new QVBoxLayout();
    textCol->setSpacing(4);
    auto *lbl = new QLabel(label, card);
    lbl->setStyleSheet("color: #dbe4ee; font-size: 14px; font-weight: 600; font-family: 'Inter', sans-serif; background: transparent;");
    auto *descLbl = new QLabel(desc, card);
    descLbl->setWordWrap(true);
    descLbl->setStyleSheet("color: #7c8798; font-size: 12px; font-family: 'Inter', sans-serif; background: transparent;");
    textCol->addWidget(lbl);
    textCol->addWidget(descLbl);

    // Toggle side
    *sw = new ToggleSwitch(false, card);

    row->addLayout(textCol, 1);
    row->addWidget(*sw, 0, Qt::AlignVCenter);
    return card;
}

} // namespace gui
