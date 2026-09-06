#pragma once

#include <QAbstractTableModel>
#include <QList>
#include <QHash>
#include "../core/Device.h"

namespace gui {

static constexpr int BW_HISTORY_LEN = 14;

class DeviceTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Columns {
        ColIP = 0,
        ColMAC,
        ColHostname,
        ColType,
        ColLatency,
        ColUp,
        ColDown,
        ColStatus,
        ColVendor,
        ColBlock,   // Inline "Block" button column
        ColumnCount
    };

    enum Roles {
        SortRole       = Qt::UserRole + 1,
        RawDataRole    = Qt::UserRole + 2,
        IsHostRole     = Qt::UserRole + 3,
        MaxBwRole      = Qt::UserRole + 4,
        UpHistoryRole  = Qt::UserRole + 5,
        DownHistoryRole= Qt::UserRole + 6,
        MacRole        = Qt::UserRole + 7
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

    // Rolling bandwidth history keyed by MAC: list of up to BW_HISTORY_LEN values (bytes/s)
    QHash<QString, QList<qreal>> m_upHistory;
    QHash<QString, QList<qreal>> m_downHistory;

    qreal parseBandwidth(const QString &bwStr) const;
    void recalculateMaxBw();
};

} // namespace gui
