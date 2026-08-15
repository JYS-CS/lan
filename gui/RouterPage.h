#pragma once

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QPropertyAnimation>
#include <QTimer>
#include <QFrame>
#include <QToolButton>
#include "../core/NetworkManager.h"
#include "../core/RouterDetector.h"

namespace gui {

// A single capability badge (icon + label, green/grey based on state)
class CapBadge : public QWidget {
    Q_OBJECT
public:
    explicit CapBadge(const QString &iconPath, const QString &label, QWidget *parent = nullptr);
    void setState(bool active);

private:
    QFrame *m_pill;
    QLabel *m_iconLabel;
    QLabel *m_textLabel;
    QString m_iconPath;
};

// Collapsible raw-probe accordion section
class AccordionSection : public QWidget {
    Q_OBJECT
public:
    AccordionSection(const QString &title, QWidget *parent = nullptr);
    void setContent(const QString &text);
    void toggle();
private:
    QToolButton *m_header;
    QLabel      *m_body;
    bool         m_expanded = false;
};

// ─── Main Router Intelligence Page ────────────────────────────────────────────
class RouterPage : public QWidget {
    Q_OBJECT
public:
    explicit RouterPage(core::NetworkManager *nm, QWidget *parent = nullptr);

public slots:
    void updateInfo(const core::RouterInfo &info);
    void setDetectionStage(const QString &stage);

private slots:
    void onRescanClicked();

private:
    void setupUi();
    void applyTheme();
    void setScanning(bool scanning);
    void populateCapabilities(const core::RouterInfo &info);

    core::NetworkManager *m_nm;

    // Hero
    QLabel      *m_gwIpLabel;
    QLabel      *m_gwMacLabel;
    QLabel      *m_stageLabel;
    QPushButton *m_rescanBtn;
    QLabel      *m_spinnerLabel;
    QTimer      *m_spinnerTimer;
    int          m_spinnerFrame = 0;

    // Identity
    QLabel *m_mfrLabel;
    QLabel *m_modelLabel;
    QLabel *m_firmwareLabel;
    QLabel *m_friendlyLabel;
    QLabel *m_classBadge;
    QLabel *m_snmpLabel;
    QLabel *m_locationLabel;

    // Capabilities
    CapBadge *m_capSSH;
    CapBadge *m_capTelnet;
    CapBadge *m_capWebUI;
    CapBadge *m_capSNMP;
    CapBadge *m_capUPnP;
    CapBadge *m_capIPv6;
    CapBadge *m_capGuest;
    CapBadge *m_capVPN;
    CapBadge *m_capQoS;
    CapBadge *m_capDual;
    CapBadge *m_capTri;
    CapBadge *m_capWPA3;
    CapBadge *m_capEnterprise;

    // Accordion
    AccordionSection *m_secHTTP;
    AccordionSection *m_secSSDPUPnP;
    AccordionSection *m_secSNMP;
    AccordionSection *m_secPorts;

    // Security risk bar
    QFrame  *m_secRiskBar;
    QLabel  *m_credsRiskLabel;
    QLabel  *m_fwRiskLabel;
    QLabel  *m_notesLabel;
};

} // namespace gui
