#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QProgressBar>
#include <QLabel>
#include "../core/PortScanner.h"

namespace gui {

class PortScanDialog : public QDialog {
    Q_OBJECT

public:
    explicit PortScanDialog(const QString &ip, QWidget *parent = nullptr);
    virtual ~PortScanDialog() = default;

private slots:
    void onResultFound(const core::PortResult &res);
    void onProgress(int current, int total);
    void onFinished();

private:
    void setupUi();
    void applyTheme();

    QString m_ip;
    core::PortScanner *m_scanner;
    QTableWidget *m_table;
    QProgressBar *m_progressBar;
    QLabel *m_statusLabel;
};

} // namespace gui
