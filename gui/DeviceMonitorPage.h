#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include "DeviceTable.h"
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

private:
    void setupUi();
    void applyTheme();
    QWidget* createStatPill(const QString &label, const QString &color, QLabel **countPtr);

    core::NetworkManager *m_networkManager;
    DeviceTable *m_deviceTable;
    
    // ActionBar elements
    QLineEdit *m_searchEdit;
    QLabel *m_onlineCount;
    QLabel *m_idleCount;
    QLabel *m_totalCount;
    QLabel *m_gatewayStatus;
    QPushButton *m_refreshBtn;
    QPushButton *m_exportBtn;

    // Status bar
    QLabel *m_hintLabel;
    QLabel *m_selLabel;
};

} // namespace gui
