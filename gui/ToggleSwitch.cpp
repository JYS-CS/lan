#include "ToggleSwitch.h"
#include <QPainterPath>

namespace gui {

ToggleSwitch::ToggleSwitch(bool checked, QWidget *parent)
    : QWidget(parent), m_checked(checked)
{
    m_thumbPos = checked ? 1.0 : 0.0;
    setCursor(Qt::PointingHandCursor);
    setFixedSize(sizeHint());

    m_anim = new QPropertyAnimation(this, "thumbPos", this);
    m_anim->setDuration(180);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
}

void ToggleSwitch::setChecked(bool v) {
    if (m_checked == v) return;
    m_checked = v;
    m_anim->stop();
    m_anim->setStartValue(m_thumbPos);
    m_anim->setEndValue(v ? 1.0 : 0.0);
    m_anim->start();
    emit toggled(v);
}

void ToggleSwitch::mousePressEvent(QMouseEvent *) {
    setChecked(!m_checked);
}

void ToggleSwitch::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    int w = width(), h = height();
    int r = h / 2;

    // Track
    QColor trackOn(52, 228, 160);
    QColor trackOff(28, 35, 44);
    QColor track;
    track.setRed  (int(trackOff.red()   + (trackOn.red()   - trackOff.red())   * m_thumbPos));
    track.setGreen(int(trackOff.green() + (trackOn.green() - trackOff.green()) * m_thumbPos));
    track.setBlue (int(trackOff.blue()  + (trackOn.blue()  - trackOff.blue())  * m_thumbPos));

    QPainterPath trackPath;
    trackPath.addRoundedRect(QRectF(0, 0, w, h), r, r);
    p.fillPath(trackPath, track);

    // Border
    p.setPen(QPen(QColor(255,255,255,20), 1));
    p.drawPath(trackPath);

    // Thumb
    int thumbDia = h - 6;
    int margin   = 3;
    int travelX  = w - thumbDia - margin * 2;
    int tx       = margin + int(travelX * m_thumbPos);
    int ty       = margin;

    QRadialGradient thumbGrad(tx + thumbDia/2, ty + thumbDia/2, thumbDia/2);
    thumbGrad.setColorAt(0, QColor(255,255,255,240));
    thumbGrad.setColorAt(1, QColor(200,210,220,200));
    p.setPen(Qt::NoPen);
    p.setBrush(thumbGrad);
    p.drawEllipse(tx, ty, thumbDia, thumbDia);
}

} // namespace gui
