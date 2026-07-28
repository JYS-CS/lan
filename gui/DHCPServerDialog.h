#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include "../core/DHCPManager.h"

namespace gui {

class DHCPServerDialog : public QDialog {
    Q_OBJECT

public:
    explicit DHCPServerDialog(QWidget *parent = nullptr);
    explicit DHCPServerDialog(const QString &detectedGateway,
                            const QString &detectedMyIp,
                            const QString &detectedInterface,
                            QWidget *parent = nullptr);

    core::DHCPServerConfig getConfig() const;
    bool serverWasStarted() const { return m_serverStarted; }

signals:
    void startServer(const core::DHCPServerConfig &config);
    void stopServer();

public slots:
    void onStartClicked();
    void onStatusUpdate(const QString &msg);
    void onError(const QString &msg);

private:
    void applyTheme();
    void setStatus(const QString &msg, bool isError = false);

    QLineEdit *m_ifaceEdit;
    QLineEdit *m_rangeStartEdit;
    QLineEdit *m_rangeEndEdit;
    QLineEdit *m_subnetEdit;
    QLineEdit *m_gatewayEdit;
    QLineEdit *m_dnsEdit;
    QLineEdit *m_leaseEdit;
    QLabel    *m_statusLabel;
    QPushButton *m_startBtn;
    QPushButton *m_stopBtn;

    bool m_serverStarted = false;
};

} // namespace gui
