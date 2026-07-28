#include "TrafficPage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDateTime>
#include <QLabel>
#include <QTableWidget>
#include "../core/NetworkManager.h"
#include "../core/TrafficMonitor.h"
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QFont>
#include <algorithm>

namespace gui {

TrafficChart::TrafficChart(QWidget *parent) : QWidget(parent) {
    setFixedHeight(200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void TrafficChart::addData(double up, double down) {
    m_historyUp.append(up);
    m_historyDown.append(down);
    if (m_historyUp.size() > MAX_POINTS) {
        m_historyUp.removeFirst();
        m_historyDown.removeFirst();
    }
    update();
}

void TrafficChart::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor("#181b22"));

    if (m_historyUp.isEmpty()) return;

    double maxVal = 1024 * 10;
    for (double v : m_historyUp) if (v > maxVal) maxVal = v;
    for (double v : m_historyDown) if (v > maxVal) maxVal = v;

    auto drawLine = [&](const QList<double> &data, QColor color, QColor areaColor) {
        if (data.size() < 2) return;
        QPainterPath path;
        double xStep = (double)width() / (MAX_POINTS - 1);
        double yFactor = (double)(height() - 40) / maxVal;
        path.moveTo(0, height());
        for (int i = 0; i < data.size(); ++i) {
            double x = i * xStep;
            double y = height() - 20 - (data[i] * yFactor);
            if (i == 0) path.lineTo(x, y);
            else path.lineTo(x, y);
        }
        QPainterPath fill = path;
        fill.lineTo((data.size()-1) * xStep, height());
        fill.closeSubpath();
        p.fillPath(fill, areaColor);
        p.setPen(QPen(color, 2));
        p.drawPath(path);
    };

    drawLine(m_historyDown, QColor("#2dd98f"), QColor(45, 217, 143, 30));
    drawLine(m_historyUp, QColor("#4f7fff"), QColor(79, 127, 255, 30));

    p.setPen(QColor("#7c8299"));
    p.setFont(QFont("Inter", 9));
    p.drawText(10, 20, "UPLOAD (BLUE)");
    p.drawText(width() - 120, 20, "DOWNLOAD (GREEN)");
}

TrafficPage::TrafficPage(core::NetworkManager *networkManager, QWidget *parent)
    : QWidget(parent), m_networkManager(networkManager) {
    setupUi();
    applyTheme();
}

void TrafficPage::setupUi() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(30, 30, 30, 30);
    mainLayout->setSpacing(25);

    QHBoxLayout *headerLayout = new QHBoxLayout();
    headerLayout->addWidget(createMetricCard("Total Inbound", "0 B", "#2dd98f", &m_totalDownLabel));
    headerLayout->addWidget(createMetricCard("Total Outbound", "0 B", "#4f7fff", &m_totalUpLabel));
    headerLayout->addWidget(createMetricCard("Current Throughput", "0 B/s", "#e8eaf0", &m_currentRateLabel));
    mainLayout->addLayout(headerLayout);

    QLabel *chartTitle = new QLabel("NETWORK THROUGHPUT (60S WINDOW)", this);
    chartTitle->setStyleSheet("color: #4a5068; font-size: 10px; font-weight: bold; letter-spacing: 1px;");
    mainLayout->addWidget(chartTitle);

    m_mainChart = new TrafficChart(this);
    mainLayout->addWidget(m_mainChart);

    QLabel *tableTitle = new QLabel("TOP BANDWIDTH CONSUMERS (LIVE RANKING)", this);
    tableTitle->setStyleSheet("color: #4a5068; font-size: 10px; font-weight: bold; letter-spacing: 1px; margin-top: 15px;");
    mainLayout->addWidget(tableTitle);

    m_topTenTable = new QTableWidget(0, 4, this);
    m_topTenTable->setHorizontalHeaderLabels({"IP ADDRESS", "UP RATE", "DOWN RATE", "TOTAL DATA"});
    m_topTenTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_topTenTable->verticalHeader()->setVisible(false);
    m_topTenTable->setShowGrid(false);
    m_topTenTable->setAlternatingRowColors(true);
    m_topTenTable->setFixedHeight(250);
    mainLayout->addWidget(m_topTenTable);
}

QWidget* TrafficPage::createMetricCard(const QString &label, const QString &val, const QString &color, QLabel **valPtr) {
    QWidget *card = new QWidget(this);
    card->setFixedHeight(80);
    card->setStyleSheet("background: #181b22; border-radius: 12px; border: 0.5px solid rgba(255,255,255,0.05);");
    QVBoxLayout *l = new QVBoxLayout(card);
    l->setContentsMargins(20, 15, 20, 15);
    QLabel *title = new QLabel(label.toUpper(), card);
    title->setStyleSheet("color: #4a5068; font-size: 10px; font-weight: bold; letter-spacing: 0.5px; border: none;");
    *valPtr = new QLabel(val, card);
    (*valPtr)->setStyleSheet(QString("color: %1; font-size: 20px; font-weight: 500; border: none;").arg(color));
    l->addWidget(title);
    l->addWidget(*valPtr);
    return card;
}

void TrafficPage::applyTheme() {
    setStyleSheet("gui--TrafficPage { background: #111318; }"
                  "QTableWidget { background: transparent; border: none; color: #e8eaf0; gridline-color: transparent; }"
                  "QHeaderView::section { background: transparent; color: #4a5068; font-size: 10px; font-weight: bold; border: none; }");
}

void TrafficPage::updateTraffic(const QMap<QString, core::TrafficStats> &stats) {
    QList<std::pair<QString, core::TrafficStats>> sorted;
    for (auto it = stats.begin(); it != stats.end(); ++it) {
        sorted.append({it.key(), it.value()});
    }

    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
        return (a.second.currentRateUp + a.second.currentRateDown) > (b.second.currentRateUp + b.second.currentRateDown);
    });

    m_topTenTable->setRowCount(std::min((int)sorted.size(), 10));
    for (int i = 0; i < m_topTenTable->rowCount(); ++i) {
        const auto &item = sorted[i];
        m_topTenTable->setItem(i, 0, new QTableWidgetItem(item.first));
        m_topTenTable->setItem(i, 1, new QTableWidgetItem(formatBandwidth(item.second.currentRateUp) + "/s"));
        m_topTenTable->setItem(i, 2, new QTableWidgetItem(formatBandwidth(item.second.currentRateDown) + "/s"));
        m_topTenTable->setItem(i, 3, new QTableWidgetItem(formatBandwidth(item.second.totalBytesUp + item.second.totalBytesDown)));
    }
}

void TrafficPage::updateGlobalStats(quint64 totalIn, quint64 totalOut, double pps) {
    Q_UNUSED(pps)
    m_totalDownLabel->setText(formatBandwidth(totalIn));
    m_totalUpLabel->setText(formatBandwidth(totalOut));
    
    double currentUpRate = (totalOut > m_lastTotalUp) ? (totalOut - m_lastTotalUp) : 0;
    double currentDownRate = (totalIn > m_lastTotalDown) ? (totalIn - m_lastTotalDown) : 0;
    
    m_lastTotalUp = totalOut;
    m_lastTotalDown = totalIn;

    m_currentRateLabel->setText(formatBandwidth(currentUpRate + currentDownRate) + "/s");
    m_mainChart->addData(currentUpRate, currentDownRate);
}

QString TrafficPage::formatBandwidth(quint64 bytes) {
    if (bytes < 1024) return QString::number(bytes) + " B";
    double kb = bytes / 1024.0;
    if (kb < 1024) return QString::number(kb, 'f', 1) + " KB";
    double mb = kb / 1024.0;
    if (mb < 1024) return QString::number(mb, 'f', 1) + " MB";
    return QString::number(mb / 1024.0, 'f', 1) + " GB";
}

} // namespace gui
