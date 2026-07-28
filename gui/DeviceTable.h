#pragma once

#include <QTableWidget>
#include <QHeaderView>
#include "../core/Device.h"

namespace gui {

class DeviceTable : public QTableWidget {
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
    QString getVendorIcon(const QString &vendor);

private:
    void initTable();
    void updateStatusBadge(int row, const QString &status);
    QMap<QString, int> m_rowMap; // IP -> RowIndex
};

} // namespace gui
