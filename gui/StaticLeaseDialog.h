#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QShowEvent>

namespace gui {

class StaticLeaseDialog : public QDialog {
    Q_OBJECT

public:
    explicit StaticLeaseDialog(const QString &mac = "", const QString &ip = "", const QString &hostname = "", QWidget *parent = nullptr);

    QString mac() const { return m_macEdit->text(); }
    QString ip() const { return m_ipEdit->text(); }
    QString hostname() const { return m_hostEdit->text(); }

    void setMac(const QString &mac) { m_macEdit->setText(mac); }
    void setIp(const QString &ip) { m_ipEdit->setText(ip); }

protected:
    void showEvent(QShowEvent *event) override;

private:
    void applyTheme();
    
    QLineEdit *m_macEdit;
    QLineEdit *m_ipEdit;
    QLineEdit *m_hostEdit;
    QLabel    *m_errorLabel;
};

} // namespace gui
