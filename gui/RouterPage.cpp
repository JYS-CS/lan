#include "RouterPage.h"
#include "Theme.h"

#include <QPainter>
#include <QScrollArea>
#include <QSizePolicy>
#include <QApplication>

namespace gui {

// ─── CapBadge ────────────────────────────────────────────────────────────────
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
    m_iconLabel->setPixmap(Theme::tintedSvgPixmap(m_iconPath, 14, QColor("#3d4255")));

    m_textLabel = new QLabel(label, m_pill);
    m_textLabel->setStyleSheet("font-size: 11px; font-weight: 600; background: transparent;");

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
            "QFrame#CapPill { background: rgba(255,184,108,0.10); border: 1px solid rgba(255,184,108,0.30);"
            " border-radius: 8px; }");
        m_textLabel->setStyleSheet("font-size: 11px; font-weight: 600; color: #e8eaf0; background: transparent;");
        m_iconLabel->setPixmap(Theme::tintedSvgPixmap(m_iconPath, 14, QColor("#ffb86c")));
    } else {
        m_pill->setStyleSheet(
            "QFrame#CapPill { background: rgba(255,255,255,0.03); border: 1px solid rgba(255,255,255,0.07);"
            " border-radius: 8px; }");
        m_textLabel->setStyleSheet("font-size: 11px; font-weight: 600; color: #3d4255; background: transparent;");
        m_iconLabel->setPixmap(Theme::tintedSvgPixmap(m_iconPath, 14, QColor("#3d4255")));
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
        "QToolButton { text-align: left; background: rgba(255,255,255,0.04); border: 1px solid rgba(255,255,255,0.07);"
        " border-radius: 6px; color: #8a93b8; font-size: 11px; font-weight: 600; padding: 8px 12px; }"
        "QToolButton:hover { background: rgba(79,127,255,0.10); color: #c7cbe0; }");

    m_body = new QLabel(this);
    m_body->setWordWrap(true);
    m_body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_body->setStyleSheet(
        "background: rgba(0,0,0,0.25); border: 1px solid rgba(255,255,255,0.06);"
        " border-top: none; border-radius: 0 0 6px 6px; color: #6a7190;"
        " font-family: monospace; font-size: 10px; padding: 10px 14px;");
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
    // extract title from current text
    QString cur = m_header->text().mid(5);
    m_header->setText("  " + icon + "  " + cur);
}

// ─── RouterPage ───────────────────────────────────────────────────────────────
RouterPage::RouterPage(core::NetworkManager *nm, QWidget *parent)
    : QWidget(parent), m_nm(nm)
{
    setupUi();
    applyTheme();

    if (m_nm) {
        connect(m_nm, &core::NetworkManager::routerInfoReady,
                this, &RouterPage::updateInfo, Qt::QueuedConnection);
        connect(m_nm, &core::NetworkManager::routerDetectionStage,
                this, &RouterPage::setDetectionStage, Qt::QueuedConnection);

        // Fetch cached info if a detection already completed automatically in the background
        core::RouterInfo cached = m_nm->getRouterInfo();
        if (cached.isValid) {
            updateInfo(cached);
        }
    }

    m_spinnerTimer = new QTimer(this);
    connect(m_spinnerTimer, &QTimer::timeout, this, [this]() {
        const char *frames[] = {"◐","◓","◑","◒"};
        m_spinnerLabel->setText(frames[m_spinnerFrame++ % 4]);
    });
}

// ─── UI Setup ─────────────────────────────────────────────────────────────────
void RouterPage::setupUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Top bar
    auto *topBar = new QWidget(this);
    topBar->setObjectName("TopBar");
    auto *topRow = new QHBoxLayout(topBar);
    topRow->setContentsMargins(22, 12, 22, 12);
    topRow->setSpacing(10);

    auto *pageIcon = new QLabel(this);
    pageIcon->setPixmap(Theme::tintedSvgPixmap(":/resources/router.svg", 18, Theme::AccentBlue));
    auto *pageTitle = new QLabel("Router Intelligence", this);
    pageTitle->setStyleSheet("font-size: 14px; font-weight: 500; color: #e8eaf0;");

    m_stageLabel = new QLabel("Idle — trigger a scan to begin detection", this);
    m_stageLabel->setStyleSheet("font-size: 11px; color: #4a5068;");

    m_spinnerLabel = new QLabel("", this);
    m_spinnerLabel->setStyleSheet("font-size: 14px; color: #4f7fff;");
    m_spinnerLabel->setFixedWidth(20);

    m_rescanBtn = new QPushButton("Re-scan", this);
    m_rescanBtn->setObjectName("PrimaryBtn");
    m_rescanBtn->setCursor(Qt::PointingHandCursor);
    m_rescanBtn->setFixedHeight(32);

    topRow->addWidget(pageIcon);
    topRow->addWidget(pageTitle);
    topRow->addSpacing(16);
    topRow->addWidget(m_spinnerLabel);
    topRow->addWidget(m_stageLabel);
    topRow->addStretch();
    topRow->addWidget(m_rescanBtn);
    root->addWidget(topBar);

    // ── Scrollable content
    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scrollArea);
    auto *cv = new QVBoxLayout(content);
    cv->setContentsMargins(22, 18, 22, 24);
    cv->setSpacing(16);
    scrollArea->setWidget(content);
    root->addWidget(scrollArea);

    // ──── Hero Card ────────────────────────────────────────────────────────────
    auto *hero = new QFrame(content);
    hero->setObjectName("HeroCard");
    auto *heroLayout = new QHBoxLayout(hero);
    heroLayout->setContentsMargins(24, 20, 24, 20);
    heroLayout->setSpacing(20);

    auto *heroIcon = new QLabel(hero);
    heroIcon->setPixmap(Theme::tintedSvgPixmap(":/resources/router.svg", 42, Theme::AccentBlue));
    heroIcon->setFixedSize(52, 52);
    heroIcon->setAlignment(Qt::AlignCenter);
    heroIcon->setStyleSheet(
        "background: rgba(79,127,255,0.12); border-radius: 14px; padding: 5px;"
        " border: 1px solid rgba(79,127,255,0.25);");

    auto *heroInfo = new QVBoxLayout();
    heroInfo->setSpacing(4);
    m_gwIpLabel = new QLabel("—", hero);
    m_gwIpLabel->setStyleSheet("font-size: 20px; font-weight: 700; color: #e8eaf0;");
    m_gwMacLabel = new QLabel("—", hero);
    m_gwMacLabel->setStyleSheet("font-size: 11px; color: #5a6175; font-family: monospace;");
    heroInfo->addWidget(m_gwIpLabel);
    heroInfo->addWidget(m_gwMacLabel);

    heroLayout->addWidget(heroIcon);
    heroLayout->addLayout(heroInfo);
    heroLayout->addStretch();

    // Class badge inside hero
    m_classBadge = new QLabel("UNKNOWN", hero);
    m_classBadge->setObjectName("ClassBadge");
    m_classBadge->setAlignment(Qt::AlignCenter);
    m_classBadge->setFixedHeight(26);
    m_classBadge->setStyleSheet(
        "QLabel#ClassBadge { background: rgba(79,127,255,0.15); color: #4f7fff;"
        " border: 1px solid rgba(79,127,255,0.3); border-radius: 6px; font-size: 10px;"
        " font-weight: 700; padding: 0 14px; letter-spacing: 0.06em; }");
    heroLayout->addWidget(m_classBadge, 0, Qt::AlignRight | Qt::AlignVCenter);

    cv->addWidget(hero);

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
    idTitle->setStyleSheet("font-size: 9px; font-weight: 700; color: #4a5068; letter-spacing: 0.1em;");

    auto makeField = [&](const QString &lbl, QLabel **valPtr) -> QWidget* {
        auto *w = new QWidget(idCard);
        auto *vl = new QVBoxLayout(w);
        vl->setContentsMargins(0,0,0,0);
        vl->setSpacing(2);
        auto *l = new QLabel(lbl, w);
        l->setStyleSheet("font-size: 9px; color: #4a5068; font-weight: 600; letter-spacing: 0.05em;");
        *valPtr = new QLabel("—", w);
        (*valPtr)->setStyleSheet("font-size: 13px; color: #c7cbe0; font-weight: 500;");
        (*valPtr)->setWordWrap(true);
        vl->addWidget(l);
        vl->addWidget(*valPtr);
        return w;
    };

    idLayout->addWidget(idTitle);
    idLayout->addWidget(makeField("MANUFACTURER", &m_mfrLabel));
    idLayout->addWidget(makeField("MODEL", &m_modelLabel));
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
    capTitle->setStyleSheet("font-size: 9px; font-weight: 700; color: #4a5068; letter-spacing: 0.1em;");
    capLayout->addWidget(capTitle);

    auto *capGrid = new QGridLayout();
    capGrid->setSpacing(8);

    m_capSSH       = new CapBadge(":/resources/ssh.svg", "SSH");
    m_capTelnet    = new CapBadge(":/resources/telnet.svg", "Telnet");
    m_capWebUI     = new CapBadge(":/resources/webui.svg", "Web UI");
    m_capSNMP      = new CapBadge(":/resources/snmp.svg", "SNMP");
    m_capUPnP      = new CapBadge(":/resources/upnp.svg", "UPnP");
    m_capIPv6      = new CapBadge(":/resources/ipv6.svg", "IPv6");
    m_capGuest     = new CapBadge(":/resources/guest.svg", "Guest WiFi");
    m_capVPN       = new CapBadge(":/resources/vpn.svg", "VPN");
    m_capQoS       = new CapBadge(":/resources/qos.svg", "QoS");
    m_capDual      = new CapBadge(":/resources/wifi.svg", "Dual Band");
    m_capTri       = new CapBadge(":/resources/wifi.svg", "Tri Band");
    m_capWPA3      = new CapBadge(":/resources/wpa3.svg", "WPA3");
    m_capEnterprise= new CapBadge(":/resources/enterprise.svg", "Enterprise");

    QList<CapBadge*> badges = {
        m_capWebUI, m_capSSH, m_capTelnet, m_capSNMP,
        m_capUPnP,  m_capIPv6, m_capGuest, m_capVPN,
        m_capQoS,   m_capDual, m_capTri, m_capWPA3,
        m_capEnterprise
    };
    for (int i = 0; i < badges.size(); ++i)
        capGrid->addWidget(badges[i], i / 2, i % 2);

    capLayout->addLayout(capGrid);
    capLayout->addStretch();

    midRow->addWidget(idCard, 1);
    midRow->addWidget(capCard, 1);
    cv->addLayout(midRow);

    // ──── Security Risk Bar ────────────────────────────────────────────────────
    m_secRiskBar = new QFrame(content);
    m_secRiskBar->setObjectName("SectionCard");
    auto *riskLayout = new QVBoxLayout(m_secRiskBar);
    riskLayout->setContentsMargins(20, 14, 20, 14);
    riskLayout->setSpacing(8);

    auto *riskTitle = new QLabel("SECURITY POSTURE", m_secRiskBar);
    riskTitle->setStyleSheet("font-size: 9px; font-weight: 700; color: #4a5068; letter-spacing: 0.1em;");

    auto *riskRow = new QHBoxLayout();
    riskRow->setSpacing(10);

    m_credsRiskLabel = new QLabel("Default Creds: —", m_secRiskBar);
    m_credsRiskLabel->setObjectName("RiskPill");
    m_credsRiskLabel->setAlignment(Qt::AlignCenter);
    m_credsRiskLabel->setFixedHeight(24);

    m_fwRiskLabel = new QLabel("Firmware Risk: —", m_secRiskBar);
    m_fwRiskLabel->setObjectName("RiskPill");
    m_fwRiskLabel->setAlignment(Qt::AlignCenter);
    m_fwRiskLabel->setFixedHeight(24);

    riskRow->addWidget(m_credsRiskLabel);
    riskRow->addWidget(m_fwRiskLabel);
    riskRow->addStretch();

    m_notesLabel = new QLabel("", m_secRiskBar);
    m_notesLabel->setWordWrap(true);
    m_notesLabel->setStyleSheet("font-size: 10px; color: #5a6080; font-style: italic;");
    m_notesLabel->setVisible(false);

    riskLayout->addWidget(riskTitle);
    riskLayout->addLayout(riskRow);
    riskLayout->addWidget(m_notesLabel);
    cv->addWidget(m_secRiskBar);

    auto *rawProbeTitle = new QLabel("RAW PROBE DATA", content);
    rawProbeTitle->setStyleSheet("font-size: 9px; font-weight: 700; color: #4a5068; letter-spacing: 0.1em; margin-top: 4px;");
    cv->addWidget(rawProbeTitle);

    m_secHTTP    = new AccordionSection("HTTP Banner  (ports 80/8080/443/8443)", content);
    m_secSSDPUPnP= new AccordionSection("SSDP / UPnP  Descriptor XML", content);
    m_secSNMP    = new AccordionSection("SNMP  sysDescr / sysName / sysLocation", content);
    m_secPorts   = new AccordionSection("Open Ports", content);

    cv->addWidget(m_secHTTP);
    cv->addWidget(m_secSSDPUPnP);
    cv->addWidget(m_secSNMP);
    cv->addWidget(m_secPorts);
    cv->addStretch();

    connect(m_rescanBtn, &QPushButton::clicked, this, &RouterPage::onRescanClicked);
}

// ─── Theme ────────────────────────────────────────────────────────────────────
void RouterPage::applyTheme() {
    setStyleSheet(
        "gui--RouterPage { background: #0d1117; }"
        "QWidget#TopBar { background: #181b22; border-bottom: 0.5px solid rgba(255,255,255,0.07); }"
        "QScrollArea { background: #0d1117; border: none; }"
        "QFrame#HeroCard {"
        "  background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
        "    stop:0 #161d2f, stop:1 #1a1f2e);"
        "  border: 1px solid rgba(79,127,255,0.20);"
        "  border-radius: 14px; }"
        "QFrame#SectionCard {"
        "  background: #161b26; border: 1px solid rgba(255,255,255,0.07);"
        "  border-radius: 12px; }"
        "QPushButton#PrimaryBtn {"
        "  background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #4f7fff, stop:1 #6b90ff);"
        "  color: #fff; font-weight: 600; font-size: 12px; border-radius: 8px; padding: 6px 16px; border: none; }"
        "QPushButton#PrimaryBtn:hover { background: #6b90ff; }"
        "QScrollBar:vertical { background: transparent; width: 8px; }"
        "QScrollBar::handle:vertical { background: rgba(255,255,255,0.1); border-radius: 4px; min-height: 20px; }"
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

    // Class badge
    QString cls = core::routerClassString(info.routerClass);
    m_classBadge->setText(cls.toUpper());
    QString badgeStyle;
    switch (info.routerClass) {
        case core::RouterClass::Enterprise:
            badgeStyle = "background:rgba(255,92,92,0.12);color:#ff5c5c;border:1px solid rgba(255,92,92,0.3);"; break;
        case core::RouterClass::SMB:
            badgeStyle = "background:rgba(255,145,66,0.12);color:#ff9142;border:1px solid rgba(255,145,66,0.3);"; break;
        case core::RouterClass::ISPCPE:
            badgeStyle = "background:rgba(180,100,255,0.12);color:#b464ff;border:1px solid rgba(180,100,255,0.3);"; break;
        case core::RouterClass::MobileHotspot:
            badgeStyle = "background:rgba(61,220,132,0.12);color:#3ddc84;border:1px solid rgba(61,220,132,0.3);"; break;
        default:
            badgeStyle = "background:rgba(79,127,255,0.12);color:#4f7fff;border:1px solid rgba(79,127,255,0.3);"; break;
    }
    m_classBadge->setStyleSheet(
        "QLabel#ClassBadge { " + badgeStyle +
        " border-radius:6px; font-size:10px; font-weight:700; padding:0 14px; letter-spacing:0.06em; }");

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

    // Security risk bar
    auto riskPillStyle = [](const QString &text, const QString &color, const QString &bg) {
        return QString("QLabel#RiskPill { background:%1; color:%2; border:1px solid %2; "
                       "border-radius:6px; font-size:10px; font-weight:700; padding:0 12px; }")
               .arg(bg, color);
    };
    if (info.caps.defaultCredsRisk) {
        m_credsRiskLabel->setText("  Default Creds Risk");
        m_credsRiskLabel->setStyleSheet(riskPillStyle("", "#ff5c5c", "rgba(255,92,92,0.10)"));
    } else {
        m_credsRiskLabel->setText("  Creds OK");
        m_credsRiskLabel->setStyleSheet(riskPillStyle("", "#3ddc84", "rgba(61,220,132,0.08)"));
    }
    auto fwColor = [](const QString &r) -> QPair<QString,QString> {
        if (r == "high")   return {"#ff5c5c", "rgba(255,92,92,0.10)"};
        if (r == "medium") return {"#ff9142", "rgba(255,145,66,0.10)"};
        if (r == "low")    return {"#3ddc84", "rgba(61,220,132,0.08)"};
        return {"#8a93b8", "rgba(255,255,255,0.04)"};
    };
    auto [fc, fb] = fwColor(info.caps.firmwareRisk);
    m_fwRiskLabel->setText(QString("  Firmware Risk: %1").arg(info.caps.firmwareRisk.toUpper()));
    m_fwRiskLabel->setStyleSheet(riskPillStyle("", fc, fb));

    // Model notes
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
    m_stageLabel->setText(stage);
    if (stage != "Done") {
        setScanning(true);
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
    // Reset throttle by calling directly with force=true
    QMetaObject::invokeMethod(m_nm, [this]() {
        m_nm->triggerRouterDetection(true);
    }, Qt::QueuedConnection);
}

} // namespace gui
