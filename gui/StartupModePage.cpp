#include "StartupModePage.h"
#include <QFrame>

namespace gui {

StartupModePage::StartupModePage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    applyTheme();
}

void StartupModePage::buildUi() {
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(36, 80, 36, 28);
    root->setSpacing(20);
    root->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

    // ── Heading ──────────────────────────────────────────────────────────────
    QLabel *title = new QLabel("Select Operating Mode", this);
    title->setObjectName("PageTitle");
    title->setAlignment(Qt::AlignCenter);
    root->addWidget(title);

    QLabel *subtitle = new QLabel(
        "How should this machine participate in the network?\n"
        "You can change this from the DHCP page at any time.", this);
    subtitle->setObjectName("PageSubtitle");
    subtitle->setAlignment(Qt::AlignCenter);
    root->addWidget(subtitle);

    // ── Container for the cards to restrict maximum width ─────────────────────
    QWidget *cardsContainer = new QWidget(this);
    cardsContainer->setMaximumWidth(800);
    QVBoxLayout *cardsContainerLayout = new QVBoxLayout(cardsContainer);
    cardsContainerLayout->setContentsMargins(0,0,0,0);
    cardsContainerLayout->setSpacing(20);

    // ── Mode cards ───────────────────────────────────────────────────────────
    QHBoxLayout *cardsRow = new QHBoxLayout();
    cardsRow->setSpacing(18);

    // Normal card
    QWidget *normalCard = new QWidget(cardsContainer);
    normalCard->setObjectName("ModeCard");
    QVBoxLayout *nl = new QVBoxLayout(normalCard);
    nl->setContentsMargins(22, 22, 22, 22);
    nl->setSpacing(10);
    QLabel *nIcon  = new QLabel("🌐", normalCard); nIcon->setObjectName("ModeIcon"); nIcon->setAlignment(Qt::AlignCenter);
    QLabel *nTitle = new QLabel("Normal Mode", normalCard); nTitle->setObjectName("ModeCardTitle"); nTitle->setAlignment(Qt::AlignCenter);
    QLabel *nDesc  = new QLabel("Passive monitoring only.\nYour router handles IP assignment.\n\n⚠  Per-device traffic details\nnot visible in this mode.", normalCard);
    nDesc->setObjectName("ModeCardDesc"); nDesc->setAlignment(Qt::AlignCenter); nDesc->setWordWrap(true);
    m_normalBtn = new QPushButton("Continue in Normal Mode", normalCard);
    m_normalBtn->setObjectName("GhostBtn"); m_normalBtn->setCursor(Qt::PointingHandCursor);
    nl->addWidget(nIcon); nl->addWidget(nTitle); nl->addWidget(nDesc); nl->addStretch(); nl->addWidget(m_normalBtn);

    // DHCP card
    QWidget *dhcpCard = new QWidget(cardsContainer);
    dhcpCard->setObjectName("ModeCard");
    QVBoxLayout *dl = new QVBoxLayout(dhcpCard);
    dl->setContentsMargins(22, 22, 22, 22);
    dl->setSpacing(10);
    QLabel *dIcon  = new QLabel("📡", dhcpCard); dIcon->setObjectName("ModeIcon"); dIcon->setAlignment(Qt::AlignCenter);
    QLabel *dTitle = new QLabel("DHCP Server Mode", dhcpCard); dTitle->setObjectName("ModeCardTitle"); dTitle->setAlignment(Qt::AlignCenter);
    QLabel *dDesc  = new QLabel("This machine becomes the DHCP\nauthority. Full lease control.\nIn Intercept mode: all device traffic\nroutes through you — fully visible.", dhcpCard);
    dDesc->setObjectName("ModeCardDesc"); dDesc->setAlignment(Qt::AlignCenter); dDesc->setWordWrap(true);
    m_dhcpBtn = new QPushButton("Enable DHCP Server  →", dhcpCard);
    m_dhcpBtn->setObjectName("PrimaryBtn"); m_dhcpBtn->setCursor(Qt::PointingHandCursor);
    dl->addWidget(dIcon); dl->addWidget(dTitle); dl->addWidget(dDesc); dl->addStretch(); dl->addWidget(m_dhcpBtn);

    cardsRow->addWidget(normalCard, 1);
    cardsRow->addWidget(dhcpCard,   1);
    cardsContainerLayout->addLayout(cardsRow);

    // ── DHCP sub-options (hidden until DHCP chosen) ───────────────────────────
    m_dhcpOptions = new QWidget(cardsContainer);
    m_dhcpOptions->setObjectName("DhcpOptions");
    m_dhcpOptions->setVisible(false);
    QVBoxLayout *optVl = new QVBoxLayout(m_dhcpOptions);
    optVl->setContentsMargins(0, 4, 0, 0);
    optVl->setSpacing(10);

    // Warning banner
    // Warning banner
    m_warningLabel = new QLabel(
        "⚠  <b>Important:</b> Disable DHCP on your router before starting the server, "
        "otherwise clients will receive conflicting IP configurations.", m_dhcpOptions);
    m_warningLabel->setObjectName("WarnLabel");
    m_warningLabel->setWordWrap(true);
    m_warningLabel->setAlignment(Qt::AlignLeft);
    optVl->addWidget(m_warningLabel);

    m_confirmBtn = new QPushButton("I've disabled my router's DHCP — Continue to Setup", m_dhcpOptions);
    m_confirmBtn->setObjectName("PrimaryBtn");
    m_confirmBtn->setCursor(Qt::PointingHandCursor);
    // Add stretch to push the button below the label nicely
    optVl->addSpacing(10);
    optVl->addWidget(m_confirmBtn, 0, Qt::AlignRight);

    cardsContainerLayout->addWidget(m_dhcpOptions);
    root->addWidget(cardsContainer);

    // ── Wire up ───────────────────────────────────────────────────────────────
    connect(m_normalBtn,  &QPushButton::clicked, this, &StartupModePage::onNormalChosen);
    connect(m_dhcpBtn,    &QPushButton::clicked, this, &StartupModePage::onDhcpChosen);
    connect(m_confirmBtn, &QPushButton::clicked, this, [this]() {
        m_mode      = Mode::DHCPServer;
        m_intercept = false; // They configure this on the actual DHCP page
        emit modeSelected(m_mode, m_intercept);
    });
}

void StartupModePage::onNormalChosen() {
    m_mode      = Mode::Normal;
    m_intercept = false;
    emit modeSelected(m_mode, m_intercept);
}

void StartupModePage::onDhcpChosen() {
    // Reveal the sub-options / warning instead of immediately accepting
    m_dhcpOptions->setVisible(true);
    m_dhcpBtn->setEnabled(false);   // prevent double-click
    adjustSize();
}

void StartupModePage::applyTheme() {
    setStyleSheet(
        "StartupModePage { background-color: #0d1117; }"
        "QLabel { color: #e8eaf0; border: none; background: transparent; }"
        "QLabel#PageTitle { font-size: 24px; font-weight: 700; color: #e8eaf0; }"
        "QLabel#PageSubtitle { font-size: 14px; color: #5a6175; margin-bottom: 20px; }"
        "QLabel#SectionLabel { font-size: 11px; letter-spacing: 0.08em; color: #4a5068; }"
        "QLabel#WarnLabel { font-size: 13px; color: #f5a623; background: rgba(245,166,35,0.08); "
        "   border: 0.5px solid rgba(245,166,35,0.25); border-radius: 6px; padding: 12px 16px; }"
        "QWidget#ModeCard { background-color: #181b22; border: 0.5px solid rgba(255,255,255,0.08); border-radius: 12px; }"
        "QWidget#DhcpOptions { background: transparent; }"
        "QLabel#ModeIcon { font-size: 36px; }"
        "QLabel#ModeCardTitle { font-size: 16px; font-weight: 600; color: #e8eaf0; }"
        "QLabel#ModeCardDesc { font-size: 13px; color: #6b7db3; line-height: 1.5; }"
        "QRadioButton { color: #c8ccd8; font-size: 13px; background: transparent; spacing: 8px; }"
        "QRadioButton::indicator { width: 16px; height: 16px; border-radius: 8px; border: 1.5px solid #4a5068; background: #1a1e28; }"
        "QRadioButton::indicator:checked { background: #4f7fff; border: 1.5px solid #4f7fff; }"
        "QPushButton { border-radius: 6px; font-size: 13px; font-weight: 500; padding: 10px 18px; }"
        "QPushButton#PrimaryBtn { background-color: #4f7fff; color: white; border: none; }"
        "QPushButton#PrimaryBtn:hover { background-color: #3d6ef0; }"
        "QPushButton#GhostBtn { background-color: transparent; border: 0.5px solid rgba(255,255,255,0.12); color: #7c8299; }"
        "QPushButton#GhostBtn:hover { background-color: #1e2230; color: #e8eaf0; }"
    );
}

} // namespace gui
