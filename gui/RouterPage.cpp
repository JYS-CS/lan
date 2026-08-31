#include "RouterPage.h"
#include "Theme.h"

#include <QPainter>
#include <QScrollArea>
#include <QSizePolicy>
#include <QApplication>

namespace gui {

// ─── CapBadge ─────────────────────────────────────────────────────────────────
CapBadge::CapBadge(const QString &iconPath, const QString &label, QWidget *parent)
    : QWidget(parent), m_iconPath(iconPath)
{
    m_pill = new QFrame(this);
    m_pill->setObjectName("CapPill");

    auto *row = new QHBoxLayout(m_pill);
    row->setContentsMargins(10, 6, 12, 6);
    row->setSpacing(7);

    m_iconLabel = new QLabel(m_pill);
    m_iconLabel->setFixedSize(16, 16);
    m_iconLabel->setAlignment(Qt::AlignCenter);

    m_textLabel = new QLabel(label, m_pill);
    m_textLabel->setStyleSheet("font-family: 'Inter', sans-serif; font-size: 11px; font-weight: 600; background: transparent;");

    row->addWidget(m_iconLabel);
    row->addWidget(m_textLabel);

    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_pill);

    setState(false);
}

void CapBadge::setState(bool active) {
    if (active) {
        m_pill->setStyleSheet(
            "QFrame#CapPill { background: rgba(52,228,160,0.08); border: 1px solid rgba(52,228,160,0.25);"
            " border-radius: 8px; }");
        m_textLabel->setStyleSheet("font-family: 'Inter'; font-size: 11px; font-weight: 600; color: #34e4a0; background: transparent;");
        m_iconLabel->setPixmap(Theme::tintedSvgPixmap(m_iconPath, 14, Theme::OpsAccentGreen));
    } else {
        m_pill->setStyleSheet(
            "QFrame#CapPill { background: rgba(255,255,255,0.03); border: 1px solid #1c232c;"
            " border-radius: 8px; }");
        m_textLabel->setStyleSheet("font-family: 'Inter'; font-size: 11px; font-weight: 600; color: #4d5666; background: transparent;");
        m_iconLabel->setPixmap(Theme::tintedSvgPixmap(m_iconPath, 14, Theme::OpsTextFaint));
    }
}

// ─── AccordionSection ─────────────────────────────────────────────────────────
AccordionSection::AccordionSection(const QString &title, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_header = new QToolButton(this);
    m_header->setText("  ▶  " + title);
    m_header->setCheckable(false);
    m_header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_header->setStyleSheet(
        "QToolButton { text-align: left; background: #0f141b; border: 1px solid #1c232c;"
        " border-radius: 6px; color: #7c8798; font-family: 'JetBrains Mono', monospace;"
        " font-size: 10px; font-weight: 600; padding: 9px 14px; letter-spacing: 0.05em; }"
        "QToolButton:hover { background: #12181f; color: #dbe4ee; border-color: #34e4a0; }");

    m_body = new QLabel(this);
    m_body->setWordWrap(true);
    m_body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_body->setStyleSheet(
        "background: #0a0d12; border: 1px solid #1c232c; border-top: none;"
        " border-radius: 0 0 6px 6px; color: #7c8798;"
        " font-family: 'JetBrains Mono', monospace; font-size: 10px; padding: 12px 16px;");
    m_body->hide();

    layout->addWidget(m_header);
    layout->addWidget(m_body);

    connect(m_header, &QToolButton::clicked, this, &AccordionSection::toggle);
}

void AccordionSection::setContent(const QString &text) {
    m_body->setText(text.isEmpty() ? "(no data)" : text.left(2000));
}

void AccordionSection::toggle() {
    m_expanded = !m_expanded;
    m_body->setVisible(m_expanded);
    QString icon = m_expanded ? "▼" : "▶";
    QString cur = m_header->text().mid(5);
    m_header->setText("  " + icon + "  " + cur);
}

// ─── RouterPage ───────────────────────────────────────────────────────────────
RouterPage::RouterPage(core::NetworkManager *nm, QWidget *parent)
    : QWidget(parent), m_nm(nm)
{
    setupUi();
    applyTheme();

    // Must be created before setScanning() is ever called
    m_spinnerTimer = new QTimer(this);
    connect(m_spinnerTimer, &QTimer::timeout, this, [this]() {
        const char *frames[] = {"◐","◓","◑","◒"};
        m_spinnerLabel->setText(frames[m_spinnerFrame++ % 4]);
    });

    if (m_nm) {
        connect(m_nm, &core::NetworkManager::routerInfoReady,
                this, &RouterPage::updateInfo, Qt::QueuedConnection);
        connect(m_nm, &core::NetworkManager::routerDetectionStage,
                this, &RouterPage::setDetectionStage, Qt::QueuedConnection);

        core::RouterInfo cached = m_nm->getRouterInfo();
        if (cached.isValid) {
            // Already have a result — show it immediately
            updateInfo(cached);
        } else {
            // Scan is still running (started at app boot) — show progress now
            setScanning(true);
            m_stageLabel->setText("Detection in progress…");
        }
    }
}

// ─── UI Setup ─────────────────────────────────────────────────────────────────
void RouterPage::setupUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Header Bar — title left, controls right
    auto *headerBar = new QWidget(this);
    headerBar->setObjectName("HeaderBar");
    auto *headerLayout = new QHBoxLayout(headerBar);
    headerLayout->setContentsMargins(24, 20, 24, 20);

    auto *pageTitle = new QLabel("Router Intelligence", this);
    pageTitle->setStyleSheet("color: #dbe4ee; font-size: 22px; font-weight: bold; font-family: 'Inter', sans-serif;");

    headerLayout->addWidget(pageTitle);
    headerLayout->addStretch();

    // Spinner + stage + rescan
    m_spinnerLabel = new QLabel("", this);
    m_spinnerLabel->setStyleSheet("font-size: 14px; color: #34e4a0;");
    m_spinnerLabel->setFixedWidth(20);

    m_stageLabel = new QLabel("Idle — trigger a scan to begin detection", this);
    m_stageLabel->setStyleSheet("font-size: 11px; color: #4d5666; font-family: 'Inter', sans-serif;");

    m_rescanBtn = new QPushButton("Re-scan", this);
    m_rescanBtn->setObjectName("PrimaryBtn");
    m_rescanBtn->setCursor(Qt::PointingHandCursor);
    m_rescanBtn->setFixedHeight(34);

    headerLayout->addWidget(m_spinnerLabel);
    headerLayout->addWidget(m_stageLabel);
    headerLayout->addSpacing(16);
    headerLayout->addWidget(m_rescanBtn);
    root->addWidget(headerBar);

    // ── Stat Strip (4 cards like DeviceMonitorPage)
    auto *statStrip = new QWidget(this);
    statStrip->setObjectName("StatStrip");
    auto *statLayout = new QHBoxLayout(statStrip);
    statLayout->setContentsMargins(24, 0, 24, 20);
    statLayout->setSpacing(16);

    statLayout->addWidget(createStatCard("GATEWAY IP",    "#5eead4", &m_gwIpLabel));
    statLayout->addWidget(createStatCard("MAC ADDRESS",   "#7c8798", &m_gwMacLabel));
    statLayout->addWidget(createStatCard("MODEL",         "#4f7fff", &m_modelLabel));
    statLayout->addWidget(createStatCard("CLASS",         "#f5a623", &m_classBadge));
    root->addWidget(statStrip);

    // ── Scrollable content
    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scrollArea);
    auto *cv = new QVBoxLayout(content);
    cv->setContentsMargins(24, 8, 24, 24);
    cv->setSpacing(16);
    scrollArea->setWidget(content);
    root->addWidget(scrollArea, 1);

    // ──── Two-column row: Identity | Capabilities ──────────────────────────────
    auto *midRow = new QHBoxLayout();
    midRow->setSpacing(16);

    // Identity Card
    auto *idCard = new QFrame(content);
    idCard->setObjectName("SectionCard");
    auto *idLayout = new QVBoxLayout(idCard);
    idLayout->setContentsMargins(20, 18, 20, 18);
    idLayout->setSpacing(12);

    auto *idTitle = new QLabel("IDENTITY", idCard);
    idTitle->setStyleSheet("font-family: 'JetBrains Mono', monospace; font-size: 10px; font-weight: 700; color: #4d5666; letter-spacing: 0.12em;");

    auto makeField = [&](const QString &lbl, QLabel **valPtr) -> QWidget* {
        auto *w = new QWidget(idCard);
        auto *vl = new QVBoxLayout(w);
        vl->setContentsMargins(0,0,0,0);
        vl->setSpacing(2);
        auto *l = new QLabel(lbl, w);
        l->setStyleSheet("font-family: 'JetBrains Mono', monospace; font-size: 9px; color: #4d5666; font-weight: 600; letter-spacing: 0.08em;");
        *valPtr = new QLabel("—", w);
        (*valPtr)->setStyleSheet("font-family: 'Inter', sans-serif; font-size: 13px; color: #dbe4ee; font-weight: 500;");
        (*valPtr)->setWordWrap(true);
        vl->addWidget(l);
        vl->addWidget(*valPtr);
        return w;
    };

    idLayout->addWidget(idTitle);
    idLayout->addWidget(makeField("MANUFACTURER", &m_mfrLabel));
    idLayout->addWidget(makeField("FIRMWARE / VERSION", &m_firmwareLabel));
    idLayout->addWidget(makeField("FRIENDLY NAME", &m_friendlyLabel));
    idLayout->addWidget(makeField("SYSTEM NAME (SNMP)", &m_snmpLabel));
    idLayout->addWidget(makeField("LOCATION", &m_locationLabel));
    idLayout->addStretch();

    // Capabilities Card
    auto *capCard = new QFrame(content);
    capCard->setObjectName("SectionCard");
    auto *capLayout = new QVBoxLayout(capCard);
    capLayout->setContentsMargins(20, 18, 20, 18);
    capLayout->setSpacing(12);

    auto *capTitle = new QLabel("CAPABILITIES", capCard);
    capTitle->setStyleSheet("font-family: 'JetBrains Mono', monospace; font-size: 10px; font-weight: 700; color: #4d5666; letter-spacing: 0.12em;");
    capLayout->addWidget(capTitle);

    auto *capGrid = new QGridLayout();
    capGrid->setSpacing(8);

    m_capSSH        = new CapBadge(":/resources/ssh.svg",        "SSH");
    m_capTelnet     = new CapBadge(":/resources/telnet.svg",     "Telnet");
    m_capWebUI      = new CapBadge(":/resources/webui.svg",      "Web UI");
    m_capSNMP       = new CapBadge(":/resources/snmp.svg",       "SNMP");
    m_capUPnP       = new CapBadge(":/resources/upnp.svg",       "UPnP");
    m_capIPv6       = new CapBadge(":/resources/ipv6.svg",       "IPv6");
    m_capGuest      = new CapBadge(":/resources/guest.svg",      "Guest WiFi");
    m_capVPN        = new CapBadge(":/resources/vpn.svg",        "VPN");
    m_capQoS        = new CapBadge(":/resources/qos.svg",        "QoS");
    m_capDual       = new CapBadge(":/resources/wifi.svg",       "Dual Band");
    m_capTri        = new CapBadge(":/resources/wifi.svg",       "Tri Band");
    m_capWPA3       = new CapBadge(":/resources/wpa3.svg",       "WPA3");
    m_capEnterprise = new CapBadge(":/resources/enterprise.svg", "Enterprise");

    QList<CapBadge*> badges = {
        m_capWebUI, m_capSSH, m_capTelnet, m_capSNMP,
        m_capUPnP,  m_capIPv6, m_capGuest, m_capVPN,
        m_capQoS,   m_capDual, m_capTri,   m_capWPA3,
        m_capEnterprise
    };
    for (int i = 0; i < badges.size(); ++i)
        capGrid->addWidget(badges[i], i / 2, i % 2);

    capLayout->addLayout(capGrid);
    capLayout->addStretch();

    midRow->addWidget(idCard, 1);
    midRow->addWidget(capCard, 1);
    cv->addLayout(midRow);

    // ──── Security Posture Card ────────────────────────────────────────────────
    m_secRiskBar = new QFrame(content);
    m_secRiskBar->setObjectName("SectionCard");
    auto *riskLayout = new QVBoxLayout(m_secRiskBar);
    riskLayout->setContentsMargins(20, 16, 20, 16);
    riskLayout->setSpacing(10);

    auto *riskTitle = new QLabel("SECURITY POSTURE", m_secRiskBar);
    riskTitle->setStyleSheet("font-family: 'JetBrains Mono', monospace; font-size: 10px; font-weight: 700; color: #4d5666; letter-spacing: 0.12em;");

    auto *riskRow = new QHBoxLayout();
    riskRow->setSpacing(12);

    m_credsRiskLabel = new QLabel("Default Creds: —", m_secRiskBar);
    m_credsRiskLabel->setObjectName("RiskPill");
    m_credsRiskLabel->setAlignment(Qt::AlignCenter);
    m_credsRiskLabel->setFixedHeight(26);

    m_fwRiskLabel = new QLabel("Firmware Risk: —", m_secRiskBar);
    m_fwRiskLabel->setObjectName("RiskPill");
    m_fwRiskLabel->setAlignment(Qt::AlignCenter);
    m_fwRiskLabel->setFixedHeight(26);

    riskRow->addWidget(m_credsRiskLabel);
    riskRow->addWidget(m_fwRiskLabel);
    riskRow->addStretch();

    m_notesLabel = new QLabel("", m_secRiskBar);
    m_notesLabel->setWordWrap(true);
    m_notesLabel->setStyleSheet("font-family: 'Inter'; font-size: 11px; color: #7c8798; font-style: italic;");
    m_notesLabel->setVisible(false);

    riskLayout->addWidget(riskTitle);
    riskLayout->addLayout(riskRow);
    riskLayout->addWidget(m_notesLabel);
    cv->addWidget(m_secRiskBar);

    // ──── Raw Probe Data (Accordions) ─────────────────────────────────────────
    auto *rawTitle = new QLabel("RAW PROBE DATA", content);
    rawTitle->setStyleSheet("font-family: 'JetBrains Mono', monospace; font-size: 10px; font-weight: 700; color: #4d5666; letter-spacing: 0.12em; margin-top: 4px;");
    cv->addWidget(rawTitle);

    m_secHTTP     = new AccordionSection("HTTP Banner  (ports 80/8080/443/8443)", content);
    m_secSSDPUPnP = new AccordionSection("SSDP / UPnP  Descriptor XML", content);
    m_secSNMP     = new AccordionSection("SNMP  sysDescr / sysName / sysLocation", content);
    m_secPorts    = new AccordionSection("Open Ports", content);

    cv->addWidget(m_secHTTP);
    cv->addWidget(m_secSSDPUPnP);
    cv->addWidget(m_secSNMP);
    cv->addWidget(m_secPorts);
    cv->addStretch();

    connect(m_rescanBtn, &QPushButton::clicked, this, &RouterPage::onRescanClicked);
}

QWidget* RouterPage::createStatCard(const QString &label, const QString &color, QLabel **countPtr) {
    auto *card = new QWidget(this);
    card->setObjectName("StatCard");
    auto *l = new QVBoxLayout(card);
    l->setContentsMargins(16, 16, 16, 16);
    l->setSpacing(4);

    auto *lbl = new QLabel(label, this);
    lbl->setStyleSheet(QString("font-family: 'JetBrains Mono', monospace; font-size: 10px; font-weight: bold; color: %1; letter-spacing: 0.1em; background: transparent;").arg(color));

    *countPtr = new QLabel("—", this);
    (*countPtr)->setStyleSheet("font-family: 'Inter', sans-serif; font-size: 18px; font-weight: 500; color: #dbe4ee; background: transparent;");
    (*countPtr)->setWordWrap(true);

    l->addWidget(lbl);
    l->addWidget(*countPtr);
    return card;
}

// ─── Theme ────────────────────────────────────────────────────────────────────
void RouterPage::applyTheme() {
    setStyleSheet(
        "gui--RouterPage { background-color: #0a0d12; }"
        "QWidget#HeaderBar { background-color: transparent; border-bottom: 1px solid #1c232c; }"
        "QWidget#StatStrip { background-color: transparent; }"
        "QWidget#StatCard  { background-color: #0f141b; border: 1px solid #1c232c; border-radius: 8px; }"
        "QScrollArea { background: #0a0d12; border: none; }"
        "QWidget { background: #0a0d12; }"

        "QFrame#SectionCard {"
        "  background: #0f141b; border: 1px solid #1c232c; border-radius: 10px; }"

        "QPushButton#PrimaryBtn {"
        "  background: #0f141b; color: #34e4a0; font-family: 'Inter'; font-weight: 600;"
        "  font-size: 12px; border-radius: 8px; padding: 6px 18px;"
        "  border: 1px solid #34e4a0; }"
        "QPushButton#PrimaryBtn:hover { background: rgba(52,228,160,0.10); }"
        "QPushButton#PrimaryBtn:disabled { color: #4d5666; border-color: #1c232c; }"

        "QLabel#RiskPill { font-family: 'JetBrains Mono'; font-size: 10px; font-weight: 700;"
        "  padding: 0 14px; border-radius: 6px; }"

        "QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }"
        "QScrollBar::handle:vertical { background: rgba(255,255,255,0.08); border-radius: 4px; min-height: 20px; }"
        "QScrollBar::handle:vertical:hover { background: rgba(52,228,160,0.3); }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    );
}

// ─── Data update ──────────────────────────────────────────────────────────────
void RouterPage::updateInfo(const core::RouterInfo &info) {
    setScanning(false);

    m_gwIpLabel->setText(info.gatewayIp.isEmpty() ? "—" : info.gatewayIp);
    m_gwMacLabel->setText(info.gatewayMac.isEmpty() ? "—" : info.gatewayMac.toUpper());
    m_mfrLabel->setText(info.manufacturer.isEmpty() ? "—" : info.manufacturer);
    m_modelLabel->setText(info.model.isEmpty() ? "—" : info.model);
    m_firmwareLabel->setText(info.firmware.isEmpty() ? "—" : info.firmware);
    m_friendlyLabel->setText(info.friendlyName.isEmpty() ? "—" : info.friendlyName);
    m_snmpLabel->setText(info.systemName.isEmpty() ? "—" : info.systemName);
    m_locationLabel->setText(info.systemLocation.isEmpty() ? "—" : info.systemLocation);

    // Class badge in stat card
    QString cls = core::routerClassString(info.routerClass);
    m_classBadge->setText(cls.isEmpty() ? "—" : cls);
    QString clsColor;
    switch (info.routerClass) {
        case core::RouterClass::Enterprise:    clsColor = "#ff5c5c"; break;
        case core::RouterClass::SMB:           clsColor = "#f5a623"; break;
        case core::RouterClass::ISPCPE:        clsColor = "#b464ff"; break;
        case core::RouterClass::MobileHotspot: clsColor = "#34e4a0"; break;
        default:                               clsColor = "#7c8798"; break;
    }
    m_classBadge->setStyleSheet(QString("font-family: 'Inter'; font-size: 18px; font-weight: 500; color: %1; background: transparent;").arg(clsColor));

    // Capabilities
    m_capSSH->setState(info.caps.hasSSH);
    m_capTelnet->setState(info.caps.hasTelnet);
    m_capWebUI->setState(info.caps.hasWebUI);
    m_capSNMP->setState(info.caps.hasSNMP);
    m_capUPnP->setState(info.caps.hasUPnP);
    m_capIPv6->setState(info.caps.hasIPv6);
    m_capGuest->setState(info.caps.hasGuestWifi);
    m_capVPN->setState(info.caps.hasVPN);
    m_capQoS->setState(info.caps.hasQoS);
    m_capDual->setState(info.caps.hasDualBand);
    m_capTri->setState(info.caps.hasTriBand);
    m_capWPA3->setState(info.caps.hasWPA3);
    m_capEnterprise->setState(info.caps.isEnterprise);

    // Security risk pills
    auto riskPillStyle = [](const QString &color, const QString &bg) {
        return QString("QLabel#RiskPill { background:%1; color:%2; border:1px solid %2; "
                       "border-radius:6px; font-size:10px; font-weight:700; padding:0 12px; }")
               .arg(bg, color);
    };
    if (info.caps.defaultCredsRisk) {
        m_credsRiskLabel->setText("  Default Creds Risk");
        m_credsRiskLabel->setStyleSheet(riskPillStyle("#ff5c5c", "rgba(255,92,92,0.10)"));
    } else {
        m_credsRiskLabel->setText("  Creds OK");
        m_credsRiskLabel->setStyleSheet(riskPillStyle("#34e4a0", "rgba(52,228,160,0.08)"));
    }
    auto fwColor = [](const QString &r) -> QPair<QString,QString> {
        if (r == "high")   return {"#ff5c5c", "rgba(255,92,92,0.10)"};
        if (r == "medium") return {"#f5a623", "rgba(245,166,35,0.10)"};
        if (r == "low")    return {"#34e4a0", "rgba(52,228,160,0.08)"};
        return {"#7c8798", "rgba(255,255,255,0.04)"};
    };
    auto [fc, fb] = fwColor(info.caps.firmwareRisk);
    m_fwRiskLabel->setText(QString("  Firmware: %1").arg(info.caps.firmwareRisk.toUpper()));
    m_fwRiskLabel->setStyleSheet(riskPillStyle(fc, fb));

    if (!info.matchedModelNotes.isEmpty()) {
        m_notesLabel->setText("  " + info.matchedModelNotes);
        m_notesLabel->setVisible(true);
    } else {
        m_notesLabel->setVisible(false);
    }

    // Accordion content
    m_secHTTP->setContent(info.httpBanner);
    m_secSSDPUPnP->setContent(
        info.ssdpResponse + (info.upnpXml.isEmpty() ? "" : "\n\n--- UPnP XML ---\n" + info.upnpXml));
    m_secSNMP->setContent(
        "sysDescr:    " + info.snmpSysDescr + "\n"
        "sysName:     " + info.systemName + "\n"
        "sysLocation: " + info.systemLocation);
    m_secPorts->setContent(info.openPorts.isEmpty() ? "(none detected)" : info.openPorts.join("\n"));

    m_stageLabel->setText(QString("Last scan: %1").arg(info.lastScanned.toString("hh:mm:ss")));
    Theme::fadeIn(this);
}

void RouterPage::setDetectionStage(const QString &stage) {
    if (stage == "Done") {
        setScanning(false);
        m_stageLabel->setText("Scan complete");
    } else {
        setScanning(true);
        m_stageLabel->setText(stage);
    }
}

void RouterPage::setScanning(bool scanning) {
    if (scanning) {
        m_spinnerTimer->start(200);
        m_rescanBtn->setEnabled(false);
    } else {
        m_spinnerTimer->stop();
        m_spinnerLabel->clear();
        m_rescanBtn->setEnabled(true);
    }
}

void RouterPage::onRescanClicked() {
    if (!m_nm) return;
    setScanning(true);
    m_stageLabel->setText("Starting detection…");
    QMetaObject::invokeMethod(m_nm, [this]() {
        m_nm->triggerRouterDetection(true);
    }, Qt::QueuedConnection);
}

} // namespace gui
