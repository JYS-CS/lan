#pragma once

#include <QObject>
#include <QHash>
#include <QMap>
#include <QList>
#include <QTimer>
#include <QElapsedTimer>
#include <QDateTime>
#include <QMutex>
#include "Types.h"

namespace core {

// ─────────────────────────────────────────────────────────────────────────────
// BandwidthEngine
//
// The MAC-keyed accounting layer that sits between TrafficMonitor (IP-keyed)
// and the GUI / database. Key responsibilities:
//
//   1. Joins TrafficMonitor's QMap<ip, TrafficStats> with a MAC→IP map
//      provided by DHCPManager to produce stable MAC-keyed bandwidth records.
//
//   2. Carries historical totals forward if DHCP assigns a new IP to a device
//      (the device's MAC is the permanent key — not its IP address).
//
//   3. Detects topology (gateway vs sniffer) by reading /proc at startup and
//      emits a TopologyCapability so the UI can label metrics correctly.
//
//   4. Writes aggregated samples to the database for historical queries.
//
//   5. Detects and discards negative/wrap-around counter deltas.
//
// Thread safety: All public slots are called from the network thread.
// Signals are emitted from the network thread; GUI connects with
// Qt::QueuedConnection (automatic for cross-thread signals).
// ─────────────────────────────────────────────────────────────────────────────
class BandwidthEngine : public QObject {
    Q_OBJECT

public:
    explicit BandwidthEngine(QObject *parent = nullptr);
    ~BandwidthEngine() override = default;

    // Called once after construction to determine the network topology.
    void detectTopology(const QString &lanInterface);

    // Update the MAC→IP mapping from DHCP leases.
    // Called whenever a lease is issued, renewed, or expires.
    void updateMacIpMap(const QHash<QString, QString> &macToIp);

    // Associate a display name (alias/hostname) with a MAC address.
    void updateDeviceInfo(const QString &mac,
                          const QString &displayName,
                          const QString &vendor,
                          bool online);

    TopologyCapability topology() const { return m_topology; }

    // Retrieve the current live snapshot for all known devices.
    QList<DeviceBandwidth> currentSnapshot() const;

public slots:
    // Receives the raw per-IP stats from TrafficMonitor every second.
    void onRawStats(const QMap<QString, TrafficStats> &ipStats);

signals:
    // Emitted every second with fresh per-device bandwidth.
    void bandwidthUpdated(const QList<core::DeviceBandwidth> &devices);

    // Emitted every second with top-20 devices sorted by rxTotal+txTotal.
    void topTalkersUpdated(const QList<core::DeviceBandwidth> &topDevices);

    // Emitted every second with aggregate LAN rates.
    void lanStatsUpdated(quint64 rxTotal, quint64 txTotal,
                         quint32 rxRate,  quint32 txRate);

    // Emitted once after detectTopology() completes.
    void topologyDetected(core::TopologyCapability capability);

private slots:
    void onHousekeepingTimer();   // aggregation + DB writes (every 5 s)
    void onPruneTimer();          // retention enforcement (every 1 h)

private:
    // ── Internal device record (stable across IP changes) ─────────────────
    struct MacRecord {
        QString  mac;
        QString  ip;          // current IP (may change)
        QString  displayName;
        QString  vendor;
        bool     online = false;

        // Accumulated totals — never reset when IP changes
        quint64  rxTotal  = 0;
        quint64  txTotal  = 0;
        quint32  rxRate   = 0;
        quint32  txRate   = 0;
        quint32  peakRx   = 0;
        quint32  peakTx   = 0;
        QDateTime lastSeen;

        // Snapshot from the previous stat cycle (for delta calculation)
        quint64  prevTotalBytesDown = 0;
        quint64  prevTotalBytesUp   = 0;
    };

    // ── Helpers ───────────────────────────────────────────────────────────
    QString    resolveIpToMac(const QString &ip) const;
    DeviceBandwidth toDeviceBandwidth(const MacRecord &r) const;
    void       writeSamplesToDb();

    // ── State ─────────────────────────────────────────────────────────────
    QHash<QString, MacRecord> m_records;   // mac → record
    QHash<QString, QString>   m_ipToMac;   // ip  → mac  (from DHCP + ARP)

    TopologyCapability m_topology = TopologyCapability::Unsupported;

    QTimer        *m_housekeepingTimer = nullptr;
    QTimer        *m_pruneTimer        = nullptr;

    // Pending samples accumulated between housekeeping ticks
    QList<BwSample> m_pendingSamples;

    mutable QMutex m_mutex;
};

} // namespace core
