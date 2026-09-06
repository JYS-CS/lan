#pragma once
#include <QWidget>
#include "ToggleSwitch.h"

class QLabel;
class QVBoxLayout;
class QPushButton;

namespace gui {

class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget *parent = nullptr);


private:
    QWidget* makeSection(const QString &title);
    QWidget* makeRow(const QString &label, const QString &desc, ToggleSwitch **sw);
};

} // namespace gui
