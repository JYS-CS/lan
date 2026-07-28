#include "DeviceTable.h"
#include <QHeaderView>
#include <QMenu>
#include <QLabel>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QIcon>

namespace gui {

DeviceTable::DeviceTable(QWidget *parent) : QTableWidget(parent) {
    initTable();
}

void DeviceTable::initTable() {
    QStringList headers = { "IP ADDRESS", "MAC ADDRESS", "HOSTNAME", "UP", "DOWN", "STATUS", "VENDOR" };
    setColumnCount(headers.size());
    setHorizontalHeaderLabels(headers);
    
    horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    horizontalHeader()->setStretchLastSection(true);
    horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    verticalHeader()->setVisible(false);
    verticalHeader()->setDefaultSectionSize(48); // Tall rows for a premium feel
    
    setAlternatingRowColors(false);
    setShowGrid(false);
    setFocusPolicy(Qt::NoFocus);
    
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &DeviceTable::customContextMenuRequested, this, &DeviceTable::onCustomContextMenu);
    
    connect(this, &DeviceTable::itemSelectionChanged, this, [this]() {
        if (currentRow() >= 0) {
            emit deviceSelected(item(currentRow(), 0)->text());
        }
    });
}

void DeviceTable::updateDevices(const QList<core::Device> &devices) {
    setSortingEnabled(false);
    
    // 1. Identify which IPs are in the new list
    QSet<QString> currentIps;
    for (const auto &d : devices) currentIps.insert(d.ip());

    // 2. Remove rows for devices no longer present
    for (int i = rowCount() - 1; i >= 0; --i) {
        QString ip = item(i, 0)->text();
        if (!currentIps.contains(ip)) {
            removeRow(i);
        }
    }

    // 3. Rebuild map (indices might have shifted)
    m_rowMap.clear();
    for (int i = 0; i < rowCount(); ++i) {
        m_rowMap.insert(item(i, 0)->text(), i);
    }

    // 4. Update or Insert
    for (const auto &dev : devices) {
        int row = -1;
        bool isNew = false;
        
        if (m_rowMap.contains(dev.ip())) {
            row = m_rowMap[dev.ip()];
        } else {
            row = rowCount();
            insertRow(row);
            isNew = true;
            m_rowMap.insert(dev.ip(), row);

            // Initialize items for new row
            for(int c=0; c<columnCount(); ++c) setItem(row, c, new QTableWidgetItem(""));
        }

        // Updating cells
        // 1. IP
        item(row, 0)->setText(dev.ip());
        if (isNew) {
            item(row, 0)->setForeground(QColor("#4f7fff"));
            item(row, 0)->setFont(QFont("monospace", 11));
        }

        // 2. MAC
        item(row, 1)->setText(dev.mac());
        if (isNew) {
            item(row, 1)->setForeground(QColor("#7c8299"));
            item(row, 1)->setFont(QFont("monospace", 10));
        }

        // 3. Hostname
        item(row, 2)->setText(dev.hostname());

        // 4. Bandwidth Up
        item(row, 3)->setText(dev.upBandwidth());
        if (isNew) {
            item(row, 3)->setForeground(QColor("#2dd98f"));
            item(row, 3)->setFont(QFont("monospace", 10));
        }

        // 5. Bandwidth Down
        item(row, 4)->setText(dev.downBandwidth());
        if (isNew) {
            item(row, 4)->setForeground(QColor("#7c8299"));
            item(row, 4)->setFont(QFont("monospace", 10));
        }

        // 6. Status Badge (Update logic)
        updateStatusBadge(row, dev.status());
        
        // Data for searching
        item(row, 5)->setData(Qt::UserRole, dev.status());

        // 7. Vendor (with icon)
        QString vendor = dev.vendor();
        QString icon = getVendorIcon(vendor);
        item(row, 6)->setText(icon + " " + vendor);
        if (isNew) {
            item(row, 6)->setForeground(QColor("#7c8299"));
            item(row, 6)->setFont(QFont("Arial", 10));
        }

        // 8. Alias update (Display alias instead of hostname or IP if set)
        if (!dev.alias().isEmpty()) {
            item(row, 2)->setText(dev.alias());
            item(row, 2)->setForeground(QColor("#e8eaf0"));
            item(row, 2)->setFont(QFont("Arial", 10, QFont::Bold));
        } else {
            item(row, 2)->setText(dev.hostname());
            item(row, 2)->setForeground(QColor("#7c8299"));
            item(row, 2)->setFont(QFont("Arial", 10, QFont::Normal));
        }

    }

    setSortingEnabled(true);
    sortByColumn(5, Qt::AscendingOrder); // Online at top (after sorting logic tweak)
}

void DeviceTable::updateStatusBadge(int row, const QString &status) {
    QWidget *existing = cellWidget(row, 5);
    bool needsCreate = !existing;
    
    // Simple helper to find labels
    auto findLabel = [](QWidget *host, const QString &objName) -> QLabel* {
        if (!host) return nullptr;
        return host->findChild<QLabel*>(objName);
    };

    if (needsCreate) {
        QWidget *badgeHost = new QWidget();
        QHBoxLayout *badgeLayout = new QHBoxLayout(badgeHost);
        badgeLayout->setContentsMargins(10, 8, 10, 8);
        
        QWidget *badge = new QWidget();
        badge->setObjectName("BadgeFrame");
        QHBoxLayout *l = new QHBoxLayout(badge);
        l->setContentsMargins(8, 4, 8, 4);
        l->setSpacing(6);
        
        QLabel *dot = new QLabel();
        dot->setObjectName("StatusDot");
        dot->setFixedSize(6, 6);
        
        QLabel *txt = new QLabel();
        txt->setObjectName("StatusText");
        txt->setStyleSheet("font-size: 10px; font-weight: bold; background: transparent;");
        
        l->addWidget(dot);
        l->addWidget(txt);
        badgeLayout->addWidget(badge);
        badgeLayout->addStretch();
        setCellWidget(row, 5, badgeHost);
        existing = badgeHost;
        
        // Ensure the background text is transparent so it doesn't blink or overlap
        item(row, 5)->setForeground(Qt::transparent);
        item(row, 5)->setFont(QFont("Arial", 1)); // Minimal size
    }

    QWidget *frame = existing->findChild<QWidget*>("BadgeFrame");
    QLabel *dot = existing->findChild<QLabel*>("StatusDot");
    QLabel *txt = existing->findChild<QLabel*>("StatusText");

    if (!frame || !dot || !txt) return;

    txt->setText(status.toUpper());
    
    // Set sorting priority (0 = high, 3 = low)
    if (status.toLower().contains("online")) {
        item(row, 5)->setText("0"); // Online at top
        frame->setStyleSheet("background: rgba(45,217,143,0.12); border: 0.5px solid rgba(45,217,143,0.2); border-radius: 4px;");
        dot->setStyleSheet("background: #2dd98f; border-radius: 3px;");
        txt->setStyleSheet("color: #2dd98f; font-size: 10px; font-weight: bold;");
    } else if (status.toLower().contains("idle")) {
        item(row, 5)->setText("1");
        frame->setStyleSheet("background: rgba(245,166,35,0.12); border: 0.5px solid rgba(245,166,35,0.2); border-radius: 4px;");
        dot->setStyleSheet("background: #f5a623; border-radius: 3px;");
        txt->setStyleSheet("color: #f5a623; font-size: 10px; font-weight: bold;");
    } else if (status.toLower().contains("offline")) {
        item(row, 5)->setText("3"); // Offline lower down
        frame->setStyleSheet("background: rgba(74,80,104,0.12); border: 0.5px solid rgba(74,80,104,0.2); border-radius: 4px;");
        dot->setStyleSheet("background: #4a5068; border-radius: 3px;");
        txt->setStyleSheet("color: #4a5068; font-size: 10px; font-weight: bold;");
    }
}

void DeviceTable::filterDevices(const QString &query) {
    QString q = query.toLower();
    for (int i = 0; i < rowCount(); ++i) {
        bool match = false;
        for (int j = 0; j < columnCount(); ++j) {
            QTableWidgetItem *it = item(i, j);
            if (it && (it->text().toLower().contains(q) || it->data(Qt::UserRole).toString().toLower().contains(q))) {
                match = true;
                break;
            }
        }
        setRowHidden(i, !match);
    }
}

void DeviceTable::onCustomContextMenu(const QPoint &pos) {
    int row = rowAt(pos.y());
    if (row < 0) return;
    
    QString ip = item(row, 0)->text();
    QString mac = item(row, 1)->text();
    
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #181b22; border: 0.5px solid rgba(255,255,255,0.12); border-radius: 8px; padding: 4px; }"
        "QMenu::item { padding: 8px 20px; border-radius: 5px; color: #7c8299; font-size: 12px; }"
        "QMenu::item:selected { background-color: #1e2230; color: #e8eaf0; }"
        "QMenu::separator { height: 1px; background: rgba(255,255,255,0.07); margin: 4px 10px; }"
    );
    
    QAction *staticAct = menu.addAction("Assign Static Lease");
    QAction *copyIp = menu.addAction("Copy IP Address");
    QAction *copyMac = menu.addAction("Copy MAC Address");
    menu.addSeparator();
    QAction *rename = menu.addAction("Rename Device");
    QAction *audit = menu.addAction("Audit Services (Scan Ports)");
    menu.addSeparator();
    QAction *whitelist = menu.addAction("Whitelist — Allow device");
    whitelist->setProperty("safe", true);

    QAction *selected = menu.exec(viewport()->mapToGlobal(pos));
    if (!selected) return;

    if (selected == staticAct) emit staticRequested(mac, ip);
    else if (selected == copyIp) { /* Copy IP */ }
    else if (selected == copyMac) { /* Copy MAC */ }
    else if (selected == rename) onRenameRequested(mac, item(row, 2)->text());
    else if (selected == audit) emit portScanRequested(ip);
    else if (selected == whitelist) emit whitelistRequested(mac);
}


void DeviceTable::onRenameRequested(const QString &mac, const QString &oldAlias) {
    bool ok;
    QString text = QInputDialog::getText(this, tr("Rename Device"),
                                         tr("Display Name:"), QLineEdit::Normal,
                                         oldAlias, &ok);
    if (ok && !text.isEmpty()) {
        emit aliasRequested(mac, text);
    }
}

QString DeviceTable::getVendorIcon(const QString &vendor) {
    QString v = vendor.toLower();
    if (v.contains("apple")) return "🍎";
    if (v.contains("microsoft") || v.contains("intel")) return "💻";
    if (v.contains("google") || v.contains("android")) return "📱";
    if (v.contains("cisco") || v.contains("tp-link") || v.contains("netgear") || v.contains("linksys")) return "🌐";
    if (v.contains("raspberry") || v.contains("arduino")) return "🥧";
    if (v.contains("samsung") || v.contains("xiaomi") || v.contains("huawei")) return "📱";
    if (v.contains("sony") || v.contains("nintendo") || v.contains("playstation")) return "🎮";
    return "•";
}

} // namespace gui
