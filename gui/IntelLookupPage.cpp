#include "IntelLookupPage.h"
#include "Theme.h"
#include <QMetaObject>

namespace gui {

IntelLookupPage::IntelLookupPage(core::NetworkManager *nm, QWidget *parent)
    : QWidget(parent), m_nm(nm)
{
    setupUi();
    applyTheme();

    if (m_nm) {
        connect(m_nm, &core::NetworkManager::intelStageChanged,     this, &IntelLookupPage::onStageChanged);
        connect(m_nm, &core::NetworkManager::intelResolvedIps,      this, &IntelLookupPage::onResolvedIps);
        connect(m_nm, &core::NetworkManager::intelReverseDnsResult, this, &IntelLookupPage::onReverseDnsResult);
        connect(m_nm, &core::NetworkManager::intelGeoResult,        this, &IntelLookupPage::onGeoResult);
        connect(m_nm, &core::NetworkManager::intelGeoLookupFailed,  this, &IntelLookupPage::onGeoLookupFailed);
        connect(m_nm, &core::NetworkManager::intelSubdomainsFound,  this, &IntelLookupPage::onSubdomainsFound);
        connect(m_nm, &core::NetworkManager::intelPortsFound,       this, &IntelLookupPage::onPortsFound);
        connect(m_nm, &core::NetworkManager::intelThreatStatus,     this, &IntelLookupPage::onThreatStatus);
        connect(m_nm, &core::NetworkManager::intelLookupFinished,   this, &IntelLookupPage::onLookupFinished);
        connect(m_nm, &core::NetworkManager::intelLookupFailed,     this, &IntelLookupPage::onLookupFailed);
    }
}

QWidget *IntelLookupPage::createResultCard(const QString &title, QLabel **bodyLabel) {
    QWidget *card = new QWidget(this);
    card->setObjectName("SectionCard");
    QVBoxLayout *l = new QVBoxLayout(card);
    l->setContentsMargins(18, 14, 18, 14);
    l->setSpacing(8);

    QLabel *titleLbl = new QLabel(title, card);
    titleLbl->setStyleSheet("font-family: 'JetBrains Mono', monospace; font-size: 9px; font-weight: 700; color: #4d5666; letter-spacing: 0.1em;");
    l->addWidget(titleLbl);

    *bodyLabel = new QLabel("—", card);
    (*bodyLabel)->setWordWrap(true);
    (*bodyLabel)->setStyleSheet("font-size: 13px; color: #c7cbe0; font-family: 'Inter';");
    l->addWidget(*bodyLabel);

    return card;
}

void IntelLookupPage::setupUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Header ───────────────────────────────────────────────────────────────
    QWidget *headerBar = new QWidget(this);
    headerBar->setObjectName("HeaderBar");
    QVBoxLayout *headerLayout = new QVBoxLayout(headerBar);
    headerLayout->setContentsMargins(24, 20, 24, 20);
    headerLayout->setSpacing(2);

    QLabel *eyebrow = new QLabel("RECON · IP & DOMAIN INTELLIGENCE", this);
    eyebrow->setStyleSheet("color: #7c8798; font-family: 'JetBrains Mono', monospace; font-size: 10px; font-weight: bold; letter-spacing: 0.15em;");
    QLabel *title = new QLabel("Intel Lookup", this);
    title->setStyleSheet("color: #dbe4ee; font-size: 22px; font-weight: bold; font-family: 'Inter', sans-serif;");
    headerLayout->addWidget(eyebrow);
    headerLayout->addWidget(title);
    root->addWidget(headerBar);

    // ── Search row ───────────────────────────────────────────────────────────
    QWidget *searchBar = new QWidget(this);
    QHBoxLayout *searchLayout = new QHBoxLayout(searchBar);
    searchLayout->setContentsMargins(24, 16, 24, 8);
    searchLayout->setSpacing(10);

    m_inputEdit = new QLineEdit(this);
    m_inputEdit->setPlaceholderText("Enter a domain (example.com) or an IP address…");
    connect(m_inputEdit, &QLineEdit::returnPressed, this, &IntelLookupPage::onLookupClicked);

    m_lookupBtn = new QPushButton("Lookup", this);
    m_lookupBtn->setObjectName("LookupBtn");
    m_lookupBtn->setCursor(Qt::PointingHandCursor);
    m_lookupBtn->setFixedHeight(36);
    connect(m_lookupBtn, &QPushButton::clicked, this, &IntelLookupPage::onLookupClicked);

    searchLayout->addWidget(m_inputEdit, 1);
    searchLayout->addWidget(m_lookupBtn);
    root->addWidget(searchBar);

    m_stageLabel = new QLabel("Enter a domain or IP to investigate it — subdomains (via certificate "
                               "transparency logs), reverse DNS, ASN/geolocation, open ports, and "
                               "threat-list status.", this);
    m_stageLabel->setWordWrap(true);
    m_stageLabel->setStyleSheet("color: #7c8798; font-size: 11px; font-family: 'Inter'; padding: 0 24px 12px 24px;");
    root->addWidget(m_stageLabel);

    // ── Scrollable results ───────────────────────────────────────────────────
    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scrollArea);
    auto *cv = new QVBoxLayout(content);
    cv->setContentsMargins(24, 0, 24, 20);
    cv->setSpacing(16);
    scrollArea->setWidget(content);
    root->addWidget(scrollArea, 1);

    QHBoxLayout *row1 = new QHBoxLayout();
    row1->setSpacing(16);
    row1->addWidget(createResultCard("RESOLVED IP(S)", &m_ipsLabel));
    row1->addWidget(createResultCard("REVERSE DNS", &m_reverseDnsLabel));
    cv->addLayout(row1);

    QHBoxLayout *row2 = new QHBoxLayout();
    row2->setSpacing(16);
    row2->addWidget(createResultCard("ASN / GEOLOCATION / ISP", &m_geoLabel));
    row2->addWidget(createResultCard("OPEN PORTS", &m_portsLabel));
    cv->addLayout(row2);

    // Threat status — its own emphasized card since it's the key
    // actionable signal
    QWidget *threatCard = new QWidget(content);
    threatCard->setObjectName("SectionCard");
    QHBoxLayout *threatLayout = new QHBoxLayout(threatCard);
    threatLayout->setContentsMargins(18, 14, 18, 14);
    QLabel *threatTitle = new QLabel("THREAT INTEL STATUS", threatCard);
    threatTitle->setStyleSheet("font-family: 'JetBrains Mono', monospace; font-size: 9px; font-weight: 700; color: #4d5666; letter-spacing: 0.1em;");
    m_threatBadge = new QLabel("—", threatCard);
    m_threatBadge->setObjectName("ThreatBadge");
    m_threatBadge->setAlignment(Qt::AlignCenter);
    m_threatBadge->setFixedHeight(26);
    threatLayout->addWidget(threatTitle);
    threatLayout->addStretch();
    threatLayout->addWidget(m_threatBadge);
    cv->addWidget(threatCard);

    // Subdomains
    QWidget *subCard = new QWidget(content);
    subCard->setObjectName("SectionCard");
    QVBoxLayout *subLayout = new QVBoxLayout(subCard);
    subLayout->setContentsMargins(18, 14, 18, 14);
    subLayout->setSpacing(8);

    QHBoxLayout *subHeaderRow = new QHBoxLayout();
    QLabel *subTitle = new QLabel("SUBDOMAINS (CERTIFICATE TRANSPARENCY)", subCard);
    subTitle->setStyleSheet("font-family: 'JetBrains Mono', monospace; font-size: 9px; font-weight: 700; color: #4d5666; letter-spacing: 0.1em;");
    m_subdomainsCountLabel = new QLabel("", subCard);
    m_subdomainsCountLabel->setStyleSheet("font-family: 'JetBrains Mono', monospace; font-size: 10px; color: #5eead4;");
    subHeaderRow->addWidget(subTitle);
    subHeaderRow->addStretch();
    subHeaderRow->addWidget(m_subdomainsCountLabel);
    subLayout->addLayout(subHeaderRow);

    m_subdomainsList = new QListWidget(subCard);
    m_subdomainsList->setFixedHeight(220);
    m_subdomainsList->setFocusPolicy(Qt::NoFocus);
    subLayout->addWidget(m_subdomainsList);
    cv->addWidget(subCard);

    cv->addStretch();

    // ── Footer ───────────────────────────────────────────────────────────────
    QWidget *footerBar = new QWidget(this);
    footerBar->setObjectName("FooterBar");
    QHBoxLayout *footerLayout = new QHBoxLayout(footerBar);
    footerLayout->setContentsMargins(24, 12, 24, 12);
    m_footerLabel = new QLabel("Sources: certificate transparency (crt.sh), ip-api.com, local reverse "
                                "DNS and port scan, and this app's own threat blocklist.", this);
    m_footerLabel->setStyleSheet("color: #7c8798; font-size: 11px; font-family: 'Inter', sans-serif;");
    footerLayout->addWidget(m_footerLabel);
    footerLayout->addStretch();
    root->addWidget(footerBar);
}

void IntelLookupPage::resetResults() {
    m_ipsLabel->setText("—");
    m_reverseDnsLabel->setText("—");
    m_geoLabel->setText("—");
    m_portsLabel->setText("—");
    m_threatBadge->setText("—");
    m_threatBadge->setStyleSheet("QLabel#ThreatBadge { background: rgba(255,255,255,0.04); color: #7c8798; "
        "border: 1px solid #1c232c; border-radius: 6px; padding: 0 14px; font-size: 10px; font-weight: 700; "
        "font-family: 'JetBrains Mono', monospace; }");
    m_subdomainsList->clear();
    m_subdomainsCountLabel->setText("");
}

void IntelLookupPage::onLookupClicked() {
    QString target = m_inputEdit->text().trimmed();
    if (target.isEmpty() || !m_nm) return;

    resetResults();
    m_lookupBtn->setEnabled(false);
    m_lookupBtn->setText("Looking up…");
    m_stageLabel->setText("Starting…");
    m_stageLabel->setStyleSheet("color: #5eead4; font-size: 11px; font-family: 'Inter'; padding: 0 24px 12px 24px;");

    QMetaObject::invokeMethod(m_nm, [this, target]() {
        m_nm->triggerIntelLookup(target);
    }, Qt::QueuedConnection);
}

void IntelLookupPage::onStageChanged(const QString &stage) {
    m_stageLabel->setText(stage);
}

void IntelLookupPage::onResolvedIps(const QStringList &ips) {
    m_ipsLabel->setText(ips.isEmpty() ? "—" : ips.join(", "));
}

void IntelLookupPage::onReverseDnsResult(const QString &hostname) {
    m_reverseDnsLabel->setText(hostname.isEmpty() ? "No PTR record" : hostname);
}

void IntelLookupPage::onGeoResult(const QString &country, const QString &city, const QString &isp, const QString &asn, const QString &org) {
    QStringList parts;
    if (!city.isEmpty() || !country.isEmpty()) parts << (city.isEmpty() ? country : city + ", " + country);
    if (!isp.isEmpty()) parts << isp;
    if (!org.isEmpty() && org != isp) parts << org;
    if (!asn.isEmpty()) parts << asn;
    m_geoLabel->setText(parts.isEmpty() ? "No data" : parts.join('\n'));
}

void IntelLookupPage::onGeoLookupFailed(const QString &error) {
    m_geoLabel->setText("Lookup failed: " + error);
}

void IntelLookupPage::onSubdomainsFound(const QStringList &subdomains) {
    if (subdomains.isEmpty()) {
        m_subdomainsCountLabel->setText("0 found");
        return;
    }
    m_subdomainsCountLabel->setText(QString("%1 found").arg(subdomains.size()));
    for (const QString &s : subdomains) m_subdomainsList->addItem(s);
}

void IntelLookupPage::onPortsFound(const QList<int> &ports) {
    if (ports.isEmpty()) {
        m_portsLabel->setText("None open (of the common ports checked)");
        return;
    }
    QStringList parts;
    for (int p : ports) parts << QString::number(p);
    m_portsLabel->setText(parts.join(", "));
}

void IntelLookupPage::onThreatStatus(bool malicious) {
    if (malicious) {
        m_threatBadge->setText("KNOWN MALICIOUS");
        m_threatBadge->setStyleSheet("QLabel#ThreatBadge { background: rgba(255,92,92,0.12); color: #ff5c5c; "
            "border: 1px solid rgba(255,92,92,0.3); border-radius: 6px; padding: 0 14px; font-size: 10px; "
            "font-weight: 700; font-family: 'JetBrains Mono', monospace; }");
    } else {
        m_threatBadge->setText("NOT ON BLOCKLIST");
        m_threatBadge->setStyleSheet("QLabel#ThreatBadge { background: rgba(52,228,160,0.10); color: #34e4a0; "
            "border: 1px solid rgba(52,228,160,0.3); border-radius: 6px; padding: 0 14px; font-size: 10px; "
            "font-weight: 700; font-family: 'JetBrains Mono', monospace; }");
    }
}

void IntelLookupPage::onLookupFinished() {
    m_lookupBtn->setEnabled(true);
    m_lookupBtn->setText("Lookup");
    m_stageLabel->setText("Done.");
    m_stageLabel->setStyleSheet("color: #34e4a0; font-size: 11px; font-family: 'Inter'; padding: 0 24px 12px 24px;");
}

void IntelLookupPage::onLookupFailed(const QString &error) {
    m_lookupBtn->setEnabled(true);
    m_lookupBtn->setText("Lookup");
    m_stageLabel->setText(error);
    m_stageLabel->setStyleSheet("color: #ff5c5c; font-size: 11px; font-family: 'Inter'; padding: 0 24px 12px 24px;");
}

void IntelLookupPage::applyTheme() {
    setStyleSheet(
        "gui--IntelLookupPage { background-color: #0a0d12; }"
        "QWidget#HeaderBar { background-color: transparent; border-bottom: 1px solid #1c232c; }"
        "QWidget#FooterBar { background-color: #0f141b; border-top: 1px solid #1c232c; }"
        "QWidget#SectionCard { background-color: #0f141b; border: 1px solid #1c232c; border-radius: 10px; }"

        "QLineEdit { background-color: #0f141b; border: 1px solid #1c232c; border-radius: 8px; "
        "color: #dbe4ee; font-size: 13px; padding: 8px 14px; }"
        "QLineEdit:focus { border: 1px solid rgba(94,234,212,0.4); }"

        "QPushButton#LookupBtn { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #5eead4, stop:1 #4f7fff); "
        "color: #0a0d12; font-weight: 700; font-size: 13px; border-radius: 8px; padding: 0 22px; border: none; }"
        "QPushButton#LookupBtn:hover { background: #5eead4; }"
        "QPushButton#LookupBtn:disabled { background: #1c232c; color: #4d5666; }"

        "QListWidget { background-color: #0a0d12; border: 1px solid #1c232c; border-radius: 6px; "
        "color: #c7cbe0; font-family: 'JetBrains Mono', monospace; font-size: 11px; }"
        "QListWidget::item { padding: 4px 10px; border-bottom: 1px solid #12181f; }"
        "QListWidget::item:hover { background: rgba(94,234,212,0.06); }"

        "QScrollBar:vertical { background: transparent; width: 8px; }"
        "QScrollBar::handle:vertical { background: #1c232c; border-radius: 4px; min-height: 20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    );
}

} // namespace gui
