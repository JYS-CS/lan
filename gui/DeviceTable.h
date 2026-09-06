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
    void setGatewayModeActive(bool active);

signals:
    void whitelistRequested(const QString &mac);
    void aliasRequested(const QString &mac, const QString &currentAlias);
    void portScanRequested(const QString &ip);
    void staticRequested(QString mac, QString ip);
    void deviceSelected(QString ip);
    void blockRequested(const QString &mac);

private slots:
    void onCustomContextMenu(const QPoint &pos);
    void onRenameRequested(const QString &mac, const QString &oldAlias);
    void applySettings();

protected:
    void mousePressEvent(QMouseEvent *event) override;

private:
    DeviceTableModel *m_model;
    QSortFilterProxyModel *m_proxy;
    DeviceTableDelegate *m_delegate;
    bool m_gatewayModeActive = false;
    
    void initTable();
};

} // namespace gui
