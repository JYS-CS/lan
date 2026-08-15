#pragma once

#include <QTableView>
#include <QSortFilterProxyModel>
#include "../core/Device.h"
#include "DeviceTableModel.h"
#include "DeviceTableDelegate.h"

namespace gui {

class DeviceTable : public QTableView {
    Q_OBJECT

public:
    explicit DeviceTable(QWidget *parent = nullptr);
    virtual ~DeviceTable() = default;

    void updateDevices(const QList<core::Device> &devices);
    void filterDevices(const QString &query);

signals:
    void whitelistRequested(const QString &mac);
    void aliasRequested(const QString &mac, const QString &currentAlias);
    void portScanRequested(const QString &ip);
    void staticRequested(QString mac, QString ip);
    void deviceSelected(QString ip);

private slots:
    void onCustomContextMenu(const QPoint &pos);
    void onRenameRequested(const QString &mac, const QString &oldAlias);

private:
    DeviceTableModel *m_model;
    QSortFilterProxyModel *m_proxy;
    DeviceTableDelegate *m_delegate;
    
    void initTable();
};

} // namespace gui
