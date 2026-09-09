#pragma once
#include <QWidget>
#include "ToggleSwitch.h"
#include "../core/NetworkManager.h"

class QLabel;
class QVBoxLayout;
class QPushButton;

namespace gui {

class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(core::NetworkManager *nm = nullptr, QWidget *parent = nullptr);

private slots:
    void refreshThreatStatus();
    void refreshDnsStatus();

private:
    QWidget* makeSection(const QString &title);
    QWidget* makeRow(const QString &label, const QString &desc, ToggleSwitch **sw);

    core::NetworkManager *m_nm = nullptr;
    QLabel *m_threatStatusLabel = nullptr;
    QLabel *m_dnsStatusLabel = nullptr;
};

} // namespace gui
