#include "TopTalkersWidget.h"
#include "Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QFontDatabase>
#include <QRegularExpression>
#include <algorithm>

namespace gui {

static const QColor kBarColors[] = {
    QColor(79,  127, 255), // blue
    QColor(52,  228, 160), // green
    QColor(245, 166,  35), // amber
    QColor(94,  234, 212), // teal
    QColor(255,  92,  92), // red
};

// ─── Helpers ─────────────────────────────────────────────────────────────────

quint64 TopTalkersWidget::parseBps(const QString &bwStr) {
    static const QRegularExpression re(R"(^([\d\.]+)\s*([KMGB]*)/s)");
    QRegularExpressionMatch m = re.match(bwStr);
    if (!m.hasMatch()) return 0;
    double val  = m.captured(1).toDouble();
    QString unit= m.captured(2);
    if (unit == "K" || unit == "KB") val *= 1024;
    else if (unit == "M" || unit == "MB") val *= 1024*1024;
    else if (unit == "G" || unit == "GB") val *= 1024*1024*1024;
    return (quint64)val;
}

QString TopTalkersWidget::formatBps(quint64 bps) {
    if (bps == 0)               return "0 B/s";
    if (bps < 1024ULL)          return QString::number(bps)                      + " B/s";
    if (bps < 1024ULL*1024)     return QString::number(bps/1024.0,    'f', 1)   + " KB/s";
    if (bps < 1024ULL*1024*1024)return QString::number(bps/(1024.0*1024),'f',1) + " MB/s";
    return                             QString::number(bps/(1024.0*1024*1024),'f',2) + " GB/s";
}

// ─── Constructor ─────────────────────────────────────────────────────────────

TopTalkersWidget::TopTalkersWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(140);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

// ─── Data ────────────────────────────────────────────────────────────────────

void TopTalkersWidget::updateDevices(const QList<core::Device> &devices) {
    m_entries.clear();
    for (const auto &d : devices) {
        quint64 up   = parseBps(d.upBandwidth());
        quint64 down = parseBps(d.downBandwidth());
        quint64 total= up + down;
        if (total == 0) continue;
        TalkerEntry e;
        e.upBps    = up;
        e.downBps  = down;
        e.totalBps = total;
        e.ipStr    = d.ip();
        // Prefer alias → hostname → IP
        e.label = !d.alias().isEmpty()    ? d.alias()
                : d.hostname() != "Unknown" ? d.hostname()
                :                             d.ip();
        m_entries.append(e);
    }
    // Sort descending by total bandwidth
    std::sort(m_entries.begin(), m_entries.end(),
              [](const TalkerEntry &a, const TalkerEntry &b){ return a.totalBps > b.totalBps; });
    if (m_entries.size() > 5) m_entries.resize(5);
    update();
}

// ─── Paint ───────────────────────────────────────────────────────────────────

void TopTalkersWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRect r = rect();

    // Background card
    QPainterPath bg;
    bg.addRoundedRect(r, 8, 8);
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::OpsPanel);
    p.drawPath(bg);
    p.setPen(QPen(Theme::OpsBorder, 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(bg);

    // Header
    QFont hdrFont("JetBrains Mono", 8);
    hdrFont.setBold(true);
    p.setFont(hdrFont);
    p.setPen(Theme::OpsTextDim);
    p.drawText(QRect(r.left() + 14, r.top() + 10, r.width() - 28, 14),
               Qt::AlignLeft | Qt::AlignVCenter, "TOP TALKERS");

    if (m_entries.isEmpty()) {
        QFont ef("Inter", 10);
        p.setFont(ef);
        p.setPen(Theme::OpsTextFaint);
        p.drawText(r, Qt::AlignCenter, "No active traffic");
        return;
    }

    quint64 maxBps = m_entries.first().totalBps;
    if (maxBps == 0) maxBps = 1;

    const int rowH    = 26;
    const int startY  = r.top() + 30;
    const int barLeft = r.left() + 14;
    const int valW    = 80;
    const int barRight= r.right() - valW - 10;
    const int barW    = barRight - barLeft;

    for (int i = 0; i < m_entries.size(); ++i) {
        const auto &e = m_entries[i];
        QColor col = kBarColors[i % 5];
        int y = startY + i * rowH;

        double ratio = (double)e.totalBps / (double)maxBps;

        // Bar track (dim background)
        QRect trackRect(barLeft, y + 6, barW, 10);
        QPainterPath track;
        track.addRoundedRect(trackRect, 5, 5);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(30, 38, 50));
        p.drawPath(track);

        // Bar fill
        int fillW = std::max(10, (int)(barW * ratio));
        QRect fillRect(barLeft, y + 6, fillW, 10);
        QPainterPath fill;
        fill.addRoundedRect(fillRect, 5, 5);
        QLinearGradient grad(fillRect.topLeft(), fillRect.topRight());
        QColor fillDim = col; fillDim.setAlpha(120);
        grad.setColorAt(0.0, fillDim);
        grad.setColorAt(1.0, col);
        p.setBrush(grad);
        p.drawPath(fill);

        // Rank dot
        p.setBrush(col);
        p.drawEllipse(barLeft, y + 8, 7, 7);

        // Label (hostname / IP)
        QFont lf("JetBrains Mono", 8);
        p.setFont(lf);
        p.setPen(Theme::OpsTextPrimary);
        QRect labelRect(barLeft + 12, y, barW - 12, rowH);
        QFontMetrics fm(lf);
        QString elided = fm.elidedText(e.label, Qt::ElideRight, barW - 12 - valW);
        p.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, elided);

        // Value pill
        QString valStr = formatBps(e.totalBps);
        QRect valRect(barRight + 4, y + 3, valW - 8, rowH - 6);
        QPainterPath valPill;
        valPill.addRoundedRect(valRect, 4, 4);
        QColor pillBg = col; pillBg.setAlpha(25);
        p.setPen(Qt::NoPen);
        p.setBrush(pillBg);
        p.drawPath(valPill);

        QFont vf("JetBrains Mono", 8);
        vf.setBold(true);
        p.setFont(vf);
        p.setPen(col);
        p.drawText(valRect, Qt::AlignCenter, valStr);
    }
}

} // namespace gui
