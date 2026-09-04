#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QTimer>
#include <QDateTime>
#include <QPropertyAnimation>
#include "DeviceTable.h"
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

private slots:
    void onRefreshRequested();
    void onSelectionChanged(const QString &ip);
    void onSearchChanged(const QString &text);
    void onExportRequested();
    void onSearchToggled();
    void applySettings();

private:
    void setupUi();
    void applyTheme();
    QWidget* createStatCard(const QString &label, const QString &color, QLabel **countPtr);

    core::NetworkManager *m_networkManager;
    DeviceTable *m_deviceTable;
    TopologyWidget *m_topologyWidget;

    // Animated search & controls
    QPushButton  *m_searchBtn;
    QPushButton  *m_refreshBtn;
    QLineEdit    *m_searchEdit;
    QPropertyAnimation *m_searchAnim;
    bool m_searchOpen = false;

    QLabel *m_onlineCount;
    QLabel *m_uploadTotal;
    QLabel *m_downloadTotal;
    QLabel *m_unknownCount;

    QWidget *m_uploadCard = nullptr;
    QWidget *m_downloadCard = nullptr;

    QLabel *m_lastUpdatedLabel;
    QLabel *m_totalHostCountLabel;

    QTimer *m_footerTimer;
    bool m_liveDotState = false;
    QLabel *m_footerLiveDot;

    QDateTime m_lastUpdate;

    qreal parseBw(const QString &bwStr);
    QString formatBw(qreal bytesPerSec);
};

} // namespace gui
