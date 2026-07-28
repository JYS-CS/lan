#include "SplashScreen.h"
#include <QPainter>
#include <QPainterPath>
#include <QApplication>
#include <QScreen>
#include <QTimerEvent>
#include <QTimer>
#include <QFontMetrics>
#include <cmath>
#include <random>

// ── Tuning constants ──────────────────────────────────────────────────────────
static constexpr int   TIMER_MS              = 30;   // ~33 fps
static constexpr int   TYPE_TICKS_PER_CHAR   = 5;    // ~150 ms per letter
static constexpr int   MAX_PACKETS           = 18;
static constexpr float PACKET_SPEED_MIN      = 0.008f;
static constexpr float PACKET_SPEED_MAX      = 0.022f;

// Loading messages that cycle as progress advances
static const char* LOAD_MESSAGES[] = {
    "Initializing network interfaces...",
    "Scanning active connections...",
    "Resolving DNS cache...",
    "Mapping packet routes...",
    "Detecting network topology...",
    "Calibrating capture engine...",
    "Binding raw socket listeners...",
    "Loading protocol decoders...",
    "Starting traffic analysis...",
    "Ready."
};
static constexpr int LOAD_MSG_COUNT = sizeof(LOAD_MESSAGES) / sizeof(LOAD_MESSAGES[0]);

// Fake background labels
static const char* BG_LABELS[] = {
    "192.168.1.1 → 10.0.0.4",
    "TCP  443   ESTABLISHED",
    "UDP  53    QUERY",
    "172.16.0.12 → 8.8.8.8",
    "ICMP  TTL:64  echo-request",
    "TLS 1.3  HANDSHAKE",
    "ARP  who-has 192.168.1.254",
    "10.10.0.55 → 104.21.8.9",
    "HTTP/2  GET /api/v1/status",
    "DNS  A  example.com",
    "MTU: 1500   MSS: 1460",
    "VLAN 100  eth0.100",
    "BGP  AS64512  UPDATE",
    "RST  seq=3821049  ack=0",
    "FIN+ACK  port 22 → 54321",
    "IPv6  fe80::1  RA",
};
static constexpr int BG_LABEL_COUNT = sizeof(BG_LABELS) / sizeof(BG_LABELS[0]);

// ─────────────────────────────────────────────────────────────────────────────

static std::mt19937 rng(42);
static float randF(float lo, float hi) {
    return lo + (hi - lo) * (rng() / float(rng.max()));
}

// ── Constructor ───────────────────────────────────────────────────────────────
SplashScreen::SplashScreen(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::SplashScreen),
      m_progress(0),
      m_message(LOAD_MESSAGES[0]),
      m_frame(0),
      m_fullTitle("Net Monitor"),
      m_titleChars(0),
      m_typeTick(0)
{
    setAttribute(Qt::WA_TranslucentBackground);

    if (QScreen *screen = QApplication::primaryScreen())
        setGeometry(screen->geometry());
    else
        setWindowState(Qt::WindowFullScreen);

    initTopology();
    initLabels();

    startTimer(TIMER_MS);
}

// ── Topology setup ────────────────────────────────────────────────────────────
void SplashScreen::initTopology() {
    // 9 nodes arranged in a rough network shape (normalised coords)
    m_nodes = {
        { {0.10f, 0.30f}, 0.0f },   // 0 - left edge
        { {0.22f, 0.18f}, 1.1f },   // 1
        { {0.22f, 0.50f}, 2.2f },   // 2
        { {0.38f, 0.28f}, 0.7f },   // 3 - central-left hub
        { {0.38f, 0.62f}, 1.8f },   // 4
        { {0.55f, 0.35f}, 3.1f },   // 5 - central hub (brightest)
        { {0.55f, 0.65f}, 0.4f },   // 6
        { {0.72f, 0.22f}, 2.5f },   // 7
        { {0.72f, 0.50f}, 1.3f },   // 8
        { {0.88f, 0.38f}, 0.9f },   // 9 - right edge
    };

    m_edges = {
        {0, 1}, {0, 2},
        {1, 3}, {2, 3}, {2, 4},
        {3, 5}, {4, 5}, {4, 6},
        {5, 7}, {5, 8}, {6, 8},
        {7, 9}, {8, 9},
        {3, 4}, {5, 6},            // vertical links
    };

    // Seed initial packets
    for (int i = 0; i < 8; ++i)
        spawnPacket();
}

void SplashScreen::spawnPacket() {
    if (m_packets.size() >= MAX_PACKETS) return;
    Packet p;
    p.edgeIndex = rng() % m_edges.size();
    p.t         = randF(0.0f, 1.0f);
    p.speed     = randF(PACKET_SPEED_MIN, PACKET_SPEED_MAX);
    p.alpha     = randF(160.0f, 255.0f);
    m_packets.append(p);
}

// ── Label setup ───────────────────────────────────────────────────────────────
void SplashScreen::initLabels() {
    for (int i = 0; i < 14; ++i) {
        FloatingLabel lbl;
        lbl.text     = BG_LABELS[i % BG_LABEL_COUNT];
        lbl.pos      = { randF(0.01f, 0.80f), randF(0.05f, 0.92f) };
        lbl.alpha    = randF(0.0f, 40.0f);
        lbl.alphaDir = (rng() % 2 == 0) ? 1.0f : -1.0f;
        lbl.driftX   = randF(0.00005f, 0.00015f) * (rng() % 2 == 0 ? 1 : -1);
        m_labels.append(lbl);
    }
}

// ── Helper: node position mapped to screen ────────────────────────────────────
QPointF SplashScreen::nodePos(int idx) const {
    const auto &n = m_nodes[idx];
    // Topology lives in the left 60% of the screen, vertically centred
    float rx = 0.05f + n.pos.x() * 0.58f;
    float ry = 0.12f + n.pos.y() * 0.76f;
    return { rx * width(), ry * height() };
}

// ── Timer tick ────────────────────────────────────────────────────────────────
void SplashScreen::timerEvent(QTimerEvent *event) {
    Q_UNUSED(event);
    m_frame++;

    // Typewriter
    if (m_titleChars < m_fullTitle.length()) {
        if (++m_typeTick >= TYPE_TICKS_PER_CHAR) {
            m_typeTick = 0;
            m_titleChars++;
        }
    }

    // Advance packets; recycle finished ones
    for (auto &pkt : m_packets) {
        pkt.t += pkt.speed;
        if (pkt.t >= 1.0f) {
            // Respawn on a new random edge
            pkt.edgeIndex = rng() % m_edges.size();
            pkt.t         = 0.0f;
            pkt.speed     = randF(PACKET_SPEED_MIN, PACKET_SPEED_MAX);
            pkt.alpha     = randF(160.0f, 255.0f);
        }
    }

    // Occasionally spawn a new packet
    if (m_frame % 12 == 0)
        spawnPacket();

    // Drift + fade labels
    for (auto &lbl : m_labels) {
        lbl.pos.rx() += lbl.driftX;
        lbl.alpha    += lbl.alphaDir * 0.4f;
        if (lbl.alpha > 45.0f) { lbl.alpha = 45.0f; lbl.alphaDir = -1.0f; }
        if (lbl.alpha <  0.0f) {
            // Recycle: pick new text & position
            lbl.text     = BG_LABELS[rng() % BG_LABEL_COUNT];
            lbl.pos      = { randF(0.01f, 0.80f), randF(0.05f, 0.92f) };
            lbl.alpha    = 0.0f;
            lbl.alphaDir = 1.0f;
            lbl.driftX   = randF(0.00005f, 0.00015f) * (rng() % 2 == 0 ? 1 : -1);
        }
    }

    // Auto-advance loading message from progress
    int msgIdx = std::clamp(m_progress * (LOAD_MSG_COUNT - 1) / 100, 0, LOAD_MSG_COUNT - 1);
    m_message = LOAD_MESSAGES[msgIdx];

    update();
}

// ── setProgress / finish ──────────────────────────────────────────────────────
void SplashScreen::setProgress(int value, const QString &message) {
    m_progress = std::clamp(value, 0, 100);
    if (!message.isEmpty()) m_message = message;
    update();
}

void SplashScreen::finish(QWidget *mainWin) {
    if (mainWin) {
        // Pre-stretch the window boundaries behind the scenes to the full screen footprint
        if (QScreen *screen = QApplication::primaryScreen()) {
            mainWin->setGeometry(screen->availableGeometry());
        }
        // Then reveal it already inherently maximized
        mainWin->setWindowState(Qt::WindowMaximized);
        mainWin->showMaximized();
    }
    close();
    deleteLater();
}

// ═════════════════════════════════════════════════════════════════════════════
//  PAINT
// ═════════════════════════════════════════════════════════════════════════════
void SplashScreen::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    // Background
    p.fillRect(rect(), QColor(13, 17, 23));   // near-black with blue tint

    drawGrid(p);
    drawLabels(p);
    drawTopology(p);
    drawSignalWave(p);
    drawLogo(p);
    drawTitle(p);
    // drawStats(p); // Removed by user request
    drawProgressBar(p);
}

// ── 1. Subtle dot grid ────────────────────────────────────────────────────────
void SplashScreen::drawGrid(QPainter &p) const {
    const int spacing = 32;
    p.setPen(Qt::NoPen);
    for (int x = 0; x < width(); x += spacing) {
        for (int y = 0; y < height(); y += spacing) {
            p.setBrush(QColor(40, 55, 70, 180));
            p.drawEllipse(QPointF(x, y), 1.0, 1.0);
        }
    }
}

// ── 2. Floating IP / protocol labels ─────────────────────────────────────────
void SplashScreen::drawLabels(QPainter &p) const {
    QFont f;
    f.setFamilies({"JetBrains Mono", "Consolas", "Courier New"});
    f.setPointSize(10);
    f.setBold(true);
    p.setFont(f);
    for (const auto &lbl : m_labels) {
        p.setPen(QColor(0, 210, 255, int(lbl.alpha)));
        p.drawText(QPointF(lbl.pos.x() * width(), lbl.pos.y() * height()), lbl.text);
    }
}

// ── 3. Topology graph + packet dots ──────────────────────────────────────────
void SplashScreen::drawTopology(QPainter &p) const {
    // Edges
    for (const auto &edge : m_edges) {
        QPointF a = nodePos(edge.first);
        QPointF b = nodePos(edge.second);
        QLinearGradient g(a, b);
        g.setColorAt(0, QColor(0, 180, 255, 35));
        g.setColorAt(1, QColor(0, 255, 180, 35));
        p.setPen(QPen(QBrush(g), 1.0));
        p.drawLine(a, b);
    }

    // Packet dots
    p.setPen(Qt::NoPen);
    for (const auto &pkt : m_packets) {
        const auto &edge = m_edges[pkt.edgeIndex];
        QPointF a = nodePos(edge.first);
        QPointF b = nodePos(edge.second);
        QPointF pos = a + (b - a) * pkt.t;

        // Outer glow
        QRadialGradient glow(pos, 6);
        glow.setColorAt(0, QColor(0, 229, 255, int(pkt.alpha * 0.6f)));
        glow.setColorAt(1, QColor(0, 229, 255, 0));
        p.setBrush(glow);
        p.drawEllipse(pos, 6, 6);

        // Core dot
        p.setBrush(QColor(180, 240, 255, int(pkt.alpha)));
        p.drawEllipse(pos, 2, 2);
    }

    // Nodes
    for (int i = 0; i < m_nodes.size(); ++i) {
        QPointF pos = nodePos(i);
        float pulse = 0.5f + 0.5f * std::sin(m_frame * 0.06f + m_nodes[i].pulsePhase);
        bool  isHub = (i == 5);   // central hub is brighter

        // Glow ring
        QRadialGradient glow(pos, isHub ? 18 : 12);
        QColor glowCol = isHub ? QColor(0, 229, 255) : QColor(0, 160, 220);
        glowCol.setAlpha(int(50 * pulse));
        glow.setColorAt(0, glowCol);
        glowCol.setAlpha(0);
        glow.setColorAt(1, glowCol);
        p.setBrush(glow);
        p.setPen(Qt::NoPen);
        p.drawEllipse(pos, isHub ? 18 : 12, isHub ? 18 : 12);

        // Node circle
        QColor nodeCol = isHub ? QColor(0, 229, 255) : QColor(0, 180, 220);
        nodeCol.setAlpha(int(180 + 75 * pulse));
        p.setBrush(nodeCol);
        p.setPen(QPen(QColor(200, 240, 255, 120), 1));
        float r = isHub ? 5.5f : 3.5f;
        p.drawEllipse(pos, r, r);
    }
}

// ── 4. Scrolling signal wave (right side) ────────────────────────────────────
void SplashScreen::drawSignalWave(QPainter &p) const {
    // Draw two sine waves stacked, right 35% of screen
    const int waveLeft  = int(width() * 0.66f);
    const int waveRight = width() - 20;
    const int waveMidY  = int(height() * 0.42f);

    for (int layer = 0; layer < 2; ++layer) {
        QPainterPath path;
        bool started = false;
        float amp    = (layer == 0) ? 18.0f : 9.0f;
        float freq   = (layer == 0) ? 0.04f  : 0.07f;
        float offset = m_frame * (layer == 0 ? 2.5f : -1.8f);
        int   midY   = waveMidY + (layer == 0 ? 0 : 45);
        QColor col   = (layer == 0)
                       ? QColor(0, 229, 255, 180)
                       : QColor(0, 180, 120, 120);

        for (int x = waveLeft; x <= waveRight; x += 2) {
            float y = midY + amp * std::sin((x + offset) * freq);
            if (!started) { path.moveTo(x, y); started = true; }
            else           path.lineTo(x, y);
        }
        p.setPen(QPen(col, layer == 0 ? 1.5f : 1.0f));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }

    // Label
    QFont f;
    f.setFamilies({"JetBrains Mono", "Consolas", "Courier New"});
    f.setPointSize(9);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(0, 229, 255, 90));
    p.drawText(QPoint(waveLeft, waveMidY - 30), "SIGNAL  ▶");
}

// ── 5. Logo: interconnected-nodes icon ───────────────────────────────────────
void SplashScreen::drawLogo(QPainter &p) const {
    // Positioned completely center-aligned, shifted to top half
    const QPointF centre(width() * 0.5f, height() * 0.40f);
    const float   R = 28.0f;  // ring radius

    // Three orbital node positions
    QPointF nodes[3] = {
        centre + QPointF(0,       -R),
        centre + QPointF( R * 0.866f,  R * 0.5f),
        centre + QPointF(-R * 0.866f,  R * 0.5f),
    };

    // Connecting lines with gradient
    for (int i = 0; i < 3; ++i) {
        for (int j = i + 1; j < 3; ++j) {
            QLinearGradient g(nodes[i], nodes[j]);
            g.setColorAt(0, QColor(0, 229, 255, 160));
            g.setColorAt(1, QColor(0, 130, 255, 160));
            p.setPen(QPen(QBrush(g), 1.5f));
            p.drawLine(nodes[i], nodes[j]);
        }
    }

    // Outer glow on centre
    float pulse = 0.5f + 0.5f * std::sin(m_frame * 0.07f);
    QRadialGradient coreGlow(centre, 16 + 4 * pulse);
    coreGlow.setColorAt(0, QColor(0, 229, 255, int(80 * pulse)));
    coreGlow.setColorAt(1, QColor(0, 229, 255, 0));
    p.setBrush(coreGlow);
    p.setPen(Qt::NoPen);
    p.drawEllipse(centre, 20, 20);

    // Centre hub
    p.setBrush(QColor(0, 229, 255));
    p.setPen(QPen(QColor(180, 240, 255), 1));
    p.drawEllipse(centre, 5, 5);

    // Outer nodes
    for (const auto &n : nodes) {
        QRadialGradient ng(n, 8);
        ng.setColorAt(0, QColor(0, 200, 255, 60));
        ng.setColorAt(1, QColor(0, 200, 255, 0));
        p.setBrush(ng);
        p.setPen(Qt::NoPen);
        p.drawEllipse(n, 8, 8);

        p.setBrush(QColor(0, 180, 255));
        p.setPen(QPen(QColor(180, 240, 255), 1));
        p.drawEllipse(n, 4, 4);
    }
}

// ── 6. Typewriter app title ───────────────────────────────────────────────────
void SplashScreen::drawTitle(QPainter &p) const {
    QFont titleFont;
    titleFont.setFamilies({"Share Tech Mono", "JetBrains Mono", "Consolas", "Courier New"});
    titleFont.setPointSize(38);
    titleFont.setBold(true);
    titleFont.setLetterSpacing(QFont::AbsoluteSpacing, 4);
    p.setFont(titleFont);

    QString visible = m_fullTitle.left(m_titleChars);
    QFontMetrics fm(titleFont);

    // Centered block
    const int blockX = 0;
    const int blockW = width();
    const int titleY = int(height() * 0.52f); // Place text right under the logo

    // Subtle text shadow
    p.setPen(QColor(0, 229, 255, 40));
    p.drawText(QRect(blockX + 2, titleY + 2, blockW, 60), Qt::AlignCenter, visible);

    // Main text
    p.setPen(Qt::white);
    p.drawText(QRect(blockX, titleY, blockW, 60), Qt::AlignCenter, visible);

    // Blinking cursor while typing
    if (m_titleChars < m_fullTitle.length()) {
        bool cursorOn = (m_frame / 8) % 2 == 0;
        if (cursorOn) {
            int tw = fm.horizontalAdvance(visible);
            int textStartX = (width() - fm.horizontalAdvance(m_fullTitle)) / 2;
            int cx = textStartX + tw + 6;
            int cy = titleY + 14;
            p.setPen(QPen(QColor(0, 229, 255), 3));
            p.drawLine(cx, cy, cx, cy + fm.ascent());
        }
    }

    // Subtitle / tagline
    QFont subFont;
    subFont.setFamilies({"JetBrains Mono", "Consolas", "Courier New"});
    subFont.setPointSize(12);
    subFont.setBold(true);
    subFont.setLetterSpacing(QFont::AbsoluteSpacing, 2);
    p.setFont(subFont);
    p.setPen(QColor(0, 180, 255, 160));
    p.drawText(QRect(blockX, titleY + 65, blockW, 30), Qt::AlignCenter, "NETWORK TRAFFIC ANALYZER");
}

// ── 7. Fake live stats panel (right side) ────────────────────────────────────
void SplashScreen::drawStats(QPainter &p) const {
    const int panelX = int(width() * 0.62f);
    const int panelY = int(height() * 0.63f);
    const int panelW = int(width() * 0.34f);

    QFont f;
    f.setFamilies({"JetBrains Mono", "Consolas", "Courier New"});
    f.setPointSize(11);
    f.setBold(true);
    p.setFont(f);

    // Animated fake values driven by m_frame
    int  byteRate = 1024 + int(512 * std::sin(m_frame * 0.04f));
    int  pktRate  = 340  + int(80  * std::sin(m_frame * 0.06f + 1.0f));
    int  ifaces   = 3;

    struct Row { QString key; QString val; };
    Row rows[] = {
        { "INTERFACES", QString::number(ifaces) + " detected" },
        { "PKT/s      ", QString::number(pktRate) },
        { "KB/s       ", QString::number(byteRate) },
        { "MTU        ", "1500" },
        { "BUILD      ", "v2.1.0 · 20260420" },
    };

    int lineH = 18;
    for (int i = 0; i < 5; ++i) {
        int y = panelY + i * lineH;
        p.setPen(QColor(0, 160, 200, 130));
        p.drawText(QPoint(panelX, y), rows[i].key);
        p.setPen(QColor(180, 230, 255, 200));
        p.drawText(QPoint(panelX + 80, y), rows[i].val);
    }

    // Thin separator line above stats
    p.setPen(QPen(QColor(0, 180, 255, 50), 1));
    p.drawLine(panelX, panelY - 8, panelX + panelW, panelY - 8);
}

// ── 8. Progress bar (bottom) ──────────────────────────────────────────────────
void SplashScreen::drawProgressBar(QPainter &p) const {
    const int barH = 3;
    const int barY = height() - 48;
    const int barX = 40;
    const int barW = width() - 80;

    // Status message
    QFont msgFont;
    msgFont.setFamilies({"JetBrains Mono", "Consolas", "Courier New"});
    msgFont.setPointSize(11);
    msgFont.setBold(true);
    msgFont.setLetterSpacing(QFont::AbsoluteSpacing, 1);
    p.setFont(msgFont);
    p.setPen(QColor(0, 180, 200, 180));
    p.drawText(QRect(barX, barY - 22, barW, 18), Qt::AlignLeft, QString("> ") + m_message);

    // Percentage right-aligned
    p.setPen(QColor(0, 229, 255, 200));
    p.drawText(QRect(barX, barY - 22, barW, 18), Qt::AlignRight,
               QString::number(m_progress) + "%");

    // Track
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(30, 45, 60));
    p.drawRoundedRect(barX, barY, barW, barH, 1, 1);

    // Fill with gradient
    int fillW = (m_progress * barW) / 100;
    if (fillW > 0) {
        QLinearGradient g(barX, barY, barX + fillW, barY);
        g.setColorAt(0.0, QColor(0,  80, 200));
        g.setColorAt(0.6, QColor(0, 180, 255));
        g.setColorAt(1.0, QColor(0, 229, 255));
        p.setBrush(g);
        p.drawRoundedRect(barX, barY, fillW, barH, 1, 1);

        // Leading glow tip
        QRadialGradient tip(barX + fillW, barY + barH / 2, 8);
        tip.setColorAt(0, QColor(0, 229, 255, 200));
        tip.setColorAt(1, QColor(0, 229, 255, 0));
        p.setBrush(tip);
        p.drawEllipse(QPointF(barX + fillW, barY + barH / 2.0), 8, 8);
    }

    // Bottom-right version tag
    QFont vFont;
    vFont.setFamilies({"JetBrains Mono", "Consolas", "Courier New"});
    vFont.setPointSize(9);
    vFont.setBold(true);
    p.setFont(vFont);
    p.setPen(QColor(60, 90, 110, 180));
    p.drawText(QRect(0, height() - 18, width() - 10, 16),
               Qt::AlignRight, "v2.1.0  build 20260420");
}
