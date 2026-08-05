#include "StartupModePage.h"
#include "../core/NetworkManager.h"
#include "Theme.h"
#include <QFrame>
#include <QFile>
#include <QProcess>
#include <QHostAddress>
#include <QButtonGroup>
#include <QPainter>
#include <QSvgRenderer>
#include <QRegularExpression>
#include <QGridLayout>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QProgressBar>

namespace gui {

namespace {

// Loads a Feather-style SVG icon (single top-level stroke="#hex" attribute),
// re-tints it to the given color, and rasterizes it at the given size.
QPixmap coloredSvgIcon(const QString &resourcePath, int size, const QColor &color) {
    QFile file(resourcePath);
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return pixmap;

    QString svgText = QString::fromUtf8(file.readAll());
    static const QRegularExpression strokeRe("stroke=\"#[0-9a-fA-F]{3,8}\"");
    svgText.replace(strokeRe, QString("stroke=\"%1\"").arg(color.name()));

    QSvgRenderer renderer(svgText.toUtf8());
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter);
    return pixmap;
}

QLabel *makeIconLabel(QWidget *parent, const QString &resourcePath, int size, const QColor &color) {
    QLabel *lbl = new QLabel(parent);
    lbl->setPixmap(coloredSvgIcon(resourcePath, size, color));
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setFixedHeight(size);
    return lbl;
}

const QColor kAccent   = gui::Theme::AccentBlue;
const QColor kOrange   = gui::Theme::AccentOrange;
const QColor kNeutral  = gui::Theme::TextSecondary;

} // namespace

StartupModePage::StartupModePage(core::NetworkManager *networkManager, QWidget *parent)
    : QWidget(parent), m_networkManager(networkManager)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    buildUi();
    applyTheme();
}

void StartupModePage::buildUi() {
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(36, 56, 36, 36);
    root->setSpacing(18);
    // No alignment constraint — let the layout fill the full widget area
    // so the page expands properly inside the QStackedWidget.

    m_stepLabel = new QLabel("STEP 1", this);
    m_stepLabel->setObjectName("StepLabel");
    m_stepLabel->setAlignment(Qt::AlignCenter);
    root->addWidget(m_stepLabel);

    m_steps = new QStackedWidget(this);
    m_steps->setObjectName("StepStack");
    m_steps->setMaximumWidth(820);
    m_steps->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_steps->addWidget(buildStepMode());            // 0
    m_steps->addWidget(buildStepRouterWarning());    // 1
    m_steps->addWidget(buildStepDetectNetwork());    // 2
    m_steps->addWidget(buildStepAuthoritative());    // 3
    m_steps->addWidget(buildStepGateway());          // 4
    m_steps->addWidget(buildStepReview());           // 5

    // Horizontally center the step widget by flanking with stretches.
    QHBoxLayout *centerRow = new QHBoxLayout();
    centerRow->addStretch(1);
    centerRow->addWidget(m_steps, 4); // give it a strong stretch factor
    centerRow->addStretch(1);
    root->addLayout(centerRow, 1); // expand vertically to fill available height
}

// ── Step 1: choose a mode ────────────────────────────────────────────────────
QWidget *StartupModePage::buildStepMode() {
    QWidget *page = new QWidget();
    QVBoxLayout *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(24);

    QLabel *title = new QLabel("Select Operating Mode", page);
    title->setObjectName("PageTitle");
    title->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(title);

    QLabel *subtitle = new QLabel(
        "Choose how this machine participates in the network.\n"
        "You can change this later from the DHCP page.", page);
    subtitle->setObjectName("PageSubtitle");
    subtitle->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(subtitle);

    QHBoxLayout *cardsRow = new QHBoxLayout();
    cardsRow->setSpacing(18);

    // Normal card
    QWidget *normalCard = new QWidget(page);
    normalCard->setObjectName("ModeCard");
    QVBoxLayout *nl = new QVBoxLayout(normalCard);
    nl->setContentsMargins(24, 24, 24, 24);
    nl->setSpacing(12);
    nl->addWidget(makeIconLabel(normalCard, ":/resources/monitor.svg", 40, kNeutral));
    QLabel *nTitle = new QLabel("Normal Mode", normalCard);
    nTitle->setObjectName("ModeCardTitle");
    nTitle->setAlignment(Qt::AlignCenter);
    QLabel *nDesc = new QLabel(
        "Passive monitoring only. Your router keeps handling IP assignment.\n\n"
        "Per-device traffic details are not available in this mode.", normalCard);
    nDesc->setObjectName("ModeCardDesc");
    nDesc->setAlignment(Qt::AlignCenter);
    nDesc->setWordWrap(true);
    m_normalBtn = new QPushButton("Continue in Normal Mode", normalCard);
    m_normalBtn->setObjectName("GhostBtn");
    m_normalBtn->setCursor(Qt::PointingHandCursor);
    nl->addWidget(nTitle);
    nl->addWidget(nDesc);
    nl->addStretch();
    nl->addWidget(m_normalBtn);

    // DHCP card
    QWidget *dhcpCard = new QWidget(page);
    dhcpCard->setObjectName("ModeCardOrange");
    QVBoxLayout *dl = new QVBoxLayout(dhcpCard);
    dl->setContentsMargins(24, 24, 24, 24);
    dl->setSpacing(12);
    dl->addWidget(makeIconLabel(dhcpCard, ":/resources/subnet.svg", 40, kOrange));
    QLabel *dTitle = new QLabel("DHCP Server Mode", dhcpCard);
    dTitle->setObjectName("ModeCardTitle");
    dTitle->setAlignment(Qt::AlignCenter);
    QLabel *dDesc = new QLabel(
        "This machine becomes the DHCP authority, with full lease control.\n\n"
        "In Intercept mode, all device traffic routes through it and is fully visible.", dhcpCard);
    dDesc->setObjectName("ModeCardDesc");
    dDesc->setAlignment(Qt::AlignCenter);
    dDesc->setWordWrap(true);
    m_dhcpBtn = new QPushButton("Continue to DHCP Setup", dhcpCard);
    m_dhcpBtn->setObjectName("PrimaryBtnOrange");
    m_dhcpBtn->setCursor(Qt::PointingHandCursor);
    dl->addWidget(dTitle);
    dl->addWidget(dDesc);
    dl->addStretch();
    dl->addWidget(m_dhcpBtn);

    // Cable card (USB to Ethernet)
    QWidget *cableCard = new QWidget(page);
    cableCard->setObjectName("ModeCard");
    QVBoxLayout *cl = new QVBoxLayout(cableCard);
    cl->setContentsMargins(24, 24, 24, 24);
    cl->setSpacing(12);
    cl->addWidget(makeIconLabel(cableCard, ":/resources/subnet.svg", 40, kNeutral));
    QLabel *cTitle = new QLabel("Cable Mode", cableCard);
    cTitle->setObjectName("ModeCardTitle");
    cTitle->setAlignment(Qt::AlignCenter);
    QLabel *cDesc = new QLabel(
        "For direct physical connections like a USB-to-Ethernet link.\n\n"
        "Automatically routes isolated subnetwork traffic without needing to disable an external router.", cableCard);
    cDesc->setObjectName("ModeCardDesc");
    cDesc->setAlignment(Qt::AlignCenter);
    cDesc->setWordWrap(true);
    m_cableBtn = new QPushButton("Start in Cable Mode", cableCard);
    m_cableBtn->setObjectName("GhostBtn");
    m_cableBtn->setCursor(Qt::PointingHandCursor);
    cl->addWidget(cTitle);
    cl->addWidget(cDesc);
    cl->addStretch();
    cl->addWidget(m_cableBtn);

    cardsRow->addWidget(normalCard, 1);
    cardsRow->addWidget(dhcpCard, 1);
    cardsRow->addWidget(cableCard, 1);
    pageLayout->addLayout(cardsRow);

    connect(m_normalBtn, &QPushButton::clicked, this, &StartupModePage::onNormalChosen);
    connect(m_dhcpBtn,   &QPushButton::clicked, this, &StartupModePage::onDhcpChosen);
    connect(m_cableBtn,  &QPushButton::clicked, this, &StartupModePage::onCableChosen);

    return page;
}

// ── Step 2: confirm router's DHCP is disabled ────────────────────────────────
QWidget *StartupModePage::buildStepRouterWarning() {
    QWidget *page = new QWidget();
    QVBoxLayout *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(20);

    pageLayout->addWidget(makeIconLabel(page, ":/resources/router.svg", 36, kOrange));

    m_warnTitle = new QLabel("Confirm DHCP Server Setup", page);
    m_warnTitle->setObjectName("PageTitle");
    m_warnTitle->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(m_warnTitle);

    m_warnSubtitle = new QLabel(
        "Before this machine starts handing out leases, make sure your\n"
        "router's built-in DHCP server is turned off.", page);
    m_warnSubtitle->setObjectName("PageSubtitle");
    m_warnSubtitle->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(m_warnSubtitle);

    m_warnLabel = new QLabel(
        "Important: If your router's DHCP server is still enabled, "
        "devices on the network may receive conflicting IP configurations. "
        "Disable it in your router's admin settings before continuing.", page);
    m_warnLabel->setObjectName("WarnLabel");
    m_warnLabel->setWordWrap(true);
    pageLayout->addWidget(m_warnLabel);

    pageLayout->addStretch();

    QHBoxLayout *actions = new QHBoxLayout();
    m_warnBackBtn = new QPushButton("Back", page);
    m_warnBackBtn->setObjectName("GhostBtn");
    m_warnBackBtn->setCursor(Qt::PointingHandCursor);
    m_warnContinueBtn = new QPushButton("I've disabled my router's DHCP — Continue", page);
    m_warnContinueBtn->setObjectName("PrimaryBtn");
    m_warnContinueBtn->setCursor(Qt::PointingHandCursor);
    actions->addWidget(m_warnBackBtn);
    actions->addStretch();
    actions->addWidget(m_warnContinueBtn);
    pageLayout->addLayout(actions);

    connect(m_warnBackBtn,     &QPushButton::clicked, this, [this]() { goToStep(0); });
    connect(m_warnContinueBtn, &QPushButton::clicked, this, [this]() { goToStep(2); });

    return page;
}

// ── Step 3: auto-detect this machine's network configuration ────────────────
QWidget *StartupModePage::buildStepDetectNetwork() {
    QWidget *page = new QWidget();
    QVBoxLayout *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(20);

    pageLayout->addWidget(makeIconLabel(page, ":/resources/search.svg", 36, kAccent));

    QLabel *title = new QLabel("Detecting Network Configuration", page);
    title->setObjectName("PageTitle");
    title->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(title);

    QLabel *subtitle = new QLabel(
        "We'll scan this machine's active interface to prefill sensible\n"
        "defaults for the DHCP pool.", page);
    subtitle->setObjectName("PageSubtitle");
    subtitle->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(subtitle);

    QWidget *resultBox = new QWidget(page);
    resultBox->setObjectName("ResultBox");
    QGridLayout *grid = new QGridLayout(resultBox);
    grid->setContentsMargins(20, 18, 20, 18);
    grid->setHorizontalSpacing(24);
    grid->setVerticalSpacing(10);

    auto addRow = [&](int row, const QString &key, QLabel *&valueLabel) {
        QLabel *keyLabel = new QLabel(key, resultBox);
        keyLabel->setObjectName("ResultKey");
        valueLabel = new QLabel("Detecting…", resultBox);
        valueLabel->setObjectName("ResultValue");
        grid->addWidget(keyLabel, row, 0);
        grid->addWidget(valueLabel, row, 1);
    };
    addRow(0, "Interface", m_detIfaceValue);
    addRow(1, "IP Address", m_detIpValue);
    addRow(2, "Subnet Mask", m_detMaskValue);
    addRow(3, "Gateway", m_detGatewayValue);
    pageLayout->addWidget(resultBox);

    m_detStatusLabel = new QLabel("Detecting…", page);
    m_detStatusLabel->setObjectName("PageSubtitle");
    m_detStatusLabel->setAlignment(Qt::AlignCenter);
    m_detStatusLabel->setWordWrap(true);
    pageLayout->addWidget(m_detStatusLabel);

    m_detProgress = new QProgressBar(page);
    m_detProgress->setRange(0, 0); // indeterminate — animates automatically while visible
    m_detProgress->setTextVisible(false);
    m_detProgress->setFixedHeight(6);
    pageLayout->addWidget(m_detProgress);

    pageLayout->addStretch();

    QHBoxLayout *actions = new QHBoxLayout();
    m_detBackBtn = new QPushButton("Back", page);
    m_detBackBtn->setObjectName("GhostBtn");
    m_detBackBtn->setCursor(Qt::PointingHandCursor);
    m_detRetryBtn = new QPushButton("Re-detect", page);
    m_detRetryBtn->setObjectName("GhostBtn");
    m_detRetryBtn->setCursor(Qt::PointingHandCursor);
    m_detContinueBtn = new QPushButton("Continue", page);
    m_detContinueBtn->setObjectName("PrimaryBtn");
    m_detContinueBtn->setCursor(Qt::PointingHandCursor);
    m_detContinueBtn->setEnabled(false);
    actions->addWidget(m_detBackBtn);
    actions->addWidget(m_detRetryBtn);
    actions->addStretch();
    actions->addWidget(m_detContinueBtn);
    pageLayout->addLayout(actions);

    connect(m_detBackBtn, &QPushButton::clicked, this, [this]() {
        if (m_selectedMode == Mode::CableMode) goToStep(0);
        else goToStep(1); 
    });
    connect(m_detContinueBtn, &QPushButton::clicked, this, [this]() { goToStep(3); });
    connect(m_detRetryBtn,    &QPushButton::clicked, this, &StartupModePage::runNetworkDetection);

    return page;
}

// ── Step 4: authoritative mode ───────────────────────────────────────────────
QWidget *StartupModePage::buildStepAuthoritative() {
    QWidget *page = new QWidget();
    QVBoxLayout *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(20);

    pageLayout->addWidget(makeIconLabel(page, ":/resources/settings.svg", 36, kAccent));

    QLabel *title = new QLabel("Authoritative Mode", page);
    title->setObjectName("PageTitle");
    title->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(title);

    QLabel *subtitle = new QLabel(
        "Decide how this server behaves when a client already holds a lease\n"
        "from a different, unrecognized DHCP server.", page);
    subtitle->setObjectName("PageSubtitle");
    subtitle->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(subtitle);

    QHBoxLayout *optionsRow = new QHBoxLayout();
    optionsRow->setSpacing(18);

    m_authYesBtn = new QPushButton(
        "Authoritative (Recommended)\n\n"
        "Actively corrects clients holding a lease from another DHCP "
        "server, keeping the network consistent.", page);
    m_authYesBtn->setObjectName("OptionCard");
    m_authYesBtn->setCheckable(true);
    m_authYesBtn->setChecked(true);
    m_authYesBtn->setCursor(Qt::PointingHandCursor);

    m_authNoBtn = new QPushButton(
        "Non-authoritative\n\n"
        "Only answers new requests and never contests leases handed "
        "out by another DHCP server.", page);
    m_authNoBtn->setObjectName("OptionCard");
    m_authNoBtn->setCheckable(true);
    m_authNoBtn->setCursor(Qt::PointingHandCursor);

    QButtonGroup *group = new QButtonGroup(page);
    group->setExclusive(true);
    group->addButton(m_authYesBtn);
    group->addButton(m_authNoBtn);

    optionsRow->addWidget(m_authYesBtn, 1);
    optionsRow->addWidget(m_authNoBtn, 1);
    pageLayout->addLayout(optionsRow);

    pageLayout->addStretch();

    QHBoxLayout *actions = new QHBoxLayout();
    m_authBackBtn = new QPushButton("Back", page);
    m_authBackBtn->setObjectName("GhostBtn");
    m_authBackBtn->setCursor(Qt::PointingHandCursor);
    m_authNextBtn = new QPushButton("Continue", page);
    m_authNextBtn->setObjectName("PrimaryBtn");
    m_authNextBtn->setCursor(Qt::PointingHandCursor);
    actions->addWidget(m_authBackBtn);
    actions->addStretch();
    actions->addWidget(m_authNextBtn);
    pageLayout->addLayout(actions);

    connect(m_authBackBtn, &QPushButton::clicked, this, [this]() { goToStep(2); });
    connect(m_authNextBtn, &QPushButton::clicked, this, [this]() {
        m_settings.authoritative = m_authYesBtn->isChecked();
        goToStep(4);
    });

    return page;
}

// ── Step 5: gateway / intercept mode ─────────────────────────────────────────
QWidget *StartupModePage::buildStepGateway() {
    QWidget *page = new QWidget();
    QVBoxLayout *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(20);

    pageLayout->addWidget(makeIconLabel(page, ":/resources/merger.svg", 36, kAccent));

    QLabel *title = new QLabel("Act as Gateway", page);
    title->setObjectName("PageTitle");
    title->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(title);

    QLabel *subtitle = new QLabel(
        "Decide whether client traffic should route through this machine\n"
        "on its way to the real router.", page);
    subtitle->setObjectName("PageSubtitle");
    subtitle->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(subtitle);

    QHBoxLayout *optionsRow = new QHBoxLayout();
    optionsRow->setSpacing(18);

    m_gwTransparentBtn = new QPushButton(
        "Transparent (Recommended)\n\n"
        "The real router stays the gateway. Less invasive, with limited "
        "per-device traffic visibility.", page);
    m_gwTransparentBtn->setObjectName("OptionCard");
    m_gwTransparentBtn->setCheckable(true);
    m_gwTransparentBtn->setChecked(true);
    m_gwTransparentBtn->setCursor(Qt::PointingHandCursor);

    m_gwInterceptBtn = new QPushButton(
        "Intercept\n\n"
        "This machine becomes the gateway via NAT. All client traffic "
        "is fully visible.", page);
    m_gwInterceptBtn->setObjectName("OptionCard");
    m_gwInterceptBtn->setCheckable(true);
    m_gwInterceptBtn->setCursor(Qt::PointingHandCursor);

    QButtonGroup *group = new QButtonGroup(page);
    group->setExclusive(true);
    group->addButton(m_gwTransparentBtn);
    group->addButton(m_gwInterceptBtn);

    optionsRow->addWidget(m_gwTransparentBtn, 1);
    optionsRow->addWidget(m_gwInterceptBtn, 1);
    pageLayout->addLayout(optionsRow);

    pageLayout->addStretch();

    QHBoxLayout *actions = new QHBoxLayout();
    m_gwBackBtn = new QPushButton("Back", page);
    m_gwBackBtn->setObjectName("GhostBtn");
    m_gwBackBtn->setCursor(Qt::PointingHandCursor);
    m_gwNextBtn = new QPushButton("Continue", page);
    m_gwNextBtn->setObjectName("PrimaryBtn");
    m_gwNextBtn->setCursor(Qt::PointingHandCursor);
    actions->addWidget(m_gwBackBtn);
    actions->addStretch();
    actions->addWidget(m_gwNextBtn);
    pageLayout->addLayout(actions);

    connect(m_gwBackBtn, &QPushButton::clicked, this, [this]() { goToStep(3); });
    connect(m_gwNextBtn, &QPushButton::clicked, this, [this]() {
        m_settings.intercept = m_gwInterceptBtn->isChecked();
        refreshReviewSummary();
        goToStep(5);
    });

    return page;
}

// ── Step 6: review + final settings ──────────────────────────────────────────
QWidget *StartupModePage::buildStepReview() {
    QWidget *page = new QWidget();
    QVBoxLayout *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(18);

    pageLayout->addWidget(makeIconLabel(page, ":/resources/list.svg", 36, kAccent));

    QLabel *title = new QLabel("Review DHCP Settings", page);
    title->setObjectName("PageTitle");
    title->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(title);

    QLabel *subtitle = new QLabel(
        "Confirm the lease pool and DNS servers, then start the server.", page);
    subtitle->setObjectName("PageSubtitle");
    subtitle->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(subtitle);

    m_reviewSummary = new QLabel(page);
    m_reviewSummary->setObjectName("ResultValue");
    m_reviewSummary->setAlignment(Qt::AlignCenter);
    m_reviewSummary->setWordWrap(true);
    pageLayout->addWidget(m_reviewSummary);

    QWidget *fieldsBox = new QWidget(page);
    fieldsBox->setObjectName("ResultBox");
    QGridLayout *grid = new QGridLayout(fieldsBox);
    grid->setContentsMargins(20, 18, 20, 18);
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(12);

    auto addField = [&](int row, int col, const QString &label, QLineEdit *&edit) {
        QLabel *l = new QLabel(label, fieldsBox);
        l->setObjectName("ResultKey");
        edit = new QLineEdit(fieldsBox);
        QVBoxLayout *cell = new QVBoxLayout();
        cell->setSpacing(4);
        cell->addWidget(l);
        cell->addWidget(edit);
        grid->addLayout(cell, row, col);
    };
    addField(0, 0, "Range start", m_rangeStartEdit);
    addField(0, 1, "Range end", m_rangeEndEdit);
    addField(1, 0, "Primary DNS", m_dns1Edit);
    addField(1, 1, "Secondary DNS", m_dns2Edit);
    addField(2, 0, "Lease time (seconds)", m_leaseEdit);
    pageLayout->addWidget(fieldsBox);

    pageLayout->addStretch();

    QHBoxLayout *actions = new QHBoxLayout();
    m_reviewBackBtn = new QPushButton("Back", page);
    m_reviewBackBtn->setObjectName("GhostBtn");
    m_reviewBackBtn->setCursor(Qt::PointingHandCursor);
    m_finishBtn = new QPushButton("Complete Setup & Start Server", page);
    m_finishBtn->setObjectName("PrimaryBtn");
    m_finishBtn->setCursor(Qt::PointingHandCursor);
    actions->addWidget(m_reviewBackBtn);
    actions->addStretch();
    actions->addWidget(m_finishBtn);
    pageLayout->addLayout(actions);

    connect(m_reviewBackBtn, &QPushButton::clicked, this, [this]() { goToStep(4); });
    connect(m_finishBtn, &QPushButton::clicked, this, [this]() {
        m_settings.rangeStart      = m_rangeStartEdit->text().trimmed();
        m_settings.rangeEnd        = m_rangeEndEdit->text().trimmed();
        m_settings.dns1            = m_dns1Edit->text().trimmed();
        m_settings.dns2            = m_dns2Edit->text().trimmed();
        m_settings.leaseTimeSeconds= m_leaseEdit->text().trimmed().toInt();
        if (m_settings.leaseTimeSeconds <= 0) m_settings.leaseTimeSeconds = 3600;
        emit dhcpWizardCompleted(m_settings);
    });

    return page;
}

void StartupModePage::refreshReviewSummary() {
    if (!m_reviewSummary) return;
    m_reviewSummary->setText(QString(
        "%1  ·  %2\nAuthoritative: %3   Gateway: %4")
        .arg(m_settings.interface.isEmpty() ? "—" : m_settings.interface)
        .arg(m_settings.hostIp.isEmpty() ? "—" : m_settings.hostIp)
        .arg(m_settings.authoritative ? "Yes" : "No")
        .arg(m_settings.intercept ? "Intercept" : "Transparent"));

    m_rangeStartEdit->setText(m_settings.rangeStart);
    m_rangeEndEdit->setText(m_settings.rangeEnd);
    m_dns1Edit->setText(m_settings.dns1);
    m_dns2Edit->setText(m_settings.dns2);
    m_leaseEdit->setText(QString::number(m_settings.leaseTimeSeconds));
}

// ── Network detection ────────────────────────────────────────────────────────
void StartupModePage::runNetworkDetection() {
    if (!m_networkManager) return;

    m_detStatusLabel->setText("Detecting…");
    m_detContinueBtn->setEnabled(false);
    m_detProgress->setVisible(true);
    m_detIfaceValue->setText("Detecting…");
    m_detIpValue->setText("Detecting…");
    m_detMaskValue->setText("Detecting…");
    m_detGatewayValue->setText("Detecting…");

    QString iface = m_networkManager->getActiveInterface();
    if (iface.isEmpty()) {
        m_detStatusLabel->setText("No active network interface detected. Check your connection and retry.");
        m_detProgress->setVisible(false);
        return;
    }

    QHostAddress ip   = m_networkManager->getInterfaceAddress(iface);
    QHostAddress mask = m_networkManager->getInterfaceNetmask(iface);
    if (ip.isNull() || mask.isNull()) {
        m_detStatusLabel->setText("Could not read the IP configuration for " + iface + ".");
        m_detProgress->setVisible(false);
        return;
    }

    QProcess proc;
    proc.start("sh", {"-c", QString("ip route show dev %1 | grep default | awk '{print $3}'").arg(iface)});
    proc.waitForFinished();
    QString gw = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();

    m_settings.interface  = iface;
    m_settings.hostIp     = ip.toString();
    m_settings.subnetMask = mask.toString();
    m_settings.gatewayIp  = gw;

    m_detIfaceValue->setText(iface);
    m_detIpValue->setText(ip.toString());
    m_detMaskValue->setText(mask.toString());
    m_detGatewayValue->setText(gw.isEmpty() ? "Not detected" : gw);

    // Suggested DHCP pool, spanning most of the subnet
    quint32 ipInt   = ip.toIPv4Address();
    quint32 maskInt = mask.toIPv4Address();
    quint32 network = ipInt & maskInt;
    quint32 bcast   = network | (~maskInt);
    quint32 poolStart = network + 100;
    quint32 poolEnd   = bcast - 10;
    if (poolStart <= ipInt) poolStart = ipInt + 1;
    if (poolEnd   <= ipInt) poolEnd   = ipInt + 254;
    m_settings.rangeStart = QHostAddress(poolStart).toString();
    m_settings.rangeEnd   = QHostAddress(poolEnd).toString();
    m_settings.dns1 = "1.1.1.1";
    m_settings.dns2 = "8.8.8.8";
    m_settings.leaseTimeSeconds = 3600;

    if (gw.isEmpty()) {
        m_detStatusLabel->setText("Gateway not detected automatically — you can set it manually in the final step.");
    } else {
        m_detStatusLabel->setText("Network detected successfully.");
    }
    m_detProgress->setVisible(false);
    m_detContinueBtn->setEnabled(true);
}

void StartupModePage::goToStep(int index) {
    if (index == 1) {
        if (m_selectedMode == Mode::CableMode) {
            m_warnTitle->setText("Hardware Connection Check");
            m_warnSubtitle->setText("Ensure that the direct physical link is structurally connected.");
            m_warnLabel->setText(
                "Important: Before proceeding to the hardware scanning step, confirm that your USB-to-Ethernet adapter "
                "(or direct CAT6 connection) is physically plugged into the computer and linked to an active hardware switch or client device."
            );
            m_warnContinueBtn->setText("Hardware is securely plugged in — Continue");
        } else {
            m_warnTitle->setText("Confirm DHCP Server Setup");
            m_warnSubtitle->setText(
                "Before this machine starts handing out leases, make sure your\n"
                "router's built-in DHCP server is turned off.");
            m_warnLabel->setText(
                "Important: If your router's DHCP server is still enabled, "
                "devices on the network may receive conflicting IP configurations. "
                "Disable it in your router's admin settings before continuing.");
            m_warnContinueBtn->setText("I've disabled my router's DHCP — Continue");
        }
    }

    m_steps->setCurrentIndex(index);
    m_stepLabel->setText(index == 0 ? "STEP 1" : QString("STEP %1 OF 6").arg(index + 1));

    QWidget *page = m_steps->currentWidget();
    if (page) {
        auto *effect = new QGraphicsOpacityEffect(page);
        page->setGraphicsEffect(effect);
        auto *anim = new QPropertyAnimation(effect, "opacity", page);
        anim->setDuration(260);
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(anim, &QPropertyAnimation::finished, effect, [page]() {
            page->setGraphicsEffect(nullptr);
        });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }

    if (index == 2) runNetworkDetection();
}

void StartupModePage::onNormalChosen() {
    emit modeSelected(Mode::Normal, false);
}

void StartupModePage::onDhcpChosen() {
    m_selectedMode = Mode::DHCPServer;
    goToStep(1);
}

void StartupModePage::onCableChosen() {
    m_selectedMode = Mode::CableMode;
    m_settings.intercept = false;
    goToStep(1); // Divert sequentially down the warning pipeline dynamically
}

void StartupModePage::applyTheme() {
    setStyleSheet(
        "StartupModePage { background-color: #0d1117; }"
        "QLabel { color: #e8eaf0; border: none; background: transparent; }"
        "QLabel#StepLabel { font-size: 11px; font-weight: 600; letter-spacing: 0.12em; color: #4f7fff; }"
        "QLabel#PageTitle { font-size: 22px; font-weight: 700; color: #e8eaf0; }"
        "QLabel#PageSubtitle { font-size: 13px; color: #7c8299; margin-bottom: 4px; }"
        "QLabel#WarnLabel { font-size: 13px; color: #e8c07a; background: rgba(232,192,122,0.08); "
        "   border: 0.5px solid rgba(232,192,122,0.25); border-radius: 8px; padding: 14px 16px; }"
        "QWidget#ModeCard { background-color: #181b22; border: 0.5px solid rgba(255,255,255,0.08); border-radius: 12px; }"
        "QWidget#ModeCardOrange { background-color: #181b22; border: 0.5px solid rgba(255,145,66,0.25); border-radius: 12px; }"
        "QWidget#ModeCardOrange:hover { border: 0.5px solid rgba(255,145,66,0.5); }"
        "QLabel#ModeCardTitle { font-size: 16px; font-weight: 600; color: #e8eaf0; }"
        "QLabel#ModeCardDesc { font-size: 13px; color: #8a93b8; }"
        "QWidget#ResultBox { background-color: #131722; border: 0.5px solid rgba(255,255,255,0.07); border-radius: 10px; }"
        "QLabel#ResultKey { font-size: 11px; font-weight: 600; letter-spacing: 0.05em; color: #5a6175; }"
        "QLabel#ResultValue { font-size: 14px; font-weight: 600; color: #e8eaf0; }"
        "QLineEdit { background-color: #181b22; border: 0.5px solid rgba(255,255,255,0.1); border-radius: 6px; "
        "   padding: 8px 10px; color: #e8eaf0; font-size: 13px; }"
        "QLineEdit:focus { border: 0.5px solid #4f7fff; }"
        "QPushButton { border-radius: 6px; font-size: 13px; font-weight: 500; padding: 10px 18px; }"
        "QPushButton#PrimaryBtn { background-color: #4f7fff; color: white; border: none; }"
        "QPushButton#PrimaryBtn:hover { background-color: #3d6ef0; }"
        "QPushButton#PrimaryBtnOrange { background-color: #ff9142; color: #1a1206; border: none; font-weight: 600; }"
        "QPushButton#PrimaryBtnOrange:hover { background-color: #f07f2e; }"
        "QPushButton#GhostBtn { background-color: transparent; border: 0.5px solid rgba(255,255,255,0.12); color: #9aa0b8; }"
        "QPushButton#GhostBtn:hover { background-color: #1e2230; color: #e8eaf0; }"
        "QPushButton#OptionCard { text-align: left; background-color: #181b22; color: #b5bad0; "
        "   border: 1px solid rgba(255,255,255,0.08); border-radius: 10px; padding: 18px; font-size: 13px; font-weight: 500; }"
        "QPushButton#OptionCard:hover { border: 1px solid rgba(79,127,255,0.4); }"
        "QPushButton#OptionCard:checked { background-color: rgba(79,127,255,0.10); color: #e8eaf0; "
        "   border: 1px solid #4f7fff; }"
    );
}

} // namespace gui
