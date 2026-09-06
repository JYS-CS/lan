#include "DeviceTableModel.h"
#include <QRegularExpression>
#include <algorithm>

namespace gui {

DeviceTableModel::DeviceTableModel(QObject *parent) : QAbstractTableModel(parent), m_maxBw(0.0) {}

int DeviceTableModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return m_devices.size();
}

int DeviceTableModel::columnCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return ColumnCount;
}

qreal DeviceTableModel::parseBandwidth(const QString &bwStr) const {
    QRegularExpression re(R"(^([\d\.]+)\s*([KMGB]*)/s)");
    QRegularExpressionMatch match = re.match(bwStr);
    if (!match.hasMatch()) return 0.0;
    qreal val = match.captured(1).toDouble();
    QString unit = match.captured(2);
    if (unit == "K" || unit == "KB") val *= 1024;
    else if (unit == "M" || unit == "MB") val *= 1024 * 1024;
    else if (unit == "G" || unit == "GB") val *= 1024 * 1024 * 1024;
    return val;
}

void DeviceTableModel::recalculateMaxBw() {
    m_maxBw = 1.0; // avoid div by zero
    for (const auto &dev : m_devices) {
        qreal up = parseBandwidth(dev.upBandwidth());
        qreal down = parseBandwidth(dev.downBandwidth());
        m_maxBw = std::max({m_maxBw, up, down});
    }
}

QVariant DeviceTableModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_devices.size()) return QVariant();
    
    const core::Device &dev = m_devices[index.row()];
    
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case ColIP: return dev.ip();
            case ColMAC: return dev.mac();
            case ColHostname: return dev.alias().isEmpty() ? dev.hostname() : dev.alias();
            case ColUp: return dev.upBandwidth();
            case ColDown: return dev.downBandwidth();
            case ColStatus: return dev.status();
            case ColVendor: return dev.vendor();
            case ColBlock: return QVariant(); // painted by delegate
            default: return QVariant();
        }
    }

    if (role == SortRole) {
        switch (index.column()) {
            case ColIP: {
                QStringList parts = dev.ip().split('.');
                if (parts.size() != 4) return dev.ip();
                quint32 ipInt = (parts[0].toUInt() << 24) | (parts[1].toUInt() << 16) | (parts[2].toUInt() << 8) | parts[3].toUInt();
                return ipInt;
            }
            case ColUp: return parseBandwidth(dev.upBandwidth());
            case ColDown: return parseBandwidth(dev.downBandwidth());
            case ColStatus: {
                QString s = dev.status().toLower();
                if (s.contains("online")) return 0;
                if (s.contains("idle")) return 1;
                return 2;
            }
            default: return data(index, Qt::DisplayRole);
        }
    }
    
    if (role == IsHostRole) {
        return dev.status().toLower().contains("self");
    }
    
    if (role == MaxBwRole) {
        return m_maxBw;
    }
    
    if (role == RawDataRole) {
        if (index.column() == ColUp) return parseBandwidth(dev.upBandwidth());
        if (index.column() == ColDown) return parseBandwidth(dev.downBandwidth());
    }

    if (role == UpHistoryRole) {
        return QVariant::fromValue(m_upHistory.value(dev.mac()));
    }

    if (role == DownHistoryRole) {
        return QVariant::fromValue(m_downHistory.value(dev.mac()));
    }
    if (role == MacRole) {
        return dev.mac();
    }

    return QVariant();
}

QVariant DeviceTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        switch (section) {
            case ColIP: return "IP ADDRESS";
            case ColMAC: return "MAC ADDRESS";
            case ColHostname: return "HOSTNAME";
            case ColUp: return "UP";
            case ColDown: return "DOWN";
            case ColStatus: return "STATUS";
            case ColVendor: return "VENDOR";
            case ColBlock: return "ACTION";
            default: return QVariant();
        }
    }
    return QVariant();
}

void DeviceTableModel::updateDevices(const QList<core::Device> &devices) {
    beginResetModel();
    m_devices = devices;

    // Push new bandwidth samples into rolling history
    for (const auto &dev : devices) {
        const QString &mac = dev.mac();

        auto &upH = m_upHistory[mac];
        upH.append(parseBandwidth(dev.upBandwidth()));
        if (upH.size() > BW_HISTORY_LEN) upH.removeFirst();

        auto &downH = m_downHistory[mac];
        downH.append(parseBandwidth(dev.downBandwidth()));
        if (downH.size() > BW_HISTORY_LEN) downH.removeFirst();
    }

    recalculateMaxBw();
    endResetModel();
}

core::Device DeviceTableModel::deviceAt(int row) const {
    if (row >= 0 && row < m_devices.size()) return m_devices[row];
    return core::Device();
}

} // namespace gui
