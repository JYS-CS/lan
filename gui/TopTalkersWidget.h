#pragma once

#include <QWidget>
#include <QList>
#include "../core/Device.h"

namespace gui {

// Top-5 bandwidth consumers panel.
// Renders horizontal proportional bars for the top devices by current traffic.
// Purely custom QPainter — production ready, no heap-allocated sub-widgets.
class TopTalkersWidget : public QWidget {
    Q_OBJECT

public:
    explicit TopTalkersWidget(QWidget *parent = nullptr);

public slots:
    void updateDevices(const QList<core::Device> &devices);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct TalkerEntry {
        QString label;   // alias or hostname or IP
        QString ipStr;
        quint64 totalBps = 0;
        quint64 upBps    = 0;
        quint64 downBps  = 0;
    };

    QList<TalkerEntry> m_entries; // sorted descending by totalBps, max 5

    static quint64 parseBps(const QString &bwStr);
    static QString formatBps(quint64 bps);
};

} // namespace gui
