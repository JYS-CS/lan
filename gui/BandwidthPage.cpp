#include "BandwidthPage.h"
#include "Theme.h"
#include "../core/DatabaseManager.h"

#include <QPainter>
#include <QPainterPath>
#include <QHeaderView>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QFont>
#include <QFontMetrics>
#include <QDateTime>
#include <algorithm>

namespace gui {

// ─────────────────────────────────────────────────────────────────────────────
// Period table (combo index → seconds, 0 = all-time)
// ─────────────────────────────────────────────────────────────────────────────
const qint64 BandwidthPage::kPeriods[] = {
    0,                      // 0: All Time
    3600LL,                 // 1: Last Hour
    24 * 3600LL,            // 2: Today (24 h)
    7 * 24 * 3600LL,        // 3: Last 7 Days
    30 * 24 * 3600LL,       // 4: Last 30 Days
};

// ─────────────────────────────────────────────────────────────────────────────
// Format helpers
// ─────────────────────────────────────────────────────────────────────────────
static QString formatBytes(quint64 b) {
    if (b < 1024ULL)           return QString::number(b) + " B";
    if (b < 1024ULL * 1024)    return QString::number(b / 1024.0, 'f', 1) + " KB";
    if (b < 1024ULL * 1024 * 1024) return QString::number(b / (1024.0 * 1024), 'f', 1) + " MB";
    return QString::number(b / (1024.0 * 1024 * 1024), 'f', 2) + " GB";
}

static QString formatRate(quint32 bps) {
    return formatBytes(bps) + "/s";
}

QString BandwidthPage::fmtBps(quint64 bytes, bool perSec) {
    QString s = formatBytes(bytes);
    return perSec ? s + "/s" : s;
}

QString BandwidthPage::fmtBytes(quint64 bytes) {
    return formatBytes(bytes);
}

// ─────────────────────────────────────────────────────────────────────────────
// MiniSparkline
// ─────────────────────────────────────────────────────────────────────────────
MiniSparkline::MiniSparkline(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(60);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void MiniSparkline::addSample(quint32 rx, quint32 tx) {
    m_rx.push_back(rx);
    m_tx.push_back(tx);
    if ((int)m_rx.size() > kMaxPts) { m_rx.pop_front(); m_tx.pop_front(); }
    update();
}

void MiniSparkline::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor("#0f141b"));

    if (m_rx.empty()) return;

    quint32 peak = 1024;
    for (auto v : m_rx) if (v > peak) peak = v;
    for (auto v : m_tx) if (v > peak) peak = v;

    auto drawLine = [&](const std::deque<quint32> &data, QColor col) {
        if (data.size() < 2) return;
        double xStep = (double)(width() - 2) / (kMaxPts - 1);
        double h     = height() - 4;

        QList<QPointF> pts;
        int startIdx = kMaxPts - (int)data.size();
        for (int i = 0; i < (int)data.size(); ++i) {
            double x = (startIdx + i) * xStep + 1;
            double y = h - (h * data[i] / (double)peak) + 2;
            pts.append({x, y});
        }
        QPainterPath path;
        path.moveTo(pts.first());
        for (int i = 1; i < pts.size(); ++i) {
            QPointF mid = (pts[i - 1] + pts[i]) / 2.0;
            path.quadTo(pts[i - 1], mid);
        }
        path.lineTo(pts.last());

        QPainterPath fill = path;
        fill.lineTo(pts.last().x(), height());
        fill.lineTo(pts.first().x(), height());
        fill.closeSubpath();

        QLinearGradient grad(0, 0, 0, height());
        QColor areaTop = col; areaTop.setAlpha(60);
        grad.setColorAt(0.0, areaTop);
        grad.setColorAt(1.0, QColor(col.red(), col.green(), col.blue(), 0));
        p.fillPath(fill, grad);
        p.setPen(QPen(col, 1.5));
        p.drawPath(path);
    };

    drawLine(m_rx, QColor("#ff9142")); // download orange
    drawLine(m_tx, QColor("#4f7fff")); // upload blue
}

// ─────────────────────────────────────────────────────────────────────────────
// BandwidthDetailPanel
// ─────────────────────────────────────────────────────────────────────────────
BandwidthDetailPanel::BandwidthDetailPanel(QWidget *parent) : QWidget(parent) {
    setupUi();
}

void BandwidthDetailPanel::setupUi() {
    setFixedWidth(320);
    setObjectName("DetailPanel");
    setStyleSheet("QWidget#DetailPanel { background: #0f141b; border-left: 1px solid rgba(79,127,255,0.2); }");

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(16);

    // Header row
    auto *hdr = new QHBoxLayout();
    QLabel *title = new QLabel("DEVICE DETAIL", this);
    title->setStyleSheet("color: #4a5068; font-size: 10px; font-weight: bold; letter-spacing: 1.2px;");
    auto *closeBtn = new QPushButton("✕", this);
    closeBtn->setFixedSize(22, 22);
    closeBtn->setStyleSheet("QPushButton { background: rgba(255,255,255,0.05); border-radius: 4px; "
                            "color: #5a6175; font-size: 11px; border: none; } "
                            "QPushButton:hover { background: rgba(255,92,92,0.2); color: #ff5c5c; }");
    connect(closeBtn, &QPushButton::clicked, this, &BandwidthDetailPanel::closeRequested);
    hdr->addWidget(title);
    hdr->addStretch();
    hdr->addWidget(closeBtn);
    root->addLayout(hdr);

    // Device name
    m_nameLabel = new QLabel("—", this);
    m_nameLabel->setStyleSheet("color: #e8eaf0; font-size: 15px; font-weight: 600;");
    m_nameLabel->setWordWrap(true);
    root->addWidget(m_nameLabel);

    // Meta row
    m_macLabel = new QLabel(this);
    m_macLabel->setStyleSheet("color: #5a6175; font-size: 11px; font-family: 'JetBrains Mono', monospace;");
    m_vendorLabel = new QLabel(this);
    m_vendorLabel->setStyleSheet("color: #7c8299; font-size: 11px;");
    m_ipLabel = new QLabel(this);
    m_ipLabel->setStyleSheet("color: #7c8299; font-size: 11px; font-family: 'JetBrains Mono', monospace;");
    m_statusLabel = new QLabel(this);
    root->addWidget(m_macLabel);
    root->addWidget(m_ipLabel);
    root->addWidget(m_vendorLabel);
    root->addWidget(m_statusLabel);

    // Separator
    auto *sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color: rgba(255,255,255,0.05);");
    root->addWidget(sep);

    // Sparkline
    QLabel *chartLabel = new QLabel("LIVE BANDWIDTH (60s)", this);
    chartLabel->setStyleSheet("color: #4a5068; font-size: 9px; font-weight: bold; letter-spacing: 1px;");
    root->addWidget(chartLabel);
    m_sparkline = new MiniSparkline(this);
    root->addWidget(m_sparkline);

    // Separator
    auto *sep2 = new QFrame(this);
    sep2->setFrameShape(QFrame::HLine);
    sep2->setStyleSheet("color: rgba(255,255,255,0.05);");
    root->addWidget(sep2);

    // Stats grid
    auto createStat = [&](const QString &label, QLabel **valPtr) {
        auto *row = new QHBoxLayout();
        QLabel *lbl = new QLabel(label, this);
        lbl->setStyleSheet("color: #4a5068; font-size: 11px;");
        *valPtr = new QLabel("—", this);
        (*valPtr)->setStyleSheet("color: #e8eaf0; font-size: 11px; font-weight: 600; "
                                 "font-family: 'JetBrains Mono', monospace;");
        row->addWidget(lbl);
        row->addStretch();
        row->addWidget(*valPtr);
        return row;
    };

    root->addLayout(createStat("↓ Download Rate",  &m_rxRateLabel));
    root->addLayout(createStat("↑ Upload Rate",    &m_txRateLabel));
    root->addLayout(createStat("↓ Peak Download",  &m_peakRxLabel));
    root->addLayout(createStat("↑ Peak Upload",    &m_peakTxLabel));
    root->addLayout(createStat("↓ Total Download", &m_rxTotalLabel));
    root->addLayout(createStat("↑ Total Upload",   &m_txTotalLabel));

    // Separator
    auto *sep3 = new QFrame(this);
    sep3->setFrameShape(QFrame::HLine);
    sep3->setStyleSheet("color: rgba(255,255,255,0.05);");
    root->addWidget(sep3);

    // IP history
    QLabel *ipHistLabel = new QLabel("IP HISTORY", this);
    ipHistLabel->setStyleSheet("color: #4a5068; font-size: 9px; font-weight: bold; letter-spacing: 1px;");
    root->addWidget(ipHistLabel);

    m_ipHistTable = new QTableWidget(0, 2, this);
    m_ipHistTable->setHorizontalHeaderLabels({"IP Address", "Last Seen"});
    m_ipHistTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_ipHistTable->verticalHeader()->setVisible(false);
    m_ipHistTable->setShowGrid(false);
    m_ipHistTable->setFixedHeight(120);
    m_ipHistTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_ipHistTable->setSelectionMode(QAbstractItemView::NoSelection);
    root->addWidget(m_ipHistTable);

    root->addStretch();
}

void BandwidthDetailPanel::showDevice(const core::DeviceBandwidth &dev,
                                       const QList<core::IpHistoryEntry> &ipHistory) {
    m_currentMac = dev.mac;
    m_nameLabel->setText(dev.displayName);
    m_macLabel->setText(dev.mac);
    m_ipLabel->setText("IP: " + dev.ip);
    m_vendorLabel->setText(dev.vendor.isEmpty() ? "Unknown Vendor" : dev.vendor);
    m_statusLabel->setText(dev.online ? "● Online" : "○ Offline");
    m_statusLabel->setStyleSheet(dev.online
        ? "color: #3ddc84; font-size: 11px; font-weight: 600;"
        : "color: #5a6175; font-size: 11px;");
    updateLiveStats(dev);

    // IP history table
    m_ipHistTable->setRowCount(ipHistory.size());
    for (int i = 0; i < ipHistory.size(); ++i) {
        m_ipHistTable->setItem(i, 0, new QTableWidgetItem(ipHistory[i].ip));
        m_ipHistTable->setItem(i, 1, new QTableWidgetItem(
            ipHistory[i].lastSeen.toString("MMM d, hh:mm")));
    }
}

void BandwidthDetailPanel::updateLiveStats(const core::DeviceBandwidth &dev) {
    if (dev.mac != m_currentMac) return;
    m_rxRateLabel->setText(formatRate(dev.rxRate));
    m_txRateLabel->setText(formatRate(dev.txRate));
    m_peakRxLabel->setText(formatRate(dev.peakRx));
    m_peakTxLabel->setText(formatRate(dev.peakTx));
    m_rxTotalLabel->setText(formatBytes(dev.rxTotal));
    m_txTotalLabel->setText(formatBytes(dev.txTotal));
    if (m_sparkline) m_sparkline->addSample(dev.rxRate, dev.txRate);
}

void BandwidthDetailPanel::clearDevice() {
    m_currentMac.clear();
}

QString BandwidthDetailPanel::formatBps(quint64 bytes, bool perSec) {
    return formatBytes(bytes) + (perSec ? "/s" : "");
}

// ─────────────────────────────────────────────────────────────────────────────
// BandwidthPage
// ─────────────────────────────────────────────────────────────────────────────
BandwidthPage::BandwidthPage(core::NetworkManager *nm, QWidget *parent)
    : QWidget(parent), m_nm(nm)
{
    setupUi();
    applyTheme();
}

QWidget* BandwidthPage::createStatCard(const QString &label,
                                        const QString &color,
                                        QLabel **valuePtr,
                                        const QString &subLabel)
{
    QWidget *card = new QWidget(this);
    card->setFixedHeight(88);
    card->setStyleSheet(QString(
        "background: #161b26; border-radius: 12px; "
        "border: 0.5px solid rgba(255,255,255,0.06);"
    ));
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    QVBoxLayout *l = new QVBoxLayout(card);
    l->setContentsMargins(18, 14, 18, 14);
    l->setSpacing(4);

    QLabel *lbl = new QLabel(label.toUpper(), card);
    lbl->setStyleSheet("color: #4a5068; font-size: 9px; font-weight: bold; "
                       "letter-spacing: 0.8px; border: none; background: transparent;");

    *valuePtr = new QLabel("—", card);
    (*valuePtr)->setStyleSheet(QString("color: %1; font-size: 20px; font-weight: 500; "
                                        "border: none; background: transparent;").arg(color));

    l->addWidget(lbl);
    l->addWidget(*valuePtr);
    if (!subLabel.isEmpty()) {
        QLabel *sub = new QLabel(subLabel, card);
        sub->setStyleSheet("color: #4a5068; font-size: 9px; border: none; background: transparent;");
        l->addWidget(sub);
    }
    return card;
}

void BandwidthPage::setupUi() {
    setObjectName("BandwidthPage");

    // Main horizontal splitter (table | detail panel)
    auto *rootH = new QHBoxLayout(this);
    rootH->setContentsMargins(0, 0, 0, 0);
    rootH->setSpacing(0);

    // ── Left: scrollable content ──────────────────────────────────────────
    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setStyleSheet("QScrollArea { background: #0d1117; border: none; }");

    auto *content = new QWidget(scrollArea);
    content->setStyleSheet("background: #0d1117;");
    scrollArea->setWidget(content);

    auto *mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(28, 24, 28, 24);
    mainLayout->setSpacing(20);

    // ── Topology badge ────────────────────────────────────────────────────
    auto *headerRow = new QHBoxLayout();
    QLabel *pageTitle = new QLabel("Bandwidth Monitor", content);
    pageTitle->setStyleSheet("color: #e8eaf0; font-size: 18px; font-weight: 700;");
    m_topologyBadge = new QLabel("Detecting topology…", content);
    m_topologyBadge->setStyleSheet(
        "background: rgba(245,166,35,0.12); color: #f5a623; border-radius: 6px; "
        "padding: 3px 12px; font-size: 10px; font-weight: bold; border: 1px solid rgba(245,166,35,0.3);"
    );
    headerRow->addWidget(pageTitle);
    headerRow->addStretch();
    headerRow->addWidget(m_topologyBadge);
    mainLayout->addLayout(headerRow);

    // ── Overview stat cards ───────────────────────────────────────────────
    auto *cardsRow = new QHBoxLayout();
    cardsRow->setSpacing(12);
    cardsRow->addWidget(createStatCard("↓ Download Rate",   "#ff9142", &m_cardRxRate));
    cardsRow->addWidget(createStatCard("↑ Upload Rate",     "#4f7fff", &m_cardTxRate));
    cardsRow->addWidget(createStatCard("↓ Total Download",  "#ff9142", &m_cardRxTotal));
    cardsRow->addWidget(createStatCard("↑ Total Upload",    "#4f7fff", &m_cardTxTotal));
    cardsRow->addWidget(createStatCard("Peak Download",     "#34e4a0", &m_cardPeakRx, "session high"));
    cardsRow->addWidget(createStatCard("Active Devices",    "#e8eaf0", &m_cardDevices));
    mainLayout->addLayout(cardsRow);

    // ── Device Bandwidth Table ────────────────────────────────────────────
    QLabel *tableTitle = new QLabel("PER-DEVICE BANDWIDTH", content);
    tableTitle->setStyleSheet("color: #4a5068; font-size: 10px; font-weight: bold; letter-spacing: 1.2px;");
    mainLayout->addWidget(tableTitle);

    m_deviceTable = new QTableWidget(0, 8, content);
    m_deviceTable->setHorizontalHeaderLabels({
        "#", "Device", "IP", "Status",
        "↓ Rate", "↑ Rate", "↓ Total", "↑ Total"
    });
    m_deviceTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_deviceTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_deviceTable->setColumnWidth(0, 36);
    m_deviceTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_deviceTable->setColumnWidth(3, 70);
    m_deviceTable->verticalHeader()->setVisible(false);
    m_deviceTable->setShowGrid(false);
    m_deviceTable->setAlternatingRowColors(true);
    m_deviceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_deviceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_deviceTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_deviceTable->setMinimumHeight(280);
    connect(m_deviceTable, &QTableWidget::cellClicked, this, &BandwidthPage::onDeviceRowClicked);
    mainLayout->addWidget(m_deviceTable);

    // ── Historical Top Talkers ────────────────────────────────────────────
    auto *talkersHeader = new QHBoxLayout();
    QLabel *talkersTitle = new QLabel("TOP BANDWIDTH CONSUMERS", content);
    talkersTitle->setStyleSheet("color: #4a5068; font-size: 10px; font-weight: bold; letter-spacing: 1.2px;");
    m_periodCombo = new QComboBox(content);
    m_periodCombo->addItems({"All Time", "Last Hour", "Today (24h)", "Last 7 Days", "Last 30 Days"});
    m_periodCombo->setFixedWidth(150);
    m_periodCombo->setStyleSheet(
        "QComboBox { background: #161b26; border: 1px solid rgba(255,255,255,0.08); "
        "border-radius: 6px; padding: 4px 10px; color: #e8eaf0; font-size: 11px; } "
        "QComboBox::drop-down { border: none; } "
        "QComboBox QAbstractItemView { background: #161b26; color: #e8eaf0; "
        "selection-background-color: rgba(79,127,255,0.2); border: none; }"
    );
    connect(m_periodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BandwidthPage::onPeriodChanged);
    talkersHeader->addWidget(talkersTitle);
    talkersHeader->addStretch();
    talkersHeader->addWidget(m_periodCombo);
    mainLayout->addLayout(talkersHeader);

    // Container for the top-talker bars
    auto *talkersCard = new QWidget(content);
    talkersCard->setStyleSheet("background: #161b26; border-radius: 12px; "
                               "border: 0.5px solid rgba(255,255,255,0.06);");
    m_topTalkersLayout = new QVBoxLayout(talkersCard);
    m_topTalkersLayout->setContentsMargins(20, 16, 20, 16);
    m_topTalkersLayout->setSpacing(10);

    auto *scrollTalkers = new QScrollArea(content);
    scrollTalkers->setWidget(talkersCard);
    scrollTalkers->setWidgetResizable(true);
    scrollTalkers->setFixedHeight(320);
    scrollTalkers->setFrameShape(QFrame::NoFrame);
    scrollTalkers->setStyleSheet("QScrollArea { background: transparent; border: none; }");
    mainLayout->addWidget(scrollTalkers);

    mainLayout->addStretch();

    rootH->addWidget(scrollArea, 1);

    // ── Detail panel (hidden initially) ──────────────────────────────────
    m_detailPanel = new BandwidthDetailPanel(this);
    m_detailPanel->setVisible(false);
    connect(m_detailPanel, &BandwidthDetailPanel::closeRequested,
            this, &BandwidthPage::onDetailCloseRequested);
    rootH->addWidget(m_detailPanel, 0);

    // Slide-in animation
    m_detailAnim = new QPropertyAnimation(m_detailPanel, "maximumWidth", this);
    m_detailAnim->setDuration(220);
    m_detailAnim->setEasingCurve(QEasingCurve::OutCubic);
}

void BandwidthPage::applyTheme() {
    setStyleSheet(
        "gui--BandwidthPage { background: #0d1117; }"
        "QTableWidget { background: #12151f; alternate-background-color: #13171f; "
        "  border: none; color: #e8eaf0; gridline-color: transparent; }"
        "QHeaderView::section { background: #161b26; color: #4a5068; font-size: 9px; "
        "  font-weight: bold; border: none; border-bottom: 1px solid rgba(79,127,255,0.2); "
        "  padding: 5px 8px; letter-spacing: 0.5px; }"
        "QTableWidget::item:selected { background: rgba(79,127,255,0.18); }"
        "QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }"
        "QScrollBar::handle:vertical { background: rgba(255,255,255,0.1); border-radius: 4px; min-height: 20px; }"
        "QScrollBar::handle:vertical:hover { background: rgba(79,127,255,0.4); }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    );
}

// ─────────────────────────────────────────────────────────────────────────────
// Slot: topology detected
// ─────────────────────────────────────────────────────────────────────────────
void BandwidthPage::onTopologyDetected(core::TopologyCapability cap) {
    m_topology = cap;
    switch (cap) {
        case core::TopologyCapability::FullGateway:
            m_topologyBadge->setText("● GATEWAY MODE — Full Per-Device Visibility");
            m_topologyBadge->setStyleSheet(
                "background: rgba(52,228,160,0.1); color: #34e4a0; border-radius: 6px; "
                "padding: 3px 12px; font-size: 10px; font-weight: bold; "
                "border: 1px solid rgba(52,228,160,0.3);");
            // Rename columns to show direction correctly
            m_deviceTable->setHorizontalHeaderLabels(
                {"#", "Device", "IP", "Status",
                 "↓ Download", "↑ Upload", "Total ↓", "Total ↑"});
            break;
        case core::TopologyCapability::PartialSniffer:
            m_topologyBadge->setText("◐ SNIFFER MODE — LAN Traffic Only");
            m_topologyBadge->setStyleSheet(
                "background: rgba(245,166,35,0.1); color: #f5a623; border-radius: 6px; "
                "padding: 3px 12px; font-size: 10px; font-weight: bold; "
                "border: 1px solid rgba(245,166,35,0.3);");
            m_deviceTable->setHorizontalHeaderLabels(
                {"#", "Device", "IP", "Status",
                 "LAN RX", "LAN TX", "Total RX", "Total TX"});
            break;
        case core::TopologyCapability::Unsupported:
            m_topologyBadge->setText("✕ Cannot measure per-device bandwidth");
            m_topologyBadge->setStyleSheet(
                "background: rgba(255,92,92,0.1); color: #ff5c5c; border-radius: 6px; "
                "padding: 3px 12px; font-size: 10px; font-weight: bold; "
                "border: 1px solid rgba(255,92,92,0.3);");
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Slot: aggregate LAN stats → overview cards
// ─────────────────────────────────────────────────────────────────────────────
void BandwidthPage::onLanStatsUpdated(quint64 rxTotal, quint64 txTotal,
                                       quint32 rxRate, quint32 txRate)
{
    m_cardRxRate->setText(formatRate(rxRate));
    m_cardTxRate->setText(formatRate(txRate));
    m_cardRxTotal->setText(formatBytes(rxTotal));
    m_cardTxTotal->setText(formatBytes(txTotal));

    if (rxRate > m_sessionPeakRx) {
        m_sessionPeakRx = rxRate;
        m_cardPeakRx->setText(formatRate(m_sessionPeakRx));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Slot: per-device bandwidth → update the device table
// ─────────────────────────────────────────────────────────────────────────────
void BandwidthPage::onBandwidthUpdated(const QList<core::DeviceBandwidth> &devices) {
    m_lastDevices = devices;

    // Sort by rxTotal+txTotal descending for the rank column
    QList<core::DeviceBandwidth> sorted = devices;
    std::sort(sorted.begin(), sorted.end(),
              [](const core::DeviceBandwidth &a, const core::DeviceBandwidth &b) {
                  return (a.rxTotal + a.txTotal) > (b.rxTotal + b.txTotal);
              });

    // Filter out zero-traffic devices with no name
    sorted.erase(std::remove_if(sorted.begin(), sorted.end(),
        [](const core::DeviceBandwidth &d) {
            return d.rxTotal == 0 && d.txTotal == 0 && d.mac.startsWith("ip:");
        }), sorted.end());

    // Count online
    int onlineCount = 0;
    for (const auto &d : sorted) if (d.online) ++onlineCount;
    m_cardDevices->setText(QString::number(onlineCount));

    // Update table
    m_deviceTable->setRowCount(sorted.size());
    for (int i = 0; i < sorted.size(); ++i) {
        const auto &dev = sorted[i];
        bool active = dev.rxRate > 0 || dev.txRate > 0;

        auto *rankItem = new QTableWidgetItem(QString::number(i + 1));
        rankItem->setTextAlignment(Qt::AlignCenter);
        rankItem->setForeground(QColor("#4a5068"));

        auto *nameItem = new QTableWidgetItem(dev.displayName);
        nameItem->setForeground(QColor("#e8eaf0"));

        auto *ipItem = new QTableWidgetItem(dev.ip);
        ipItem->setForeground(QColor("#7c8299"));
        ipItem->setFont(QFont("JetBrains Mono", 10));

        auto *statusItem = new QTableWidgetItem(dev.online ? "Online" : "Offline");
        statusItem->setForeground(dev.online ? QColor("#3ddc84") : QColor("#4a5068"));

        auto *rxRateItem = new QTableWidgetItem(active ? formatRate(dev.rxRate) : "");
        rxRateItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        rxRateItem->setForeground(QColor("#ff9142"));
        rxRateItem->setFont(QFont("JetBrains Mono", 10));

        auto *txRateItem = new QTableWidgetItem(active ? formatRate(dev.txRate) : "");
        txRateItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        txRateItem->setForeground(QColor("#4f7fff"));
        txRateItem->setFont(QFont("JetBrains Mono", 10));

        auto *rxTotalItem = new QTableWidgetItem(formatBytes(dev.rxTotal));
        rxTotalItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        rxTotalItem->setForeground(QColor("#c0a870"));
        rxTotalItem->setFont(QFont("JetBrains Mono", 10));

        auto *txTotalItem = new QTableWidgetItem(formatBytes(dev.txTotal));
        txTotalItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        txTotalItem->setForeground(QColor("#7090c0"));
        txTotalItem->setFont(QFont("JetBrains Mono", 10));

        // Store MAC in the rank item for click handling
        rankItem->setData(Qt::UserRole, dev.mac);

        m_deviceTable->setItem(i, 0, rankItem);
        m_deviceTable->setItem(i, 1, nameItem);
        m_deviceTable->setItem(i, 2, ipItem);
        m_deviceTable->setItem(i, 3, statusItem);
        m_deviceTable->setItem(i, 4, rxRateItem);
        m_deviceTable->setItem(i, 5, txRateItem);
        m_deviceTable->setItem(i, 6, rxTotalItem);
        m_deviceTable->setItem(i, 7, txTotalItem);

        m_deviceTable->setRowHeight(i, 34);
    }

    // If detail panel is open, keep it up to date
    if (m_detailVisible && !m_selectedMac.isEmpty()) {
        for (const auto &dev : devices) {
            if (dev.mac == m_selectedMac) {
                m_detailPanel->updateLiveStats(dev);
                break;
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Slot: top talkers (live, sorted by total) → not used directly for the
//       historical panel (that uses DB), but kept for potential future use
// ─────────────────────────────────────────────────────────────────────────────
void BandwidthPage::onTopTalkersUpdated(const QList<core::DeviceBandwidth> &top) {
    Q_UNUSED(top)
    // Live top-talkers are shown in the device table (ranked by column 0).
    // The historical panel is refreshed on demand via onPeriodChanged().
}

// ─────────────────────────────────────────────────────────────────────────────
// Historical top talkers
// ─────────────────────────────────────────────────────────────────────────────
void BandwidthPage::refreshHistoricalTalkers() {
    int idx = m_periodCombo->currentIndex();
    qint64 period = (idx >= 0 && idx < 5) ? kPeriods[idx] : 0;

    // Choose the appropriate aggregation table for the period
    QString table = "bw_samples";
    if (period == 0)                 table = "bw_days";
    else if (period <= 3600)         table = "bw_samples";
    else if (period <= 7 * 86400)    table = "bw_minutes";
    else                             table = "bw_hours";

    auto entries = core::DatabaseManager::instance().getTopTalkers(10, period, table);

    // Clear old bars
    while (QLayoutItem *item = m_topTalkersLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    if (entries.isEmpty()) {
        QLabel *empty = new QLabel("No historical data yet — data is written every 5 seconds.", this);
        empty->setStyleSheet("color: #4a5068; font-size: 11px; padding: 20px;");
        empty->setAlignment(Qt::AlignCenter);
        m_topTalkersLayout->addWidget(empty);
        return;
    }

    quint64 maxTotal = entries.first().total > 0 ? entries.first().total : 1;

    static const QColor kBarColors[] = {
        QColor(79,  127, 255),
        QColor(255, 145,  66),
        QColor(52,  228, 160),
        QColor(94,  234, 212),
        QColor(245, 166,  35),
        QColor(255,  92,  92),
        QColor(174, 128, 255),
        QColor(52,  211, 153),
        QColor(251, 191,  36),
        QColor(239, 119,  51),
    };

    for (int i = 0; i < entries.size(); ++i) {
        const auto &e = entries[i];
        QColor col = kBarColors[i % 10];

        // Resolve display name from live device list
        QString name = e.mac;
        for (const auto &dev : m_lastDevices) {
            if (dev.mac == e.mac) { name = dev.displayName; break; }
        }

        // Row widget
        auto *row = new QWidget(this);
        row->setFixedHeight(38);
        auto *rowL = new QHBoxLayout(row);
        rowL->setContentsMargins(0, 4, 0, 4);
        rowL->setSpacing(10);

        // Rank
        QLabel *rankLbl = new QLabel(QString::number(i + 1), row);
        rankLbl->setFixedWidth(20);
        rankLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        rankLbl->setStyleSheet("color: #4a5068; font-size: 10px;");
        rowL->addWidget(rankLbl);

        // Bar + label container
        auto *barWidget = new QWidget(row);
        barWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        auto *barLayout = new QVBoxLayout(barWidget);
        barLayout->setContentsMargins(0, 0, 0, 0);
        barLayout->setSpacing(2);

        QLabel *nameLbl = new QLabel(name, barWidget);
        nameLbl->setStyleSheet("color: #c0c6e0; font-size: 11px; font-weight: 500;");
        barLayout->addWidget(nameLbl);

        // Progress bar simulation using a styled QWidget
        double ratio = (maxTotal > 0) ? (double)e.total / (double)maxTotal : 0.0;
        auto *barTrack = new QWidget(barWidget);
        barTrack->setFixedHeight(6);
        barTrack->setStyleSheet("background: rgba(255,255,255,0.05); border-radius: 3px;");
        auto *barFill = new QWidget(barTrack);
        barFill->setGeometry(0, 0, (int)(barTrack->width() * ratio), 6);
        barFill->setStyleSheet(QString("background: %1; border-radius: 3px;").arg(col.name()));
        barLayout->addWidget(barTrack);

        // We use a fixed-width fill via stylesheet (paint on resize would need paintEvent)
        // Use percentage-based approach via pseudo-progress
        // Simple approach: use a QProgressBar-like label
        barTrack->hide(); // replaced below
        barLayout->removeWidget(barTrack);
        delete barTrack;

        // Simpler: just show a colored line with fixed width percentage
        auto *progressRow = new QHBoxLayout();
        progressRow->setContentsMargins(0, 0, 0, 0);
        progressRow->setSpacing(0);
        auto *fillW = new QWidget(barWidget);
        fillW->setFixedHeight(5);
        fillW->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        // We'll paint this as an overlay — for simplicity use stylesheet with fixed min-width
        QString barStyle = QString(
            "background: qlineargradient(x1:0,y1:0,x2:1,y2:0, "
            "stop:0 rgba(%1,%2,%3,0.5), stop:1 rgba(%1,%2,%3,1.0)); "
            "border-radius: 2px;"
        ).arg(col.red()).arg(col.green()).arg(col.blue());
        fillW->setStyleSheet(barStyle);
        fillW->setFixedWidth(qMax(4, (int)(350 * ratio)));
        progressRow->addWidget(fillW);
        progressRow->addStretch();
        barLayout->addLayout(progressRow);

        rowL->addWidget(barWidget, 1);

        // Total label
        QLabel *totLbl = new QLabel(formatBytes(e.total), row);
        totLbl->setFixedWidth(72);
        totLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        totLbl->setStyleSheet(QString("color: %1; font-size: 11px; font-weight: 600; "
                                       "font-family: 'JetBrains Mono', monospace;").arg(col.name()));
        rowL->addWidget(totLbl);

        m_topTalkersLayout->addWidget(row);
    }
    m_topTalkersLayout->addStretch();
}

void BandwidthPage::onPeriodChanged(int /*index*/) {
    refreshHistoricalTalkers();
}

// ─────────────────────────────────────────────────────────────────────────────
// Device row click → slide in detail panel
// ─────────────────────────────────────────────────────────────────────────────
void BandwidthPage::onDeviceRowClicked(int row, int /*col*/) {
    auto *rankItem = m_deviceTable->item(row, 0);
    if (!rankItem) return;
    QString mac = rankItem->data(Qt::UserRole).toString();
    if (mac.isEmpty()) return;

    // Find the device
    core::DeviceBandwidth found;
    bool found_ok = false;
    for (const auto &dev : m_lastDevices) {
        if (dev.mac == mac) { found = dev; found_ok = true; break; }
    }
    if (!found_ok) return;

    // Get IP history from DB
    auto ipHist = core::DatabaseManager::instance().getIpHistory(mac);

    m_selectedMac = mac;
    m_detailPanel->showDevice(found, ipHist);

    // Slide in
    if (!m_detailVisible) {
        m_detailVisible = true;
        m_detailPanel->setVisible(true);
        m_detailAnim->stop();
        m_detailAnim->setStartValue(0);
        m_detailAnim->setEndValue(320);
        m_detailAnim->start();
    }
}

void BandwidthPage::onDetailCloseRequested() {
    m_detailVisible = false;
    m_selectedMac.clear();
    m_detailPanel->clearDevice();

    m_detailAnim->stop();
    m_detailAnim->setStartValue(320);
    m_detailAnim->setEndValue(0);
    connect(m_detailAnim, &QPropertyAnimation::finished, this, [this]() {
        m_detailPanel->setVisible(false);
        disconnect(m_detailAnim, &QPropertyAnimation::finished, this, nullptr);
    });
    m_detailAnim->start();
}

} // namespace gui
