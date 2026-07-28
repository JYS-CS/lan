#pragma once

#include <QMainWindow>
#include <QTableWidget>
#include <QToolBar>
#include <QDockWidget>
#include <QLabel>
#include <QPushButton>
#include <QHeaderView>
#include <QThread>
#include <QTimer>
#include "networkscanner.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onRefreshDevices();
    void onToggleDHCP();
    void onScanNetwork();
    void onClearLogs();
    void onKickDevice();
    void onBlockDevice();
    void onAddStaticLease();

    void updateDeviceTable(const QList<Device> &devices);
    void onSelectionChanged();
    void handleScanError(const QString &message);
    void updateStatus(const QString &message);

private:
    void setupToolBar();
    void setupCentralTable();
    void setupDockWidget();
    void setupStatusBar();
    void applyStyleSheet();

    // UI Elements
    QTableWidget  *m_deviceTable  = nullptr;
    QLabel *m_statusLabel    = nullptr;
    QLabel *m_interfaceLabel = nullptr;
    
    // Controls
    QPushButton *m_kickBtn = nullptr;
    QPushButton *m_blockBtn = nullptr;
    QPushButton *m_staticBtn = nullptr;

    // Background Scanning
    QThread m_scannerThread;
    NetworkScanner *m_scanner = nullptr;
    QTimer *m_refreshTimer = nullptr;
};
