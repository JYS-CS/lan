#include "TrafficPage.h"
#include "Theme.h"
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

    m_growAnim = new QVariantAnimation(this);
    m_growAnim->setDuration(260);
    m_growAnim->setStartValue(0.0);
    m_growAnim->setEndValue(1.0);
    m_growAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_growAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_growT = v.toDouble();
        update();
    });
}

void TrafficChart::addData(double up, double down) {
    m_prevLastUp   = m_historyUp.isEmpty()   ? up   : m_historyUp.last();
    m_prevLastDown = m_historyDown.isEmpty() ? down : m_historyDown.last();

    m_historyUp.append(up);
    m_historyDown.append(down);
    if (m_historyUp.size() > MAX_POINTS) {
        m_historyUp.removeFirst();
        m_historyDown.removeFirst();
    }

    m_growAnim->stop();
    m_growAnim->start();
}

// Smooth polyline through data using midpoint control points — cheap,
// dependency-free curve smoothing that avoids sharp zig-zags.
static void addSmoothPath(QPainterPath &path, const QList<QPointF> &pts) {
    if (pts.isEmpty()) return;
    path.moveTo(pts.first());
    if (pts.size() == 1) return;
    for (int i = 0; i < pts.size() - 1; ++i) {
        QPointF mid = (pts[i] + pts[i + 1]) / 2.0;
        path.quadTo(pts[i], mid);
    }
    path.lineTo(pts.last());
}

void TrafficChart::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor("#181b22"));

    if (m_historyUp.isEmpty()) return;

    double maxVal = 1024 * 10;
    for (double v : m_historyUp) if (v > maxVal) maxVal = v;
    for (double v : m_historyDown) if (v > maxVal) maxVal = v;

    double xStep   = (double)width() / (MAX_POINTS - 1);
    double yFactor = (double)(height() - 40) / maxVal;

    auto drawSeries = [&](const QList<double> &data, double prevLast, QColor color, QColor areaTop) {
        if (data.size() < 2) return;

        QList<QPointF> pts;
        pts.reserve(data.size());
        for (int i = 0; i < data.size(); ++i) {
            double value = data[i];
            if (i == data.size() - 1) {
                // Animate the newest point growing from its previous height
                value = prevLast + (data[i] - prevLast) * m_growT;
            }
            double x = i * xStep;
            double y = height() - 20 - (value * yFactor);
            pts.append(QPointF(x, y));
        }

        QPainterPath line;
        addSmoothPath(line, pts);

        QPainterPath fill = line;
        fill.lineTo(pts.last().x(), height());
        fill.lineTo(pts.first().x(), height());
        fill.closeSubpath();

        QLinearGradient grad(0, 0, 0, height());
        grad.setColorAt(0.0, areaTop);
        grad.setColorAt(1.0, QColor(areaTop.red(), areaTop.green(), areaTop.blue(), 0));
        p.fillPath(fill, grad);

        p.setPen(QPen(color, 2));
        p.drawPath(line);

        // Highlight the newest point
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawEllipse(pts.last(), 3, 3);
    };

    drawSeries(m_historyDown, m_prevLastDown, QColor("#ff9142"), QColor(255, 145, 66, 55));
    drawSeries(m_historyUp,   m_prevLastUp,   QColor("#4f7fff"), QColor(79, 127, 255, 55));

    // Legend with colored dots instead of plain text
    auto drawLegend = [&](int x, QColor color, const QString &text) {
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(x, 15), 4, 4);
        p.setPen(QColor("#7c8299"));
        p.setFont(QFont("Inter", 9));
        p.drawText(x + 10, 19, text);
    };
    drawLegend(10, QColor("#4f7fff"), "UPLOAD");
    drawLegend(120, QColor("#ff9142"), "DOWNLOAD");
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
    headerLayout->addWidget(createMetricCard("Total Inbound", "0 B", "#ff9142", &m_totalDownLabel));
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
