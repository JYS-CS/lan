#include "BlockedDevicesPage.h"
#include "Theme.h"
#include <QHeaderView>
#include <QMetaObject>
#include <QDateTime>
#include <QColor>
#include <QFont>
#include "../core/DatabaseManager.h"

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
        connect(m_nm, &core::NetworkManager::gatewayModeChanged, this, &BlockedDevicesPage::onGatewayModeChanged);
    }
    
    // Initial state
    onGatewayModeChanged(m_nm ? m_nm->isGatewayModeActive() : false);
    refresh();
}

void BlockedDevicesPage::refresh() {
    if (!m_nm) return;
    QMetaObject::invokeMethod(m_nm, "requestBlockedDevices", Qt::QueuedConnection);
}

QWidget *BlockedDevicesPage::createStatCard(const QString &label, const QString &color, QLabel **valuePtr) {
    QWidget *container = new QWidget(this);
    container->setObjectName("HeaderStat");
    container->setToolTip(label);
    QHBoxLayout *layout = new QHBoxLayout(container);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(6);

    QLabel *iconLabel = new QLabel(container);
    iconLabel->setPixmap(Theme::tintedIcon(":/resources/icon_blocked_devices.svg", 16, color).pixmap(16, 16));
    iconLabel->setFixedSize(16, 16);

    *valuePtr = new QLabel("0", container);
    (*valuePtr)->setStyleSheet(QString("color: %1; font-weight: bold; font-family: 'Inter', sans-serif; font-size: 14px; background: transparent;").arg(color));

    layout->addWidget(iconLabel);
    layout->addWidget(*valuePtr);
    return container;
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

    headerLayout->addStretch();

    QVBoxLayout *titleLayout = new QVBoxLayout();
    titleLayout->setSpacing(2);
    titleLayout->setAlignment(Qt::AlignCenter);
    QLabel *eyebrow = new QLabel("SECURITY · ACCESS CONTROL", this);
    eyebrow->setAlignment(Qt::AlignCenter);
    eyebrow->setStyleSheet("color: #7c8798; font-family: 'JetBrains Mono', monospace; font-size: 10px; font-weight: bold; letter-spacing: 0.15em;");
    QLabel *title = new QLabel("Blocked Devices", this);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("color: #dbe4ee; font-size: 22px; font-weight: bold; font-family: 'Inter', sans-serif;");
    titleLayout->addWidget(eyebrow);
    titleLayout->addWidget(title);

    headerLayout->addLayout(titleLayout);
    headerLayout->addSpacing(32); // Space between title and search

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName("SearchEdit");
    m_searchEdit->setPlaceholderText("Filter devices...");
    m_searchEdit->setFixedHeight(30);
    m_searchEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &BlockedDevicesPage::onSearchChanged);
    headerLayout->addWidget(m_searchEdit);
    headerLayout->addSpacing(16);

    headerLayout->addWidget(createStatCard("Total Blocked", "#ff5c5c", &m_statTotal));
    headerLayout->addSpacing(12);

    m_refreshBtn = new QPushButton(this);
    m_refreshBtn->setObjectName("IconBtn");
    m_refreshBtn->setFixedSize(30, 30);
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    m_refreshBtn->setToolTip("Refresh");

    auto createStaticIcon = [](const QString &color) {
        QPixmap base(24, 24);
        base.fill(Qt::transparent);
        QPainter p(&base);
        p.drawPixmap(4, 4, Theme::tintedIcon(":/resources/refresh.svg", 15, color).pixmap(15, 15));
        return QIcon(base);
    };

    m_refreshBtn->setIcon(createStaticIcon("#7c8798")); // Theme::OpsTextDim
    m_refreshBtn->setIconSize(QSize(24, 24));
    
    m_spinAnim = new QVariantAnimation(this);
    m_spinAnim->setDuration(600);
    m_spinAnim->setStartValue(0.0);
    m_spinAnim->setEndValue(360.0);
    connect(m_spinAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        QPixmap base(24, 24);
        base.fill(Qt::transparent);
        QPainter p(&base);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.translate(12, 12);
        p.rotate(value.toReal());
        p.drawPixmap(-7, -7, Theme::tintedIcon(":/resources/refresh.svg", 15, "#34e4a0").pixmap(15, 15)); // OpsAccentGreen
        m_refreshBtn->setIcon(QIcon(base));
    });
    connect(m_spinAnim, &QVariantAnimation::finished, this, [this, createStaticIcon]() {
        m_refreshBtn->setIcon(createStaticIcon("#7c8798"));
    });

    connect(m_refreshBtn, &QPushButton::clicked, this, [this]() {
        if (m_spinAnim->state() != QAbstractAnimation::Running) {
            m_spinAnim->start();
        }
        refresh();
    });
    headerLayout->addWidget(m_refreshBtn);

    root->addWidget(headerBar);
    
    // ── DHCP Warning Banner ──────────────────────────────────────────────────
    m_dhcpWarningLabel = new QLabel(this);
    m_dhcpWarningLabel->setAlignment(Qt::AlignCenter);
    m_dhcpWarningLabel->setWordWrap(true);
    m_dhcpWarningLabel->setStyleSheet("color: #ffcc00; font-family: 'Inter', sans-serif; font-size: 12px; font-weight: 500; background: #332b00; padding: 8px; border: 1px solid #665500; border-radius: 4px; margin-left: 24px; margin-right: 24px;");
    root->addWidget(m_dhcpWarningLabel);
    m_dhcpWarningLabel->hide(); // Hidden by default, updated by onGatewayModeChanged

    // ── Table ────────────────────────────────────────────────────────────────
    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({"MAC ADDRESS", "IP ADDRESS", "HOSTNAME", "REASON", "BLOCKED AT", "ACTION"});
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    m_table->setColumnWidth(5, 110);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    connect(m_table, &QTableWidget::itemChanged, this, &BlockedDevicesPage::onItemChanged);
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
    m_populating = true;
    m_table->setRowCount(0);
    m_statTotal->setText(QString::number(entries.size()));
    m_footerCountLabel->setText(QString("%1 device%2 blocked").arg(entries.size()).arg(entries.size() == 1 ? "" : "s"));

    for (const QVariant &v : entries) {
        QVariantMap m = v.toMap();
        QString mac = m.value("mac").toString();

        int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setRowHeight(row, 40);

        auto makeItem = [](const QString &text, bool editable = false) {
            auto *item = new QTableWidgetItem(text.isEmpty() ? "—" : text);
            item->setForeground(QColor("#c7cbe0"));
            item->setFont(QFont("Inter", 11));
            if (!editable) item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            return item;
        };
        auto *macItem = makeItem(mac.toUpper());
        macItem->setForeground(QColor("#7c8798")); // Theme::OpsTextDim
        macItem->setFont(QFont("JetBrains Mono", 11));
        m_table->setItem(row, 0, macItem);
        
        auto *ipItem = makeItem(m.value("ip").toString());
        ipItem->setForeground(QColor("#4f7fff")); // Theme::AccentBlue
        ipItem->setFont(QFont("JetBrains Mono", 11, QFont::Bold));
        m_table->setItem(row, 1, ipItem);
        
        m_table->setItem(row, 2, makeItem(m.value("hostname").toString()));
        
        auto *reasonItem = makeItem(m.value("reason").toString(), true);
        reasonItem->setToolTip("Double-click to edit the block reason");
        m_table->setItem(row, 3, reasonItem);

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
    
    // Apply current search filter
    onSearchChanged(m_searchEdit->text());
    m_populating = false;
}

void BlockedDevicesPage::onItemChanged(QTableWidgetItem *item) {
    if (m_populating || !item) return;
    if (item->column() == 3) {
        QString newReason = item->text();
        QTableWidgetItem *macItem = m_table->item(item->row(), 0);
        if (macItem) {
            QString mac = macItem->text();
            core::DatabaseManager::instance().updateBlacklistReason(m_nm->gatewayMac(), mac, newReason);
        }
    }
}

void BlockedDevicesPage::onSearchChanged(const QString &text) {
    for (int i = 0; i < m_table->rowCount(); ++i) {
        bool match = false;
        // Search in columns 0, 1, 2, 3
        for (int col = 0; col <= 3; ++col) {
            auto *item = m_table->item(i, col);
            if (item && item->text().contains(text, Qt::CaseInsensitive)) {
                match = true;
                break;
            }
        }
        // Also keep "No devices" row visible if present
        if (m_table->item(i, 0) && m_table->item(i, 0)->text() == "No devices are currently blocked.") {
            match = true;
        }
        m_table->setRowHidden(i, !match);
    }
}

void BlockedDevicesPage::applyTheme() {
    setStyleSheet(
        "gui--BlockedDevicesPage { background-color: #0a0d12; }"
        "QWidget#HeaderBar { background-color: transparent; border-bottom: 1px solid #1c232c; }"
        "QWidget#FooterBar { background-color: #0f141b; border-top: 1px solid #1c232c; }"
        "QWidget#HeaderStat { background: rgba(255,255,255,0.03); border: 1px solid #1c232c; border-radius: 6px; }"
        
        "QLineEdit#SearchEdit { background: #0f141b; border: 1px solid #1c232c; border-radius: 6px; padding: 0 12px; color: #dbe4ee; font-size: 11px; font-family: 'Inter', sans-serif; }"
        "QLineEdit#SearchEdit:focus { border-color: #34e4a0; background: #12181f; }"

        "QTableWidget { background-color: #0a0d12; border: 1px solid #1c232c; border-radius: 8px; gridline-color: transparent; }"
        "QTableWidget::item { padding: 6px 10px; border-bottom: 1px solid #1c232c; }"
        "QHeaderView::section { background: #0a0d12; color: #dbe4ee; font-family: 'JetBrains Mono', monospace; "
        "font-size: 11px; font-weight: bold; text-transform: uppercase; letter-spacing: 0.1em; "
        "padding: 12px 15px; border: none; border-bottom: 1px solid #1c232c; }"

        "QPushButton#RefreshBtn { background: #0f141b; border: 1px solid #1c232c; color: #dbe4ee; "
        "font-size: 12px; font-weight: 600; border-radius: 7px; padding: 6px 16px; }"
        "QPushButton#RefreshBtn:hover { border: 1px solid rgba(94,234,212,0.4); }"
        
        "QPushButton#IconBtn { background: transparent; border: 1px solid #1c232c; border-radius: 15px; }"
        "QPushButton#IconBtn:hover { background: rgba(52,228,160,0.07); border-color: rgba(52,228,160,0.4); }"
        "QPushButton#IconBtn:pressed { background: rgba(52,228,160,0.14); }"

        "QPushButton#UnblockBtn { background: rgba(52,228,160,0.10); border: 1px solid rgba(52,228,160,0.30); "
        "color: #34e4a0; font-size: 11px; font-weight: 700; border-radius: 6px; padding: 0 12px; margin: 4px 6px; }"
        "QPushButton#UnblockBtn:hover { background: rgba(52,228,160,0.18); }"

        "QScrollBar:vertical { background: transparent; width: 8px; }"
        "QScrollBar::handle:vertical { background: #1c232c; border-radius: 4px; min-height: 20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    );
}

void BlockedDevicesPage::onGatewayModeChanged(bool active) {
    if (active) {
        m_dhcpWarningLabel->hide();
    } else {
        m_dhcpWarningLabel->setText("DHCP Server is currently OFF. Blocking functionality requires DHCP Gateway mode to be enabled. The devices shown here are isolated to the current network.");
        m_dhcpWarningLabel->show();
    }
}

} // namespace gui
