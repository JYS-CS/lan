#pragma once

#include <QWidget>
#include <QTimer>
#include <deque>

namespace gui {

// Rolling 60-second live bandwidth chart.
// Draws upload (blue) and download (green) polylines with gradient fills
// over a dark grid. Entirely custom QPainter — no external charting library.
// Only meaningful when DHCP gateway is active; caller hides/shows this widget.
class BandwidthChartWidget : public QWidget {
    Q_OBJECT

public:
    static constexpr int kMaxSamples = 60; // 60 one-second samples = 60s window

    explicit BandwidthChartWidget(QWidget *parent = nullptr);

public slots:
    // Feed one second's worth of aggregate network data.
    // upBytes / downBytes are bytes-per-second totals across all devices.
    void addSample(quint64 upBytesPerSec, quint64 downBytesPerSec);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct Sample {
        quint64 up   = 0;
        quint64 down = 0;
    };

    std::deque<Sample> m_samples;  // oldest → newest (max kMaxSamples entries)

    // Helpers
    static QString formatBytes(quint64 bps);
    void drawGrid(QPainter &p, const QRect &area, quint64 peak) const;
    void drawSeries(QPainter &p, const QRect &area, quint64 peak,
                    bool isUp) const;
    void drawLabels(QPainter &p, const QRect &area, quint64 peak) const;
};

} // namespace gui
