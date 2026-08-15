#include "TopologyWidget.h"
#include <QPainter>
#include <QPainterPath>
#include <QDateTime>
#include <cmath>
#include "Theme.h"

namespace gui {

TopologyWidget::TopologyWidget(QWidget *parent) : QWidget(parent) {
    setFixedSize(300, 60);
    
    connect(&m_animTimer, &QTimer::timeout, this, [this]() {
        m_pulsePhase += 0.1;
        m_travelPhase += 0.02 * m_travelDir;
        if (m_travelPhase >= 1.0) {
            m_travelPhase = 1.0;
            m_travelDir = -1.0;
        } else if (m_travelPhase <= 0.0) {
            m_travelPhase = 0.0;
            m_travelDir = 1.0;
        }
        update();
    });
    m_animTimer.start(30); // ~33fps
}

void TopologyWidget::setMode(bool isGateway) {
    m_isGateway = isGateway;
    update();
}

void TopologyWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int nodeRadius = 4;
    int y = (height() / 2) - 8; // Shift up slightly to make room for text below
    
    QPoint pt1(nodeRadius + 25, y);
    QPoint pt2(width() / 2, y);
    QPoint pt3(width() - nodeRadius - 25, y);

    // Draw lines
    painter.setPen(QPen(Theme::OpsBorderSoft, 2));
    painter.drawLine(pt1, pt2);
    painter.drawLine(pt2, pt3);

    // Draw traveling highlight
    int travelX = pt1.x() + (pt3.x() - pt1.x()) * m_travelPhase;
    QLinearGradient grad(travelX - 20, y, travelX + 20, y);
    QColor hlColor = Theme::OpsAccentGreen;
    hlColor.setAlpha(0);
    grad.setColorAt(0, hlColor);
    grad.setColorAt(0.5, Theme::OpsAccentTeal);
    grad.setColorAt(1, hlColor);
    
    painter.setPen(QPen(QBrush(grad), 2));
    painter.drawLine(pt1.x(), y, pt3.x(), y);

    // Draw nodes
    auto drawNode = [&](QPoint pt, QColor color, double pulseOffset, const QString &iconPath, const QString &label) {
        double pulse = (std::sin(m_pulsePhase + pulseOffset) + 1.0) / 2.0;
        
        QRadialGradient glowGrad(pt, 18);
        QColor glowColor = color;
        glowColor.setAlpha(40 * pulse);
        QColor trans = color;
        trans.setAlpha(0);
        glowGrad.setColorAt(0, glowColor);
        glowGrad.setColorAt(1, trans);
        
        painter.setPen(Qt::NoPen);
        painter.setBrush(glowGrad);
        painter.drawEllipse(pt, 18, 18);
        
        // Background circle
        painter.setBrush(Theme::OpsPanel);
        painter.setPen(QPen(Theme::OpsBorder, 1));
        painter.drawEllipse(pt, 12, 12);
        
        // Icon
        QPixmap pm = Theme::tintedSvgPixmap(iconPath, 14, color);
        painter.drawPixmap(pt.x() - 7, pt.y() - 7, pm);
        
        // Label
        painter.setPen(Theme::OpsTextDim);
        QFont f("JetBrains Mono", 8, QFont::Bold);
        painter.setFont(f);
        QFontMetrics fm(f);
        int tw = fm.horizontalAdvance(label);
        painter.drawText(pt.x() - tw/2, pt.y() + 24, label);
    };

    if (m_isGateway) {
        drawNode(pt1, Theme::OpsAccentTeal, 0.0, ":/resources/traffic.svg", "DEVICES");
        drawNode(pt2, Theme::OpsAccentGreen, 2.0, ":/resources/monitor.svg", "THIS HOST");
        drawNode(pt3, Theme::OpsTextDim, 4.0, ":/resources/router.svg", "GATEWAY");
    } else {
        drawNode(pt1, Theme::OpsAccentGreen, 0.0, ":/resources/monitor.svg", "THIS HOST");
        drawNode(pt2, Theme::OpsAccentTeal, 2.0, ":/resources/router.svg", "GATEWAY");
        drawNode(pt3, Theme::OpsTextDim, 4.0, ":/resources/wifi.svg", "INTERNET");
    }
}

} // namespace gui
