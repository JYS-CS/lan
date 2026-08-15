#pragma once
#include <QString>
#include <QColor>
#include <QWidget>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QFile>
#include <QPixmap>
#include <QIcon>
#include <QPainter>
#include <QSvgRenderer>
#include <QRegularExpression>
#include <QTimer>

namespace gui {

// Shared visual language for the whole app: a dark base with a
// blue/orange duotone accent (inspired by Sniffnet's mark), rounded
// corners, and soft borders instead of hard dividers.
namespace Theme {

inline const QColor BgPrimary   = QColor("#0d1117");
inline const QColor BgCard      = QColor("#161b26");
inline const QColor BgCardAlt   = QColor("#1c2230");
inline const QColor Border      = QColor(255, 255, 255, 20);   // ~8% white
inline const QColor BorderHover = QColor(79, 127, 255, 100);

inline const QColor AccentBlue    = QColor("#4f7fff");
inline const QColor AccentBlueDim = QColor("#3d6ef0");
inline const QColor AccentOrange  = QColor("#ff9142");
inline const QColor AccentOrangeDim = QColor("#f07f2e");

inline const QColor TextPrimary   = QColor("#e8eaf0");
inline const QColor TextSecondary = QColor("#8a93b8");
inline const QColor TextMuted     = QColor("#5a6175");

inline const QColor Success = QColor("#3ddc84");
inline const QColor Warning = QColor("#e8c07a");
inline const QColor Danger  = QColor("#ff5c5c");

// Ops Console Theme Tokens
inline const QColor OpsBg        = QColor("#0a0d12");
inline const QColor OpsPanel     = QColor("#0f141b");
inline const QColor OpsAltPanel  = QColor("#12181f");
inline const QColor OpsBorder    = QColor("#1c232c");
inline const QColor OpsBorderSoft= QColor("#161c24");

inline const QColor OpsTextPrimary = QColor("#dbe4ee");
inline const QColor OpsTextDim     = QColor("#7c8798");
inline const QColor OpsTextFaint   = QColor("#4d5666");

inline const QColor OpsAccentGreen = QColor("#34e4a0"); // online/self
inline const QColor OpsAccentTeal  = QColor("#5eead4"); // gateway
inline const QColor OpsAccentAmber = QColor("#f5a623"); // warning/unknown vendor

// Applied once, app-wide, in main.cpp. Individual pages can still
// layer more specific rules on top via their own setStyleSheet() —
// Qt's cascade lets widget-level styling override this base.
inline QString globalStyleSheet() {
    return
        "QWidget { selection-background-color: rgba(79,127,255,0.35); }"

        "QToolTip { background-color: #1c2230; color: #e8eaf0; border: 1px solid rgba(79,127,255,0.35); "
        "   border-radius: 6px; padding: 6px 10px; font-size: 12px; }"

        "QPushButton { border-radius: 8px; padding: 6px 14px; font-size: 12px; }"
        "QPushButton:disabled { color: #4a5068; }"

        "QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox { "
        "   background-color: #161b26; border: 1px solid rgba(255,255,255,0.10); "
        "   border-radius: 8px; padding: 6px 10px; color: #e8eaf0; }"
        "QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border: 1px solid #4f7fff; }"
        "QComboBox::drop-down { border: none; width: 22px; }"

        "QRadioButton { color: #c7cbe0; spacing: 8px; }"
        "QRadioButton::indicator { width: 15px; height: 15px; "
        "   border-radius: 7px; border: 1px solid rgba(255,255,255,0.18); background: #161b26; }"
        "QRadioButton::indicator:checked { background: #4f7fff; border: 1px solid #4f7fff; }"

        "QCheckBox { color: #c7cbe0; spacing: 8px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px; "
        "   border: 1px solid rgba(255,92,92,0.6); background: rgba(255,92,92,0.15); "
        "   image: url(:/resources/cross.svg); }"
        "QCheckBox::indicator:checked { background: #2dd98f; border: 1px solid #2dd98f; "
        "   image: url(:/resources/check.svg); }"

        "QGroupBox { border: 1px solid rgba(255,255,255,0.08); border-radius: 10px; "
        "   margin-top: 14px; padding-top: 10px; color: #e8eaf0; font-weight: 600; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; color: #8a93b8; }"

        "QTableWidget, QTableView { background-color: #12151f; alternate-background-color: #151926; "
        "   gridline-color: rgba(255,255,255,0.05); border: 1px solid rgba(255,255,255,0.07); "
        "   border-radius: 8px; color: #e8eaf0; selection-background-color: rgba(79,127,255,0.25); }"
        "QHeaderView::section { background-color: #161b26; color: #8a93b8; border: none; "
        "   border-bottom: 1px solid rgba(79,127,255,0.25); padding: 6px 8px; font-weight: 600; }"

        "QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }"
        "QScrollBar::handle:vertical { background: rgba(255,255,255,0.15); border-radius: 5px; min-height: 24px; }"
        "QScrollBar::handle:vertical:hover { background: rgba(79,127,255,0.5); }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }"
        "QScrollBar::handle:horizontal { background: rgba(255,255,255,0.15); border-radius: 5px; min-width: 24px; }"
        "QScrollBar::handle:horizontal:hover { background: rgba(79,127,255,0.5); }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"

        "QMenu { background: #161b26; border: 1px solid rgba(79,127,255,0.25); border-radius: 8px; padding: 6px; }"
        "QMenu::item { padding: 6px 22px 6px 14px; border-radius: 6px; color: #b5bad0; }"
        "QMenu::item:selected { background: rgba(79,127,255,0.18); color: #e8eaf0; }"

        "QProgressBar { background-color: #161b26; border: 1px solid rgba(255,255,255,0.08); "
        "   border-radius: 6px; text-align: center; color: #e8eaf0; }"
        "QProgressBar::chunk { background-color: qlineargradient(x1:0,y1:0,x2:1,y2:0, "
        "   stop:0 #4f7fff, stop:1 #ff9142); border-radius: 6px; }";
}

// Brief dim-then-brighten flash, used to draw the eye to a value that
// just changed (device counts, lease counts, freshly computed results).
inline void pulse(QWidget *w) {
    if (!w) return;
    auto *effect = new QGraphicsOpacityEffect(w);
    w->setGraphicsEffect(effect);

    auto *dim = new QPropertyAnimation(effect, "opacity", effect);
    dim->setDuration(90);
    dim->setStartValue(1.0);
    dim->setEndValue(0.35);

    auto *bright = new QPropertyAnimation(effect, "opacity", effect);
    bright->setDuration(220);
    bright->setStartValue(0.35);
    bright->setEndValue(1.0);
    bright->setEasingCurve(QEasingCurve::OutCubic);

    auto *seq = new QSequentialAnimationGroup(effect);
    seq->addAnimation(dim);
    seq->addAnimation(bright);
    QObject::connect(seq, &QSequentialAnimationGroup::finished, w, [w]() {
        QTimer::singleShot(0, w, [w]() { w->setGraphicsEffect(nullptr); });
    });
    seq->start(QAbstractAnimation::DeleteWhenStopped);
}

// Simple fade-in, used when a card/panel/result appears or refreshes.
inline void fadeIn(QWidget *w, int durationMs = 260) {
    if (!w) return;
    auto *effect = new QGraphicsOpacityEffect(w);
    w->setGraphicsEffect(effect);
    auto *anim = new QPropertyAnimation(effect, "opacity", effect);
    anim->setDuration(durationMs);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(anim, &QPropertyAnimation::finished, w, [w]() {
        QTimer::singleShot(0, w, [w]() { w->setGraphicsEffect(nullptr); });
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

// Loads a single-color SVG icon (Feather-style stroke icons or
// SVGRepo-style fill icons — both use one top-level color attribute),
// re-tints it, and rasterizes it. Used to give every icon in the app
// the same clean, consistent brand color instead of whatever muted
// or mismatched tone shipped in the source file.
inline QPixmap tintedSvgPixmap(const QString &resourcePath, int size, const QColor &color) {
    QFile file(resourcePath);
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return pixmap;

    QString svgText = QString::fromUtf8(file.readAll());
    static const QRegularExpression colorRe("(stroke|fill)=\"#[0-9a-fA-F]{3,8}\"");
    svgText.replace(colorRe, QString("\\1=\"%1\"").arg(color.name()));
    // Some icon sets (Lucide/Feather-derived) use stroke="currentColor" instead of
    // a literal hex value, relying on CSS inheritance that doesn't exist in this
    // standalone rendering context — resolve it explicitly or it renders unstyled.
    static const QRegularExpression currentColorRe("(stroke|fill)=\"currentColor\"");
    svgText.replace(currentColorRe, QString("\\1=\"%1\"").arg(color.name()));

    QSvgRenderer renderer(svgText.toUtf8());
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter);
    return pixmap;
}

inline QIcon tintedIcon(const QString &resourcePath, int size, const QColor &color) {
    return QIcon(tintedSvgPixmap(resourcePath, size, color));
}

} // namespace Theme
} // namespace gui
