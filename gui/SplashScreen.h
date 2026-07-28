#pragma once

#include <QWidget>
#include <QString>
#include <QVector>
#include <QPointF>

// ── Packet dot travelling along a topology edge ───────────────────────────────
struct Packet {
    int   edgeIndex;
    float t;        // 0.0 → 1.0 progress along the edge
    float speed;
    float alpha;
};

// ── Node in the fake topology graph ──────────────────────────────────────────
struct TopoNode {
    QPointF pos;         // normalised 0–1 coords
    float   pulsePhase;  // per-node phase offset for glow animation
};

// ── Floating background label (IP / protocol text) ───────────────────────────
struct FloatingLabel {
    QString text;
    QPointF pos;       // normalised 0–1
    float   alpha;
    float   alphaDir;  // +1 = fading in, -1 = fading out
    float   driftX;    // horizontal drift per frame (normalised)
};

class SplashScreen : public QWidget {
    Q_OBJECT
public:
    explicit SplashScreen(QWidget *parent = nullptr);
    ~SplashScreen() override = default;

    void setProgress(int value, const QString &message = {});
    void finish(QWidget *mainWin);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void timerEvent(QTimerEvent *event) override;

    // Core
    int     m_progress;
    QString m_message;
    int     m_frame;

    // Typewriter
    QString m_fullTitle;
    int     m_titleChars;
    int     m_typeTick;

    // Topology
    QVector<TopoNode>        m_nodes;
    QVector<QPair<int,int>>  m_edges;
    QVector<Packet>          m_packets;

    // Floating labels
    QVector<FloatingLabel>   m_labels;

    // Helpers
    void    initTopology();
    void    initLabels();
    void    spawnPacket();
    QPointF nodePos(int idx) const;

    // Draw layers
    void drawGrid        (QPainter &p) const;
    void drawLabels      (QPainter &p) const;
    void drawTopology    (QPainter &p) const;
    void drawSignalWave  (QPainter &p) const;
    void drawLogo        (QPainter &p) const;
    void drawTitle       (QPainter &p) const;
    void drawStats       (QPainter &p) const;
    void drawProgressBar (QPainter &p) const;
};
