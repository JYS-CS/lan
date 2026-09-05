#include "BlockedDevicesPage.h"
#include "Theme.h"
#include <QHeaderView>
#include <QMetaObject>
#include <QDateTime>
#include <QColor>
#include <QFont>

namespace gui {

BlockedDevicesPage::BlockedDevicesPage(core::NetworkManager *nm, QWidget *parent)
    : QWidget(parent), m_nm(nm)
{
    setupUi();
    applyTheme();

    if (m_nm) {
        connect(m_nm, &core::NetworkManager::blockedDevicesReady, this, &BlockedDevicesPage::onBlockedDevicesReady);
        connect(m_nm, &core::NetworkManager::deviceBlocked,   this, [this](const QString &) { refresh(); });
        connect(m_nm, &core::NetworkManager::deviceUnblocked, this, [this](const QString &) { refresh(); });
    }
    refresh();
}

void BlockedDevicesPage::refresh() {
    if (!m_nm) return;
    QMetaObject::invokeMethod(m_nm, "requestBlockedDevices", Qt::QueuedConnection);
}

QWidget *BlockedDevicesPage::createStatCard(const QString &label, const QString &color, QLabel **valuePtr) {
    QWidget *card = new QWidget(this);
    card->setObjectName("StatCard");
    QVBoxLayout *l = new QVBoxLayout(card);
    l->setContentsMargins(16, 16, 16, 16);
    l->setSpacing(4);

    QLabel *lbl = new QLabel(label, this);
    lbl->setStyleSheet(QString("font-family: 'JetBrains Mono', monospace; font-size: 10px; font-weight: bold; color: %1; letter-spacing: 0.1em; background: transparent;").arg(color));

    *valuePtr = new QLabel("0", this);
    (*valuePtr)->setStyleSheet("font-family: 'JetBrains Mono', monospace; font-size: 24px; font-weight: 300; color: #dbe4ee; background: transparent;");

    l->addWidget(lbl);
    l->addWidget(*valuePtr);
    return card;
}

void BlockedDevicesPage::setupUi() {
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
    QLabel *eyebrow = new QLabel("SECURITY · ACCESS CONTROL", this);
    eyebrow->setStyleSheet("color: #7c8798; font-family: 'JetBrains Mono', monospace; font-size: 10px; font-weight: bold; letter-spacing: 0.15em;");
    QLabel *title = new QLabel("Blocked Devices", this);
    title->setStyleSheet("color: #dbe4ee; font-size: 22px; font-weight: bold; font-family: 'Inter', sans-serif;");
    titleLayout->addWidget(eyebrow);
    titleLayout->addWidget(title);

    headerLayout->addLayout(titleLayout);
    headerLayout->addStretch();

    QPushButton *refreshBtn = new QPushButton("Refresh", this);
    refreshBtn->setObjectName("RefreshBtn");
    refreshBtn->setCursor(Qt::PointingHandCursor);
    refreshBtn->setFixedHeight(30);
    connect(refreshBtn, &QPushButton::clicked, this, &BlockedDevicesPage::refresh);
    headerLayout->addWidget(refreshBtn);

    root->addWidget(headerBar);

    // ── Stat strip ───────────────────────────────────────────────────────────
    QWidget *statStrip = new QWidget(this);
    QHBoxLayout *statLayout = new QHBoxLayout(statStrip);
    statLayout->setContentsMargins(24, 0, 24, 20);
    statLayout->setSpacing(16);
    statLayout->addWidget(createStatCard("TOTAL BLOCKED", "#ff5c5c", &m_statTotal));
    statLayout->addStretch(3);
    root->addWidget(statStrip);

    // ── Table ────────────────────────────────────────────────────────────────
    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({"MAC ADDRESS", "IP ADDRESS", "HOSTNAME", "REASON", "BLOCKED AT", ""});
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(false);
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
    m_footerLiveDot->setStyleSheet("background-color: #34e4a0; border-radius: 4px;");

    m_footerCountLabel = new QLabel("0 devices blocked", this);
    m_footerCountLabel->setStyleSheet("color: #7c8798; font-size: 11px; font-family: 'Inter', sans-serif;");

    footerLayout->addWidget(m_footerLiveDot);
    footerLayout->addWidget(m_footerCountLabel);
    footerLayout->addStretch();
    root->addWidget(footerBar);
}

void BlockedDevicesPage::onBlockedDevicesReady(const QVariantList &entries) {
    m_table->setRowCount(0);
    m_statTotal->setText(QString::number(entries.size()));
    m_footerCountLabel->setText(QString("%1 device%2 blocked").arg(entries.size()).arg(entries.size() == 1 ? "" : "s"));

    for (const QVariant &v : entries) {
        QVariantMap m = v.toMap();
        QString mac = m.value("mac").toString();

        int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setRowHeight(row, 40);

        auto makeItem = [](const QString &text) {
            auto *item = new QTableWidgetItem(text.isEmpty() ? "—" : text);
            item->setForeground(QColor("#c7cbe0"));
            return item;
        };
        auto *macItem = makeItem(mac.toUpper());
        macItem->setFont(QFont("JetBrains Mono", 10, QFont::Bold));
        m_table->setItem(row, 0, macItem);
        m_table->setItem(row, 1, makeItem(m.value("ip").toString()));
        m_table->setItem(row, 2, makeItem(m.value("hostname").toString()));
        m_table->setItem(row, 3, makeItem(m.value("reason").toString()));

        QDateTime blockedAt = QDateTime::fromString(m.value("blockedAt").toString(), Qt::ISODate);
        m_table->setItem(row, 4, makeItem(blockedAt.isValid() ? blockedAt.toString("yyyy-MM-dd hh:mm") : "—"));

        QPushButton *unblockBtn = new QPushButton("Unblock", this);
        unblockBtn->setObjectName("UnblockBtn");
        unblockBtn->setCursor(Qt::PointingHandCursor);
        unblockBtn->setFixedHeight(26);
        connect(unblockBtn, &QPushButton::clicked, this, [this, mac]() {
            if (m_nm) QMetaObject::invokeMethod(m_nm, "unblockDevice", Qt::QueuedConnection, Q_ARG(QString, mac));
        });
        m_table->setCellWidget(row, 5, unblockBtn);
    }

    if (entries.isEmpty()) {
        m_table->setRowCount(1);
        m_table->setSpan(0, 0, 1, 6);
        auto *empty = new QTableWidgetItem("No devices are currently blocked.");
        empty->setForeground(QColor("#4d5666"));
        empty->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(0, 0, empty);
        m_table->setRowHeight(0, 60);
    }
}

void BlockedDevicesPage::applyTheme() {
    setStyleSheet(
        "gui--BlockedDevicesPage { background-color: #0a0d12; }"
        "QWidget#HeaderBar { background-color: transparent; border-bottom: 1px solid #1c232c; }"
        "QWidget#FooterBar { background-color: #0f141b; border-top: 1px solid #1c232c; }"
        "QWidget#StatCard { background-color: #0f141b; border: 1px solid #1c232c; border-radius: 8px; }"

        "QTableWidget { background-color: #0a0d12; border: 1px solid #1c232c; border-radius: 8px; gridline-color: transparent; }"
        "QTableWidget::item { padding: 6px 10px; border-bottom: 1px solid #12181f; }"
        "QHeaderView::section { background: #0f141b; color: #7c8798; font-family: 'JetBrains Mono', monospace; "
        "font-size: 10px; font-weight: bold; letter-spacing: 0.08em; padding: 10px; border: none; border-bottom: 1px solid #1c232c; }"

        "QPushButton#RefreshBtn { background: #0f141b; border: 1px solid #1c232c; color: #dbe4ee; "
        "font-size: 12px; font-weight: 600; border-radius: 7px; padding: 6px 16px; }"
        "QPushButton#RefreshBtn:hover { border: 1px solid rgba(94,234,212,0.4); }"

        "QPushButton#UnblockBtn { background: rgba(52,228,160,0.10); border: 1px solid rgba(52,228,160,0.30); "
        "color: #34e4a0; font-size: 11px; font-weight: 700; border-radius: 6px; padding: 0 12px; margin: 4px 6px; }"
        "QPushButton#UnblockBtn:hover { background: rgba(52,228,160,0.18); }"

        "QScrollBar:vertical { background: transparent; width: 8px; }"
        "QScrollBar::handle:vertical { background: #1c232c; border-radius: 4px; min-height: 20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    );
}

} // namespace gui
