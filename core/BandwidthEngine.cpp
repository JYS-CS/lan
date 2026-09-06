#include "BandwidthEngine.h"
#include "DatabaseManager.h"

#include <QFile>
#include <QTextStream>
#include <QMutexLocker>
#include <QDebug>
#include <algorithm>

// Linux kernel proc files used for topology detection
static constexpr const char *kProcRoute   = "/proc/net/route";
static constexpr const char *kIpForward   = "/proc/sys/net/ipv4/ip_forward";

// Housekeeping: write DB samples every 5 seconds
static constexpr int kHousekeepingIntervalMs = 5000;
// Prune old data every hour
static constexpr int kPruneIntervalMs = 60 * 60 * 1000;

namespace core {

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────
BandwidthEngine::BandwidthEngine(QObject *parent)
    : QObject(parent)
{
    m_housekeepingTimer = new QTimer(this);
    m_housekeepingTimer->setInterval(kHousekeepingIntervalMs);
    connect(m_housekeepingTimer, &QTimer::timeout,
            this, &BandwidthEngine::onHousekeepingTimer);
    m_housekeepingTimer->start();

    m_pruneTimer = new QTimer(this);
    m_pruneTimer->setInterval(kPruneIntervalMs);
    connect(m_pruneTimer, &QTimer::timeout,
            this, &BandwidthEngine::onPruneTimer);
    m_pruneTimer->start();
}

// ─────────────────────────────────────────────────────────────────────────────
// Topology detection
// ─────────────────────────────────────────────────────────────────────────────
void BandwidthEngine::detectTopology(const QString &lanInterface)
{
    // 1. Check if IP forwarding is enabled
    bool ipForwardEnabled = false;
    {
        QFile f(kIpForward);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream ts(&f);
            QString val = ts.readLine().trimmed();
            ipForwardEnabled = (val == "1");
        }
    }

    // 2. Check /proc/net/route to see if this interface carries the default route
    //    Columns: Iface  Destination  Gateway  Flags  RefCnt  Use  Metric  Mask  MTU  Window  IRTT
    //    Default route has Destination == 00000000
    bool defaultRouteOnLanIface = false;
    bool hasDefaultRoute = false;
    {
        QFile f(kProcRoute);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream ts(&f);
            ts.readLine(); // skip header
            while (!ts.atEnd()) {
                QString line = ts.readLine().trimmed();
                if (line.isEmpty()) continue;
                QStringList parts = line.split('\t');
                if (parts.size() < 4) continue;
                QString iface       = parts[0].trimmed();
                QString destination = parts[1].trimmed();
                if (destination == "00000000") {
                    hasDefaultRoute = true;
                    if (iface == lanInterface)
                        defaultRouteOnLanIface = true;
                }
            }
        }
    }

    TopologyCapability cap;
    if (ipForwardEnabled && defaultRouteOnLanIface) {
        // This machine IS the gateway — all LAN↔WAN traffic passes through it.
        cap = TopologyCapability::FullGateway;
        qDebug() << "[BandwidthEngine] Topology: FullGateway (ip_forward=1, default route on" << lanInterface << ")";
    } else if (hasDefaultRoute) {
        // We have network access but are not in the forwarding path.
        // We can see broadcast/ARP and our own traffic via promiscuous capture,
        // but per-device WAN bandwidth cannot be reliably attributed.
        cap = TopologyCapability::PartialSniffer;
        qDebug() << "[BandwidthEngine] Topology: PartialSniffer (ip_forward=" << (ipForwardEnabled?1:0)
                 << ", defaultRouteOnLanIface=" << defaultRouteOnLanIface << ")";
    } else {
        cap = TopologyCapability::Unsupported;
        qDebug() << "[BandwidthEngine] Topology: Unsupported (no default route found)";
    }

    m_topology = cap;
    emit topologyDetected(cap);
}

// ─────────────────────────────────────────────────────────────────────────────
// DHCP / ARP map update
// ─────────────────────────────────────────────────────────────────────────────
void BandwidthEngine::updateMacIpMap(const QHash<QString, QString> &macToIp)
{
    QMutexLocker lk(&m_mutex);

    // Build the reverse map (ip → mac)
    m_ipToMac.clear();
    for (auto it = macToIp.begin(); it != macToIp.end(); ++it) {
        const QString &mac = it.key().toLower();
        const QString &ip  = it.value();
        m_ipToMac[ip] = mac;

        // If we already have a record for this MAC but the IP changed,
        // update the current IP and record the change in the DB
        if (m_records.contains(mac)) {
            if (m_records[mac].ip != ip) {
                qDebug() << "[BandwidthEngine] IP change for" << mac
                         << ":" << m_records[mac].ip << "->" << ip;
                m_records[mac].ip = ip;
                // Record IP change in DB (fire-and-forget on calling thread)
                DatabaseManager::instance().recordIpChange(mac, ip);
            }
        } else {
            // First time we see this MAC — create a skeletal record
            MacRecord r;
            r.mac = mac;
            r.ip  = ip;
            m_records.insert(mac, r);
            DatabaseManager::instance().recordIpChange(mac, ip);
        }
    }
}

void BandwidthEngine::updateDeviceInfo(const QString &mac,
                                       const QString &displayName,
                                       const QString &vendor,
                                       bool online)
{
    QMutexLocker lk(&m_mutex);
    const QString normMac = mac.toLower();
    if (!m_records.contains(normMac)) {
        MacRecord r;
        r.mac = normMac;
        m_records.insert(normMac, r);
    }
    m_records[normMac].displayName = displayName;
    m_records[normMac].vendor      = vendor;
    m_records[normMac].online      = online;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main stats processing (called every ~1 second from TrafficMonitor)
// ─────────────────────────────────────────────────────────────────────────────
void BandwidthEngine::onRawStats(const QMap<QString, TrafficStats> &ipStats)
{
    QMutexLocker lk(&m_mutex);

    quint64 aggregateRxTotal = 0;
    quint64 aggregateTxTotal = 0;
    quint32 aggregateRxRate  = 0;
    quint32 aggregateTxRate  = 0;

    // Process each IP entry from TrafficMonitor
    for (auto it = ipStats.begin(); it != ipStats.end(); ++it) {
        const QString      &ip    = it.key();
        const TrafficStats &stats = it.value();

        // Resolve IP → MAC
        QString mac = m_ipToMac.value(ip);
        if (mac.isEmpty()) {
            // Unknown MAC — still accumulate in a temporary record keyed by IP
            // so traffic isn't lost during ARP resolution
            mac = "ip:" + ip;
        }
        mac = mac.toLower();

        if (!m_records.contains(mac)) {
            MacRecord r;
            r.mac = mac;
            r.ip  = ip;
            m_records.insert(mac, r);
        }
        MacRecord &rec = m_records[mac];
        rec.ip = ip;
        rec.lastSeen = QDateTime::currentDateTime();

        // ── Delta / wrap-around safety ────────────────────────────────────
        // TrafficMonitor totalBytes are monotonically increasing since last
        // resetStats(). If totalBytesDown < prevTotalBytesDown it means
        // TrafficMonitor was reset (service restart, iface change, etc.).
        // In that case we skip the delta for this tick instead of producing
        // a huge bogus spike.
        quint64 rxDelta = 0;
        quint64 txDelta = 0;

        if (stats.totalBytesDown >= rec.prevTotalBytesDown) {
            rxDelta = stats.totalBytesDown - rec.prevTotalBytesDown;
        } else {
            qDebug() << "[BandwidthEngine] Counter reset detected for" << ip
                     << "(rx:" << rec.prevTotalBytesDown << "->" << stats.totalBytesDown << ")";
        }
        if (stats.totalBytesUp >= rec.prevTotalBytesUp) {
            txDelta = stats.totalBytesUp - rec.prevTotalBytesUp;
        }
        rec.prevTotalBytesDown = stats.totalBytesDown;
        rec.prevTotalBytesUp   = stats.totalBytesUp;

        // Accumulate into stable totals
        rec.rxTotal += rxDelta;
        rec.txTotal += txDelta;

        // Current rates come directly from TrafficMonitor (already smoothed)
        rec.rxRate = stats.currentRateDown;
        rec.txRate = stats.currentRateUp;

        // Update session peak (never decreases)
        if (rec.rxRate > rec.peakRx) rec.peakRx = rec.rxRate;
        if (rec.txRate > rec.peakTx) rec.peakTx = rec.txRate;

        // Aggregate LAN totals
        aggregateRxTotal += rec.rxTotal;
        aggregateTxTotal += rec.txTotal;
        aggregateRxRate  += rec.rxRate;
        aggregateTxRate  += rec.txRate;

        // Accumulate pending DB sample
        BwSample sample;
        sample.mac       = mac;
        sample.timestamp = QDateTime::currentSecsSinceEpoch();
        sample.rxBytes   = rec.rxTotal;
        sample.txBytes   = rec.txTotal;
        sample.rxRate    = rec.rxRate;
        sample.txRate    = rec.txRate;
        m_pendingSamples.append(sample);
    }

    // Build snapshot list
    QList<DeviceBandwidth> snapshot;
    snapshot.reserve(m_records.size());
    for (const MacRecord &r : m_records) {
        snapshot.append(toDeviceBandwidth(r));
    }

    // Top talkers — sorted by rxTotal + txTotal descending
    QList<DeviceBandwidth> topList = snapshot;
    std::sort(topList.begin(), topList.end(),
              [](const DeviceBandwidth &a, const DeviceBandwidth &b) {
                  return (a.rxTotal + a.txTotal) > (b.rxTotal + b.txTotal);
              });
    if (topList.size() > 20) topList = topList.mid(0, 20);

    // Release lock before emitting
    lk.unlock();

    emit bandwidthUpdated(snapshot);
    emit topTalkersUpdated(topList);
    emit lanStatsUpdated(aggregateRxTotal, aggregateTxTotal,
                         aggregateRxRate,  aggregateTxRate);
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
DeviceBandwidth BandwidthEngine::toDeviceBandwidth(const MacRecord &r) const
{
    DeviceBandwidth d;
    d.mac         = r.mac;
    d.ip          = r.ip;
    d.displayName = r.displayName.isEmpty() ? r.ip : r.displayName;
    d.vendor      = r.vendor;
    d.online      = r.online;
    d.rxRate      = r.rxRate;
    d.txRate      = r.txRate;
    d.peakRx      = r.peakRx;
    d.peakTx      = r.peakTx;
    d.rxTotal     = r.rxTotal;
    d.txTotal     = r.txTotal;
    d.lastSeen    = r.lastSeen;
    return d;
}

QList<DeviceBandwidth> BandwidthEngine::currentSnapshot() const
{
    QMutexLocker lk(&m_mutex);
    QList<DeviceBandwidth> out;
    out.reserve(m_records.size());
    for (const MacRecord &r : m_records)
        out.append(toDeviceBandwidth(r));
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Housekeeping — write pending samples to DB, aggregate old samples
// ─────────────────────────────────────────────────────────────────────────────
void BandwidthEngine::onHousekeepingTimer()
{
    QMutexLocker lk(&m_mutex);
    QList<BwSample> toWrite = m_pendingSamples;
    m_pendingSamples.clear();
    lk.unlock();

    // Write in one batch (called from the network thread which owns the DB)
    DatabaseManager &db = DatabaseManager::instance();
    for (const BwSample &s : toWrite) {
        db.insertBwSample(s);
    }
}

void BandwidthEngine::onPruneTimer()
{
    DatabaseManager::instance().pruneOldSamples();
}

} // namespace core
