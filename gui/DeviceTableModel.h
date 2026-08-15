#pragma once

#include <QAbstractTableModel>
#include <QList>
#include "../core/Device.h"

namespace gui {

class DeviceTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Columns {
        ColIP = 0,
        ColMAC,
        ColHostname,
        ColUp,
        ColDown,
        ColStatus,
        ColVendor,
        ColumnCount
    };
    
    enum Roles {
        SortRole = Qt::UserRole + 1,
        RawDataRole = Qt::UserRole + 2,
        IsHostRole = Qt::UserRole + 3,
        MaxBwRole = Qt::UserRole + 4
    };

    explicit DeviceTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void updateDevices(const QList<core::Device> &devices);
    core::Device deviceAt(int row) const;

private:
    QList<core::Device> m_devices;
    qreal m_maxBw;
    
    qreal parseBandwidth(const QString &bwStr) const;
    void recalculateMaxBw();
};

} // namespace gui
