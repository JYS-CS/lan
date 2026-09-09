#include "DnsActivityPage.h"
#include "Theme.h"
#include <QHeaderView>
#include <QDateTime>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace gui {

DnsActivityPage::DnsActivityPage(core::NetworkManager *nm, QWidget *parent)
    : QWidget(parent), m_nm(nm)
{
    setupUi();
    applyTheme();

    if (m_nm) {
        connect(m_nm, &core::NetworkManager::dnsQueryLogUpdated, this, &DnsActivityPage::onQueryLogUpdated);
    }
    refresh();
}

QWidget *DnsActivityPage::createStatCard(const QString &label, const QString &color, QLabel **valuePtr) {
    QWidget *card = new QWidget(this);
    card->setObjectName("StatCard");
    QVBoxLayout *l = new QVBoxLayout(card);
    l->setContentsMargins(16, 16, 16, 16);
    l->setSpacing(4);
    QLabel *lbl = new QLabel(label, this);
    lbl->setStyleSheet(QString("font-family: 'JetBrains Mono', monospace; font-size: 10px; font-weight: bold; color: %1; letter-spacing: 0.1em;").arg(color));
    *valuePtr = new QLabel("0", this);
    (*valuePtr)->setStyleSheet("font-family: 'JetBrains Mono', monospace; font-size: 24px; font-weight: 300; color: #dbe4ee;");
    l->addWidget(lbl);
    l->addWidget(*valuePtr);
    return card;
}

void DnsActivityPage::setupUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Header ───────────────────────────────────────────────────────────────
    QWidget *headerBar = new QWidget(this);
    headerBar->setObjectName("HeaderBar");
    QHBoxLayout *headerLayout = new QHBoxLayout(headerBar);
    headerLayout->setContentsMargins(24, 20, 24, 20);

    QVBoxLayout *titleLayout = new QVBoxLayout();
    titleLayout->setSpacing(2);
    QLabel *eyebrow = new QLabel("DNS · BROWSING ACTIVITY", this);
    eyebrow->setStyleSheet("color: #7c8798; font-family: 'JetBrains Mono', monospace; font-size: 10px; font-weight: bold; letter-spacing: 0.15em;");
    QLabel *title = new QLabel("DNS Activity", this);
    title->setStyleSheet("color: #dbe4ee; font-size: 22px; font-weight: bold; font-family: 'Inter', sans-serif;");
    titleLayout->addWidget(eyebrow);
    titleLayout->addWidget(title);
    headerLayout->addLayout(titleLayout);
    headerLayout->addStretch();

    m_refreshBtn = new QPushButton("Refresh", this);
    m_refreshBtn->setObjectName("RefreshBtn");
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    m_refreshBtn->setFixedHeight(30);
    connect(m_refreshBtn, &QPushButton::clicked, this, &DnsActivityPage::refresh);
    headerLayout->addWidget(m_refreshBtn);
    root->addWidget(headerBar);

    // ── Stat strip ───────────────────────────────────────────────────────────
    QWidget *statStrip = new QWidget(this);
    QHBoxLayout *statLayout = new QHBoxLayout(statStrip);
    statLayout->setContentsMargins(24, 16, 24, 12);
    statLayout->setSpacing(16);
    statLayout->addWidget(createStatCard("TOTAL QUERIES", "#5eead4", &m_statTotal));
    statLayout->addWidget(createStatCard("BLOCKED", "#ff5c5c", &m_statBlocked));
    statLayout->addWidget(createStatCard("UNIQUE DOMAINS", "#4f7fff", &m_statUnique));
    statLayout->addStretch(2);
    root->addWidget(statStrip);

    // ── Toolbar ──────────────────────────────────────────────────────────────
    QWidget *toolbar = new QWidget(this);
    QHBoxLayout *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(24, 0, 24, 12);
    toolbarLayout->setSpacing(10);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Filter by domain or device IP…");
    m_searchEdit->setFixedWidth(280);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &DnsActivityPage::onFilterChanged);

    m_statusFilter = new QComboBox(this);
    m_statusFilter->addItems({"All Queries", "Allowed Only", "Blocked Only"});
    m_statusFilter->setFixedWidth(140);
    connect(m_statusFilter, &QComboBox::currentIndexChanged, this, &DnsActivityPage::onFilterChanged);

    toolbarLayout->addWidget(m_searchEdit);
    toolbarLayout->addWidget(m_statusFilter);
    toolbarLayout->addStretch();
    root->addWidget(toolbar);

    // ── Table ────────────────────────────────────────────────────────────────
    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({"TIME", "DEVICE", "DOMAIN", "TYPE", "STATUS"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setFocusPolicy(Qt::NoFocus);

    QVBoxLayout *tableWrap = new QVBoxLayout();
    tableWrap->setContentsMargins(24, 0, 24, 16);
    tableWrap->addWidget(m_table);
    root->addLayout(tableWrap, 1);

    // ── Footer ───────────────────────────────────────────────────────────────
    QWidget *footerBar = new QWidget(this);
    footerBar->setObjectName("FooterBar");
    QHBoxLayout *footerLayout = new QHBoxLayout(footerBar);
    footerLayout->setContentsMargins(24, 12, 24, 12);
    m_footerLiveDot = new QLabel(this);
    m_footerLiveDot->setFixedSize(8, 8);
    m_footerLiveDot->setStyleSheet("background-color: #4d5666; border-radius: 4px;");
    m_footerLabel = new QLabel("DNS resolver not running — enable in Settings", this);
    m_footerLabel->setStyleSheet("color: #7c8798; font-size: 11px; font-family: 'Inter', sans-serif;");
    footerLayout->addWidget(m_footerLiveDot);
    footerLayout->addWidget(m_footerLabel);
    footerLayout->addStretch();
    root->addWidget(footerBar);
}

bool DnsActivityPage::passesFilter(const core::DnsLogEntry &entry) const {
    QString filterText = m_searchEdit->text().trimmed().toLower();
    if (!filterText.isEmpty() &&
        !entry.domain.contains(filterText) && !entry.clientIp.contains(filterText)) {
        return false;
    }
    int statusIdx = m_statusFilter->currentIndex();
    if (statusIdx == 1 && entry.blocked) return false;  // Allowed only
    if (statusIdx == 2 && !entry.blocked) return false; // Blocked only
    return true;
}

void DnsActivityPage::insertRow(const core::DnsLogEntry &entry, bool atTop) {
    int row = atTop ? 0 : m_table->rowCount();
    m_table->insertRow(row);
    m_table->setRowHeight(row, 32);

    auto makeItem = [](const QString &text, const QColor &color = QColor("#c7cbe0")) {
        auto *item = new QTableWidgetItem(text);
        item->setForeground(color);
        return item;
    };

    QDateTime ts = QDateTime::fromString(entry.timestamp, Qt::ISODate);
    m_table->setItem(row, 0, makeItem(ts.isValid() ? ts.toString("hh:mm:ss") : entry.timestamp));
    m_table->setItem(row, 1, makeItem(entry.clientIp));

    auto *domainItem = makeItem(entry.domain);
    domainItem->setFont(QFont("JetBrains Mono", 10));
    m_table->setItem(row, 2, domainItem);
    m_table->setItem(row, 3, makeItem(entry.qtype, QColor("#7c8798")));

    QString statusText = entry.blocked ? "BLOCKED" : (entry.cached ? "CACHED" : "ALLOWED");
    QColor statusColor = entry.blocked ? QColor("#ff5c5c") : (entry.cached ? QColor("#5eead4") : QColor("#34e4a0"));
    auto *statusItem = makeItem(statusText, statusColor);
    statusItem->setFont(QFont("JetBrains Mono", 9, QFont::Bold));
    m_table->setItem(row, 4, statusItem);
}

void DnsActivityPage::refresh() {
    if (!m_nm) return;
    m_table->setRowCount(0);
    m_uniqueDomains.clear();

    m_running = m_nm->isDnsProxyRunning();
    m_footerLiveDot->setStyleSheet(QString("background-color: %1; border-radius: 4px;").arg(m_running ? "#34e4a0" : "#4d5666"));
    m_footerLabel->setText(m_running ? "Live — new queries appear automatically" : "DNS resolver not running — enable in Settings");

    auto entries = m_nm->getRecentDnsQueries(500);
    int blocked = 0;
    for (const auto &e : entries) {
        m_uniqueDomains.insert(e.domain);
        if (e.blocked) blocked++;
        if (passesFilter(e)) insertRow(e, false);
    }

    m_statTotal->setText(QString::number(m_nm->countDnsQueries(false)));
    m_statBlocked->setText(QString::number(m_nm->countDnsQueries(true)));
    m_statUnique->setText(QString::number(m_uniqueDomains.size()));
}

void DnsActivityPage::onFilterChanged() {
    refresh();
}

void DnsActivityPage::onQueryLogUpdated(const core::DnsLogEntry &entry) {
    m_uniqueDomains.insert(entry.domain);
    if (passesFilter(entry)) {
        insertRow(entry, true);
        if (m_table->rowCount() > 500) m_table->removeRow(m_table->rowCount() - 1);
    }
    m_statTotal->setText(QString::number(m_statTotal->text().toInt() + 1));
    if (entry.blocked) m_statBlocked->setText(QString::number(m_statBlocked->text().toInt() + 1));
    m_statUnique->setText(QString::number(m_uniqueDomains.size()));

    if (!m_running) {
        m_running = true;
        m_footerLiveDot->setStyleSheet("background-color: #34e4a0; border-radius: 4px;");
        m_footerLabel->setText("Live — new queries appear automatically");
    }
}

void DnsActivityPage::applyTheme() {
    setStyleSheet(
        "gui--DnsActivityPage { background-color: #0a0d12; }"
        "QWidget#HeaderBar { background-color: transparent; border-bottom: 1px solid #1c232c; }"
        "QWidget#FooterBar { background-color: #0f141b; border-top: 1px solid #1c232c; }"
        "QWidget#StatCard { background-color: #0f141b; border: 1px solid #1c232c; border-radius: 8px; }"

        "QTableWidget { background-color: #0a0d12; border: 1px solid #1c232c; border-radius: 8px; gridline-color: transparent; }"
        "QTableWidget::item { padding: 4px 10px; border-bottom: 1px solid #12181f; }"
        "QHeaderView::section { background: #0f141b; color: #7c8798; font-family: 'JetBrains Mono', monospace; "
        "font-size: 10px; font-weight: bold; letter-spacing: 0.08em; padding: 8px; border: none; border-bottom: 1px solid #1c232c; }"

        "QPushButton#RefreshBtn { background: #0f141b; border: 1px solid #1c232c; color: #dbe4ee; "
        "font-size: 12px; font-weight: 600; border-radius: 7px; padding: 0 16px; }"
        "QPushButton#RefreshBtn:hover { border: 1px solid rgba(94,234,212,0.4); }"

        "QScrollBar:vertical { background: transparent; width: 8px; }"
        "QScrollBar::handle:vertical { background: #1c232c; border-radius: 4px; min-height: 20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    );
}

} // namespace gui
