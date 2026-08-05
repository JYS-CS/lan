#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QProgressBar>
#include <QString>

namespace core { class NetworkManager; }

namespace gui {

// Collected at the end of the DHCP setup wizard and handed to DHCPPage,
// which fills its fields with these values and starts the server.
struct DhcpWizardSettings {
    QString interface;
    QString hostIp;
    QString gatewayIp;          // real router IP, always detected
    QString subnetMask;
    QString rangeStart;
    QString rangeEnd;
    QString dns1;
    QString dns2;
    int     leaseTimeSeconds = 3600;
    bool    authoritative    = true;
    bool    intercept        = false;
};

// Shown once at app startup so the user picks their operating mode
// before the main window becomes interactive. Presented as a short,
// linear wizard. Choosing Normal Mode exits immediately; choosing
// DHCP Server Mode walks through a fixed sequence of steps — router
// warning, network detection, authoritative mode, gateway mode, and
// final review — each one gating the next. There is no way to jump
// ahead; only the Back/Continue buttons move between steps.
class StartupModePage : public QWidget {
    Q_OBJECT
public:
    enum class Mode { Normal, DHCPServer, CableMode };

    explicit StartupModePage(core::NetworkManager *networkManager, QWidget *parent = nullptr);

signals:
    void modeSelected(StartupModePage::Mode mode, bool interceptEnabled);
    void dhcpWizardCompleted(const gui::DhcpWizardSettings &settings);

private slots:
    void onNormalChosen();
    void onDhcpChosen();
    void onCableChosen();

private:
    void buildUi();
    void applyTheme();

    QWidget *buildStepMode();
    QWidget *buildStepRouterWarning();
    QWidget *buildStepDetectNetwork();
    QWidget *buildStepAuthoritative();
    QWidget *buildStepGateway();
    QWidget *buildStepReview();

    void goToStep(int index);
    void runNetworkDetection();
    void refreshReviewSummary();

    Mode m_selectedMode = Mode::Normal;

    core::NetworkManager *m_networkManager = nullptr;
    DhcpWizardSettings     m_settings;

    // Wizard chrome
    QLabel         *m_stepLabel = nullptr;
    QStackedWidget *m_steps     = nullptr;

    // Step 0 UI
    QPushButton *m_normalBtn = nullptr;
    QPushButton *m_dhcpBtn   = nullptr;
    QPushButton *m_cableBtn  = nullptr;

    // Step: router warning / cable warning
    QLabel      *m_warnTitle       = nullptr;
    QLabel      *m_warnSubtitle    = nullptr;
    QLabel      *m_warnLabel       = nullptr;
    QPushButton *m_warnBackBtn     = nullptr;
    QPushButton *m_warnContinueBtn = nullptr;

    // Step: network detection
    QLabel      *m_detIfaceValue   = nullptr;
    QLabel      *m_detIpValue      = nullptr;
    QLabel      *m_detMaskValue    = nullptr;
    QLabel      *m_detGatewayValue = nullptr;
    QLabel      *m_detStatusLabel  = nullptr;
    QProgressBar *m_detProgress    = nullptr;
    QPushButton *m_detRetryBtn     = nullptr;
    QPushButton *m_detBackBtn      = nullptr;
    QPushButton *m_detContinueBtn  = nullptr;

    // Step: authoritative mode
    QPushButton *m_authYesBtn  = nullptr;
    QPushButton *m_authNoBtn   = nullptr;
    QPushButton *m_authBackBtn = nullptr;
    QPushButton *m_authNextBtn = nullptr;

    // Step: gateway mode
    QPushButton *m_gwTransparentBtn = nullptr;
    QPushButton *m_gwInterceptBtn   = nullptr;
    QPushButton *m_gwBackBtn        = nullptr;
    QPushButton *m_gwNextBtn        = nullptr;

    // Step: review / final settings
    QLabel    *m_reviewSummary   = nullptr;
    QLineEdit *m_rangeStartEdit  = nullptr;
    QLineEdit *m_rangeEndEdit    = nullptr;
    QLineEdit *m_dns1Edit        = nullptr;
    QLineEdit *m_dns2Edit        = nullptr;
    QLineEdit *m_leaseEdit       = nullptr;
    QPushButton *m_reviewBackBtn = nullptr;
    QPushButton *m_finishBtn     = nullptr;
};

} // namespace gui
