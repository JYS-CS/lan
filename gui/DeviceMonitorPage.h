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
#include <QVariantAnimation>
#include <QPainter>
#include "DeviceTable.h"
#include "TopologyWidget.h"
#include "TopologyWidget.h"
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
    QWidget* createHeaderStat(const QString &iconPath, const QString &color,
                              QLabel **countPtr, const QString &tooltip);

    core::NetworkManager *m_networkManager;

    // ── Layout containers ──────────────────────────────────────────────────
    // ── Layout containers ──────────────────────────────────────────────────
    QWidget *m_blockedCard    = nullptr;
    QWidget *m_headerStatsContainer = nullptr;

    // ── Widgets ────────────────────────────────────────────────────────────
    DeviceTable          *m_deviceTable      = nullptr;
    TopologyWidget       *m_topologyWidget   = nullptr;

    // ── Gateway ────────────────────────────────────────────────────────────

    // ── KPI labels ─────────────────────────────────────────────────────────
    QLabel *m_onlineCount   = nullptr;
    QLabel *m_offlineCount  = nullptr;
    QLabel *m_unknownCount  = nullptr;
    QLabel *m_blockedCount  = nullptr;

    // ── Search ─────────────────────────────────────────────────────────────
    QPushButton        *m_searchBtn  = nullptr;
    QPushButton        *m_refreshBtn = nullptr;
    QLineEdit          *m_searchEdit = nullptr;
    QPropertyAnimation *m_searchAnim = nullptr;
    QVariantAnimation  *m_spinAnim   = nullptr;
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

};

} // namespace gui
