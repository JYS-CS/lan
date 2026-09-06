#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QComboBox>
#include <QFrame>
#include <QTimer>
#include <QPropertyAnimation>
#include <deque>
#include "../core/NetworkManager.h"
#include "../core/Types.h"

namespace gui {

// ─────────────────────────────────────────────────────────────────────────────
// MiniSparkline — tiny real-time upload/download chart for the detail panel
// ─────────────────────────────────────────────────────────────────────────────
class MiniSparkline : public QWidget {
    Q_OBJECT
public:
    explicit MiniSparkline(QWidget *parent = nullptr);
    void addSample(quint32 rx, quint32 tx);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    static constexpr int kMaxPts = 60;
    std::deque<quint32> m_rx;
    std::deque<quint32> m_tx;
};

// ─────────────────────────────────────────────────────────────────────────────
// BandwidthDetailPanel — slide-in side panel for per-device details
// ─────────────────────────────────────────────────────────────────────────────
class BandwidthDetailPanel : public QWidget {
    Q_OBJECT
public:
    explicit BandwidthDetailPanel(QWidget *parent = nullptr);

    void showDevice(const core::DeviceBandwidth &dev,
                    const QList<core::IpHistoryEntry> &ipHistory);
    void updateLiveStats(const core::DeviceBandwidth &dev);
    void clearDevice();

    QString currentMac() const { return m_currentMac; }

signals:
    void closeRequested();

private:
    void setupUi();
    static QString formatBps(quint64 bytes, bool perSec = false);

    QString      m_currentMac;
    QLabel      *m_nameLabel     = nullptr;
    QLabel      *m_macLabel      = nullptr;
    QLabel      *m_vendorLabel   = nullptr;
    QLabel      *m_ipLabel       = nullptr;
    QLabel      *m_statusLabel   = nullptr;
    QLabel      *m_rxRateLabel   = nullptr;
    QLabel      *m_txRateLabel   = nullptr;
    QLabel      *m_peakRxLabel   = nullptr;
    QLabel      *m_peakTxLabel   = nullptr;
    QLabel      *m_rxTotalLabel  = nullptr;
    QLabel      *m_txTotalLabel  = nullptr;
    MiniSparkline *m_sparkline   = nullptr;
    QTableWidget  *m_ipHistTable = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// BandwidthPage — the main dedicated bandwidth monitoring page
// ─────────────────────────────────────────────────────────────────────────────
class BandwidthPage : public QWidget {
    Q_OBJECT

public:
    explicit BandwidthPage(core::NetworkManager *nm, QWidget *parent = nullptr);
    ~BandwidthPage() override = default;

public slots:
    void onBandwidthUpdated(const QList<core::DeviceBandwidth> &devices);
    void onTopTalkersUpdated(const QList<core::DeviceBandwidth> &top);
    void onLanStatsUpdated(quint64 rxTotal, quint64 txTotal,
                           quint32 rxRate,  quint32 txRate);
    void onTopologyDetected(core::TopologyCapability cap);

private slots:
    void onDeviceRowClicked(int row, int col);
    void onPeriodChanged(int index);
    void onDetailCloseRequested();
    void refreshHistoricalTalkers();

private:
    void setupUi();
    void applyTheme();
    QWidget* createStatCard(const QString &label, const QString &color,
                            QLabel **valuePtr, const QString &subLabel = QString());

    static QString fmtBps(quint64 bytes, bool perSec = false);
    static QString fmtBytes(quint64 bytes);

    core::NetworkManager       *m_nm;
    core::TopologyCapability    m_topology = core::TopologyCapability::Unsupported;

    // ── Overview cards ────────────────────────────────────────────────────
    QLabel *m_cardRxRate    = nullptr;
    QLabel *m_cardTxRate    = nullptr;
    QLabel *m_cardRxTotal   = nullptr;
    QLabel *m_cardTxTotal   = nullptr;
    QLabel *m_cardPeakRx    = nullptr;
    QLabel *m_cardDevices   = nullptr;
    QLabel *m_topologyBadge = nullptr;

    // Session peaks for the overview cards
    quint32 m_sessionPeakRx = 0;
    quint32 m_sessionPeakTx = 0;

    // ── Device table ──────────────────────────────────────────────────────
    QTableWidget *m_deviceTable = nullptr;
    QList<core::DeviceBandwidth> m_lastDevices;

    // ── Historical top talkers ────────────────────────────────────────────
    QComboBox    *m_periodCombo    = nullptr;
    QWidget      *m_topTalkersList = nullptr;
    QVBoxLayout  *m_topTalkersLayout = nullptr;

    // ── Detail panel ──────────────────────────────────────────────────────
    BandwidthDetailPanel      *m_detailPanel   = nullptr;
    QPropertyAnimation        *m_detailAnim    = nullptr;
    bool                       m_detailVisible = false;
    QString                    m_selectedMac;

    // Period mapping (combo index → seconds, 0 = all-time)
    static const qint64 kPeriods[];
};

} // namespace gui
