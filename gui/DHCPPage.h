#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QTableWidget>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QStackedWidget>
#include <QMenu>
#include <QTimer>
#include "../core/NetworkManager.h"

namespace gui {

struct DhcpWizardSettings;

class DHCPPage : public QWidget {
    Q_OBJECT

public:
    explicit DHCPPage(core::NetworkManager *networkManager, QWidget *parent = nullptr);
    virtual ~DHCPPage() = default;

    // Called by MainWindow after the startup mode dialog completes.
    // Pre-selects the requested intercept setting.
    void setStartupMode(bool intercept);

    // Called after the DHCP setup wizard finishes. Fills every field with
    // the collected settings and starts the server immediately.
    void applyWizardSettingsAndStart(const gui::DhcpWizardSettings &settings);

private slots:
    void onStartStopClicked();
    void onAddStaticLeaseClicked();
    void onRemoveStaticLeaseClicked();
    void onRefreshLeases();
    void onStatusUpdate(const QString &msg);
    void updateActiveLeases(const QList<core::DHCPLease> &leases);
    void dhcpStatusChanged(bool active);
    void onHealthCheckRequested();
    void onDHCPLogEvent(const QString &message);
    void onDHCPError(const QString &message);
    void onDHCPSuccess(const QString &message);
    void autoFillNetworkInfo();
    void onActiveLeasesContextMenu(const QPoint &pos);
    void onActiveLeaseSelectionChanged();

private:
    void setupUi();
    void applyTheme();
    void startDhcpWithCurrentConfig();
    void stopDhcpAndCleanup();

    core::NetworkManager *m_networkManager;
    core::DHCPManager    *m_dhcpManager;

    // ── Panel 1: DHCP Config ──────────────────────────────────────────────────
    QWidget     *m_configWidget   = nullptr;

    // Network info fields
    QComboBox *m_ifaceCombo       = nullptr;
    QLineEdit *m_myIpEdit        = nullptr;   // host laptop's IP (read-only info)
    QLineEdit *m_gatewayEdit     = nullptr;   // real router IP
    QLineEdit *m_rangeStartEdit  = nullptr;
    QLineEdit *m_rangeEndEdit    = nullptr;
    QLineEdit *m_subnetEdit      = nullptr;
    QLineEdit *m_dnsEdit         = nullptr;
    QLineEdit *m_leaseEdit       = nullptr;
    // Checkboxes
    QCheckBox *m_authCheck       = nullptr;
    QCheckBox *m_interceptCheck  = nullptr;

    // Header bar
    QPushButton *m_startStopBtn  = nullptr;
    QPushButton *m_healthCheckBtn= nullptr;
    QPushButton *m_detectBtn     = nullptr;
    QLabel      *m_statusLabel   = nullptr;

    // Leases tables
    QTableWidget *m_activeLeasesTable  = nullptr;
    QTableWidget *m_staticLeasesTable  = nullptr;
    QPushButton  *m_addStaticBtn       = nullptr;
    QPushButton  *m_removeStaticBtn    = nullptr;
    QLabel       *m_feedbackLabel      = nullptr;

    QTimer *m_leaseTimer  = nullptr;
    bool    m_serverActive  = false;
    bool    m_interceptMode = false;   // true = clients route through laptop
    int     m_prevIpForward = -1;      // kernel ip_forward value before we changed it (-1 = not set by us)
};

} // namespace gui
