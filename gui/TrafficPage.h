#pragma once

#include <QWidget>
#include <QMap>
#include <QList>
#include <QTimer>
#include <QPainter>
#include <QDateTime>
#include <QLabel>
#include <QTableWidget>
#include "../core/NetworkManager.h"
#include "../core/TrafficMonitor.h"

namespace gui {

// Simple real-time chart widget
class TrafficChart : public QWidget {
    Q_OBJECT
public:
    explicit TrafficChart(QWidget *parent = nullptr);
    void addData(double up, double down);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QList<double> m_historyUp;
    QList<double> m_historyDown;
    static const int MAX_POINTS = 60; // 1 minute of data
};

class TrafficPage : public QWidget {
    Q_OBJECT

public:
    explicit TrafficPage(core::NetworkManager *networkManager, QWidget *parent = nullptr);
    virtual ~TrafficPage() = default;

public slots:
    void updateTraffic(const QMap<QString, core::TrafficStats> &stats);
    void updateGlobalStats(quint64 totalIn, quint64 totalOut, double pps);

private:
    void setupUi();
    void applyTheme();
    QWidget* createMetricCard(const QString &label, const QString &val, const QString &color, QLabel **valPtr);
    QString formatBandwidth(quint64 bytes);

    core::NetworkManager *m_networkManager;
    
    // UI Elements
    TrafficChart *m_mainChart;
    QLabel *m_totalUpLabel;
    QLabel *m_totalDownLabel;
    QLabel *m_currentRateLabel;
    
    QTableWidget *m_topTenTable;
    
    // Last stats for calculations (using Outbound/Inbound logic)
    quint64 m_lastTotalUp = 0;
    quint64 m_lastTotalDown = 0;
};

} // namespace gui
