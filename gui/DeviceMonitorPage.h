#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QDateTime>
#include <QPropertyAnimation>
#include "DeviceTable.h"
#include "TopologyWidget.h"
#include "BandwidthChartWidget.h"
#include "TopTalkersWidget.h"
#include "../core/NetworkManager.h"

namespace gui {

class DeviceMonitorPage : public QWidget {
    Q_OBJECT

public:
    explicit DeviceMonitorPage(core::NetworkManager *networkManager, QWidget *parent = nullptr);
    virtual ~DeviceMonitorPage() = default;

    DeviceTable* getDeviceTable() const { return m_deviceTable; }

public slots:
    void updateDevices(const QList<core::Device> &devices);
    void updateGatewayStatus(bool active);
    void setGatewayModeActive(bool active);
    void onTrafficUpdated(const QMap<QString, core::TrafficStats> &stats);
    void onGlobalTrafficStats(int packetCount, double pps, quint64 totalIn, quint64 totalOut);

private slots:
    void onRefreshRequested();
    void onSelectionChanged(const QString &ip);
    void onSearchChanged(const QString &text);
    void onSearchToggled();
    void applySettings();
    void tickFooter();

private:
    void setupUi();
    void applyTheme();
    QWidget* createStatCard(const QString &label, const QString &color,
                            QLabel **countPtr, const QString &objName = "StatCard");

    core::NetworkManager *m_networkManager;

    // ── Layout containers ──────────────────────────────────────────────────
    QWidget *m_analyticsPanel = nullptr;  // hidden when gateway is off
    QWidget *m_bwUpCard       = nullptr;
    QWidget *m_bwDownCard     = nullptr;
    QWidget *m_blockedCard    = nullptr;

    // ── Widgets ────────────────────────────────────────────────────────────
    DeviceTable          *m_deviceTable      = nullptr;
    TopologyWidget       *m_topologyWidget   = nullptr;
    BandwidthChartWidget *m_bandwidthChart   = nullptr;
    TopTalkersWidget     *m_topTalkersWidget = nullptr;

    // ── Gateway badge ──────────────────────────────────────────────────────
    QLabel *m_gatewayBadge = nullptr;

    // ── KPI labels ─────────────────────────────────────────────────────────
    QLabel *m_onlineCount   = nullptr;
    QLabel *m_uploadTotal   = nullptr;
    QLabel *m_downloadTotal = nullptr;
    QLabel *m_unknownCount  = nullptr;
    QLabel *m_blockedCount  = nullptr;

    // ── Search ─────────────────────────────────────────────────────────────
    QPushButton        *m_searchBtn  = nullptr;
    QPushButton        *m_refreshBtn = nullptr;
    QLineEdit          *m_searchEdit = nullptr;
    QPropertyAnimation *m_searchAnim = nullptr;
    bool                m_searchOpen = false;

    // ── Footer ─────────────────────────────────────────────────────────────
    QLabel *m_footerLiveDot     = nullptr;
    QLabel *m_lastUpdatedLabel  = nullptr;
    QLabel *m_totalHostCountLabel = nullptr;
    QLabel *m_ppsLabel          = nullptr;
    QTimer *m_footerTimer       = nullptr;
    bool    m_liveDotState      = false;
    QDateTime m_lastUpdate;

    // ── State ──────────────────────────────────────────────────────────────
    bool m_gatewayActive = false;

    // ── Helpers ────────────────────────────────────────────────────────────
    static qreal parseBw(const QString &bwStr);
    static QString formatBw(qreal bytesPerSec);
};

} // namespace gui
