#pragma once

#include <QWidget>
#include <QTimer>
#include <QPixmap>
#include <QString>

namespace gui {

class TopologyWidget : public QWidget {
    Q_OBJECT

public:
    explicit TopologyWidget(QWidget *parent = nullptr);
    
    void setMode(bool isGateway);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    // Icons never change while a mode is active, but paintEvent runs every
    // ~30ms via the animation timer — re-parsing and rasterizing the SVGs
    // on every frame would be wasted work. Cache them per mode instead.
    QPixmap iconFor(const QString &path, const QColor &color);
    void rebuildIconCache();

    QTimer m_animTimer;
    double m_pulsePhase = 0.0;
    double m_travelPhase = 0.0;
    double m_travelDir = 1.0;
    bool m_isGateway = false;

    QPixmap m_iconCache[3];
};

} // namespace gui
