#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace gui {

// Shown once at app startup so the user picks their operating mode
// before the main window becomes interactive.
class StartupModePage : public QWidget {
    Q_OBJECT
public:
    enum class Mode { Normal, DHCPServer };

    explicit StartupModePage(QWidget *parent = nullptr);

signals:
    void modeSelected(StartupModePage::Mode mode, bool interceptEnabled);

private slots:
    void onNormalChosen();
    void onDhcpChosen();

private:
    void buildUi();
    void applyTheme();
    void showDhcpOptions();   // expand intercept radio when DHCP is chosen

    Mode m_mode      = Mode::Normal;
    bool m_intercept = false;

    // Widgets
    QPushButton  *m_normalBtn      = nullptr;
    QPushButton  *m_dhcpBtn        = nullptr;

    // DHCP sub-options (initially hidden)
    QWidget      *m_dhcpOptions    = nullptr;
    QPushButton  *m_confirmBtn     = nullptr;
    QLabel       *m_warningLabel   = nullptr;
};

} // namespace gui
