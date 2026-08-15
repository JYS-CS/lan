#include "DeviceTable.h"
#include <QHeaderView>
#include <QMenu>
#include <QInputDialog>
#include "Theme.h"

namespace gui {

DeviceTable::DeviceTable(QWidget *parent) : QTableView(parent) {
    initTable();
}

void DeviceTable::initTable() {
    m_model = new DeviceTableModel(this);
    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setFilterKeyColumn(-1);
    m_proxy->setSortRole(DeviceTableModel::SortRole);

    setModel(m_proxy);
    
    m_delegate = new DeviceTableDelegate(this);
    setItemDelegate(m_delegate);
    
    horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    horizontalHeader()->setStretchLastSection(true);
    horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    horizontalHeader()->setStyleSheet(
        "QHeaderView::section { background: #0a0d12; color: #7c8798; font-family: 'JetBrains Mono', monospace; "
        "font-size: 10px; font-weight: bold; text-transform: uppercase; letter-spacing: 0.1em; "
        "padding: 12px 15px; border: none; border-bottom: 1px solid #1c232c; }"
        "QHeaderView::section:hover { color: #dbe4ee; }"
        "QHeaderView { background: #0a0d12; border: none; }"
    );
    
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    verticalHeader()->setVisible(false);
    
    setAlternatingRowColors(false);
    setShowGrid(false);
    setFocusPolicy(Qt::NoFocus);
    setSortingEnabled(true);
    sortByColumn(DeviceTableModel::ColStatus, Qt::AscendingOrder);
    
    setStyleSheet(
        "QTableView { background-color: #0a0d12; border: none; }"
        "QTableView::item { border: none; }" // border painted in delegate
    );
    
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &DeviceTable::customContextMenuRequested, this, &DeviceTable::onCustomContextMenu);
    
    connect(selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() {
        QModelIndexList selected = selectionModel()->selectedRows();
        if (!selected.isEmpty()) {
            QModelIndex srcIndex = m_proxy->mapToSource(selected.first());
            core::Device dev = m_model->deviceAt(srcIndex.row());
            emit deviceSelected(dev.ip());
        }
    });
}

void DeviceTable::updateDevices(const QList<core::Device> &devices) {
    m_model->updateDevices(devices);
}

void DeviceTable::filterDevices(const QString &query) {
    m_proxy->setFilterFixedString(query);
}

void DeviceTable::onCustomContextMenu(const QPoint &pos) {
    QModelIndex index = indexAt(pos);
    if (!index.isValid()) return;
    
    QModelIndex srcIndex = m_proxy->mapToSource(index);
    core::Device dev = m_model->deviceAt(srcIndex.row());
    
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #12181f; border: 1px solid #1c232c; border-radius: 8px; padding: 4px; }"
        "QMenu::item { padding: 8px 20px; border-radius: 5px; color: #7c8798; font-size: 12px; }"
        "QMenu::item:selected { background-color: #1c232c; color: #dbe4ee; }"
        "QMenu::separator { height: 1px; background: #1c232c; margin: 4px 10px; }"
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

    if (selected == staticAct) emit staticRequested(dev.mac(), dev.ip());
    else if (selected == copyIp) { /* Copy IP */ }
    else if (selected == copyMac) { /* Copy MAC */ }
    else if (selected == rename) onRenameRequested(dev.mac(), dev.alias().isEmpty() ? dev.hostname() : dev.alias());
    else if (selected == audit) emit portScanRequested(dev.ip());
    else if (selected == whitelist) emit whitelistRequested(dev.mac());
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

} // namespace gui
