#pragma once

#include <QWidget>
#include <QTimer>

namespace gui {

class TopologyWidget : public QWidget {
    Q_OBJECT

public:
    explicit TopologyWidget(QWidget *parent = nullptr);
    
    void setMode(bool isGateway);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QTimer m_animTimer;
    double m_pulsePhase = 0.0;
    double m_travelPhase = 0.0;
    double m_travelDir = 1.0;
    bool m_isGateway = false;
};

} // namespace gui
