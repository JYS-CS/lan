#include "BandwidthChartWidget.h"
#include "Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QFontDatabase>
#include <algorithm>
#include <cmath>

namespace gui {

// ─── Formatting helpers ──────────────────────────────────────────────────────

QString BandwidthChartWidget::formatBytes(quint64 bps) {
    if (bps == 0)               return "0 B/s";
    if (bps < 1024ULL)          return QString::number(bps)                         + " B/s";
    if (bps < 1024ULL*1024)     return QString::number(bps / 1024.0,       'f', 1) + " KB/s";
    if (bps < 1024ULL*1024*1024)return QString::number(bps / (1024.0*1024),'f', 1) + " MB/s";
    return                             QString::number(bps / (1024.0*1024*1024),'f', 2) + " GB/s";
}

// ─── Constructor ─────────────────────────────────────────────────────────────

BandwidthChartWidget::BandwidthChartWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(140);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Pre-fill with zeroes so the chart isn't blank on first show
    for (int i = 0; i < kMaxSamples; ++i)
        m_samples.push_back({0, 0});
}

// ─── Data ingestion ──────────────────────────────────────────────────────────

void BandwidthChartWidget::addSample(quint64 upBps, quint64 downBps) {
    m_samples.push_back({upBps, downBps});
    while ((int)m_samples.size() > kMaxSamples)
        m_samples.pop_front();
    update();
}

// ─── Paint ──────────────────────────────────────────────────────────────────

void BandwidthChartWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRect r = rect();

    // ── Background ──────────────────────────────────────────────────────────
    QPainterPath bg;
    bg.addRoundedRect(r, 8, 8);
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::OpsPanel);
    p.drawPath(bg);

    // Inner border
    p.setPen(QPen(Theme::OpsBorder, 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(bg);

    // Chart margins
    const int ml = 58, mr = 14, mt = 18, mb = 30;
    QRect area(r.left() + ml, r.top() + mt, r.width() - ml - mr, r.height() - mt - mb);
    if (area.width() < 20 || area.height() < 20) return;

    // ── Peak for auto-scale ─────────────────────────────────────────────────
    quint64 peak = 1024; // minimum 1 KB/s so the scale is never 0
    for (const auto &s : m_samples) {
        peak = std::max(peak, std::max(s.up, s.down));
    }
    // Round up to a nice power-of-two multiple for cleaner grid labels
    quint64 scale = 1;
    while (scale < peak) scale <<= 1;
    // Use 75% of the next power-of-two as ceiling so the line never hugs the top
    quint64 ceiling = scale;

    // ── Grid ────────────────────────────────────────────────────────────────
    drawGrid(p, area, ceiling);

    // ── Series (down first so upload draws on top) ───────────────────────────
    drawSeries(p, area, ceiling, false); // download — green
    drawSeries(p, area, ceiling, true);  // upload   — blue

    // ── Axis labels ─────────────────────────────────────────────────────────
    drawLabels(p, area, ceiling);

    // ── Header label ────────────────────────────────────────────────────────
    QFont hdrFont("JetBrains Mono", 8);
    hdrFont.setBold(true);
    p.setFont(hdrFont);
    p.setPen(Theme::OpsTextDim);
    p.drawText(QRect(area.left(), r.top() + 4, area.width(), 14),
               Qt::AlignLeft | Qt::AlignVCenter, "LIVE BANDWIDTH — 60s WINDOW");

    // ── Legend ──────────────────────────────────────────────────────────────
    auto drawLegendDot = [&](const QColor &c, const QString &label, int x) {
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(x, r.top() + 6, 7, 7);
        QFont lf("JetBrains Mono", 8);
        p.setFont(lf);
        p.setPen(Theme::OpsTextDim);
        p.drawText(x + 10, r.top() + 14, label);
    };
    int legendX = area.right() - 150;
    drawLegendDot(Theme::AccentBlue,       "UPLOAD ↑",   legendX);
    drawLegendDot(Theme::OpsAccentGreen,   "DOWNLOAD ↓", legendX + 75);

    // ── Live value readout (last sample) ────────────────────────────────────
    if (!m_samples.empty()) {
        const Sample &last = m_samples.back();
        QFont valFont("JetBrains Mono", 9);
        valFont.setBold(true);

        // Upload
        p.setFont(valFont);
        p.setPen(Theme::AccentBlue);
        QString upStr = formatBytes(last.up);
        p.drawText(QRect(r.left() + 4, area.top(), ml - 6, 20),
                   Qt::AlignRight | Qt::AlignVCenter, upStr);

        // Download
        p.setPen(Theme::OpsAccentGreen);
        QString dnStr = formatBytes(last.down);
        p.drawText(QRect(r.left() + 4, area.top() + 22, ml - 6, 20),
                   Qt::AlignRight | Qt::AlignVCenter, dnStr);
    }
}

// ─── Grid ────────────────────────────────────────────────────────────────────

void BandwidthChartWidget::drawGrid(QPainter &p, const QRect &area, quint64 ceiling) const {
    const int kLines = 4;
    QPen gridPen(Theme::OpsBorder, 1, Qt::DotLine);
    p.setPen(gridPen);

    for (int i = 0; i <= kLines; ++i) {
        int y = area.bottom() - (int)((double)i / kLines * area.height());
        p.drawLine(area.left(), y, area.right(), y);
    }

    // Vertical time ticks every 15 seconds
    for (int s = 0; s <= 60; s += 15) {
        int x = area.left() + (int)((double)s / 60.0 * area.width());
        p.drawLine(x, area.top(), x, area.bottom());
    }
}

// ─── Series polyline with gradient fill ──────────────────────────────────────

void BandwidthChartWidget::drawSeries(QPainter &p, const QRect &area, quint64 ceiling, bool isUp) const {
    if (m_samples.empty()) return;

    const QColor lineColor = isUp ? Theme::AccentBlue : Theme::OpsAccentGreen;

    int n = (int)m_samples.size();
    double xStep = (double)area.width() / (kMaxSamples - 1);

    // Build the polyline from oldest → newest
    QVector<QPointF> pts;
    pts.reserve(n);
    for (int i = 0; i < n; ++i) {
        double val = isUp ? (double)m_samples[i].up : (double)m_samples[i].down;
        double ratio = ceiling > 0 ? val / (double)ceiling : 0.0;
        ratio = std::min(ratio, 1.0);
        // Map to screen — offset by (kMaxSamples - n) so data always right-aligns
        int xi = area.left() + (int)(((kMaxSamples - n + i)) * xStep);
        int yi = area.bottom() - (int)(ratio * area.height());
        pts.append(QPointF(xi, yi));
    }

    // Gradient fill under the line
    QPainterPath fillPath;
    fillPath.moveTo(pts.first().x(), area.bottom());
    for (const auto &pt : pts) fillPath.lineTo(pt);
    fillPath.lineTo(pts.last().x(), area.bottom());
    fillPath.closeSubpath();

    QLinearGradient grad(area.topLeft(), area.bottomLeft());
    QColor fillTop = lineColor; fillTop.setAlpha(55);
    QColor fillBot = lineColor; fillBot.setAlpha(0);
    grad.setColorAt(0.0, fillTop);
    grad.setColorAt(1.0, fillBot);

    p.setPen(Qt::NoPen);
    p.setBrush(grad);
    p.drawPath(fillPath);

    // Line stroke
    QPainterPath linePath;
    linePath.moveTo(pts.first());
    for (int i = 1; i < pts.size(); ++i) linePath.lineTo(pts[i]);

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(lineColor, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(linePath);

    // Draw dot at the head (latest value)
    if (!pts.isEmpty()) {
        p.setBrush(lineColor);
        p.setPen(Qt::NoPen);
        p.drawEllipse(pts.last(), 3.5, 3.5);
    }
}

// ─── Axis labels ─────────────────────────────────────────────────────────────

void BandwidthChartWidget::drawLabels(QPainter &p, const QRect &area, quint64 ceiling) const {
    QFont axFont("JetBrains Mono", 7);
    p.setFont(axFont);
    p.setPen(Theme::OpsTextFaint);

    const int kLines = 4;
    for (int i = 0; i <= kLines; ++i) {
        int y = area.bottom() - (int)((double)i / kLines * area.height());
        quint64 val = (quint64)((double)i / kLines * ceiling);
        QString lbl = formatBytes(val);
        p.drawText(QRect(area.left() - 56, y - 8, 52, 16),
                   Qt::AlignRight | Qt::AlignVCenter, lbl);
    }

    // Time axis labels
    const QStringList timeLbls = {"60s", "45s", "30s", "15s", "now"};
    for (int i = 0; i < timeLbls.size(); ++i) {
        int x = area.left() + (int)((double)(i * 15) / 60.0 * area.width());
        p.drawText(QRect(x - 16, area.bottom() + 4, 32, 14),
                   Qt::AlignCenter, timeLbls[i]);
    }
}

} // namespace gui
