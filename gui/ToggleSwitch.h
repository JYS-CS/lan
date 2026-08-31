#pragma once
#include <QWidget>
#include <QPainter>
#include <QPropertyAnimation>
#include <QMouseEvent>

namespace gui {

// ── Custom pill-style toggle switch ─────────────────────────────────────────
class ToggleSwitch : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal thumbPos READ thumbPos WRITE setThumbPos)
public:
    explicit ToggleSwitch(bool checked = false, QWidget *parent = nullptr);
    bool isChecked() const { return m_checked; }
    void setChecked(bool v);
    QSize sizeHint() const override { return {52, 28}; }

signals:
    void toggled(bool checked);

protected:
    void mousePressEvent(QMouseEvent *) override;
    void paintEvent(QPaintEvent *) override;

private:
    qreal thumbPos() const { return m_thumbPos; }
    void  setThumbPos(qreal v) { m_thumbPos = v; update(); }

    bool   m_checked  = false;
    qreal  m_thumbPos = 0.0;   // 0.0 = left, 1.0 = right
    QPropertyAnimation *m_anim;
};

} // namespace gui
