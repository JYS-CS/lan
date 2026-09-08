#include "TopologyPage.h"
#include "Theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGraphicsSceneHoverEvent>
#include <QWheelEvent>
#include <QtMath>
#include <QToolTip>

namespace gui {

// ─── TopologyNodeItem ──────────────────────────────────────────────────────────
TopologyNodeItem::TopologyNodeItem(bool isHub, QGraphicsItem *parent)
    : QGraphicsEllipseItem(parent), m_isHub(isHub)
{
    m_radius = isHub ? 26 : 18;
    setRect(-m_radius, -m_radius, m_radius * 2, m_radius * 2);
    setBrush(QBrush(QColor("#0f141b")));
    setPen(QPen(QColor("#1c232c"), isHub ? 2 : 1.5));
    setAcceptHoverEvents(true);
    setZValue(isHub ? 10 : 5);

    m_iconItem = new QGraphicsPixmapItem(this);
    m_labelItem = new QGraphicsTextItem(this);
    m_labelItem->setDefaultTextColor(QColor("#7c8798"));
    QFont f("Inter", isHub ? 10 : 9, isHub ? QFont::Bold : QFont::Normal);
    m_labelItem->setFont(f);
}

void TopologyNodeItem::setIcon(const QPixmap &pixmap) {
    m_iconItem->setPixmap(pixmap);
    m_iconItem->setOffset(-pixmap.width() / 2.0, -pixmap.height() / 2.0);
}

void TopologyNodeItem::setLabel(const QString &text) {
    m_labelItem->setPlainText(text);
    QRectF br = m_labelItem->boundingRect();
    m_labelItem->setPos(-br.width() / 2.0, m_radius + 4);
}

void TopologyNodeItem::setStatusColor(const QColor &color) {
    setPen(QPen(color, m_isHub ? 3 : 2));
}

void TopologyNodeItem::animateTo(const QPointF &target) {
    auto *anim = new QPropertyAnimation(this, "pos", this);
    anim->setDuration(400);
    anim->setStartValue(pos());
    anim->setEndValue(target);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void TopologyNodeItem::hoverEnterEvent(QGraphicsSceneHoverEvent *event) {
    setPen(QPen(pen().color(), (m_isHub ? 3 : 2) + 1.5));
    QGraphicsEllipseItem::hoverEnterEvent(event);
}

void TopologyNodeItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event) {
    setPen(QPen(pen().color(), m_isHub ? 3 : 2));
    QGraphicsEllipseItem::hoverLeaveEvent(event);
}

// ─── TopologyPage ──────────────────────────────────────────────────────────────
TopologyPage::TopologyPage(core::NetworkManager *nm, QWidget *parent)
    : QWidget(parent), m_nm(nm)
{
    setupUi();
    applyTheme();

    if (m_nm) {
        connect(m_nm, &core::NetworkManager::devicesUpdated, this, &TopologyPage::onDevicesUpdated);
        connect(m_nm, &core::NetworkManager::routerInfoReady, this, &TopologyPage::onRouterInfoReady);
    }
}

QColor TopologyPage::colorForStatus(const QString &status) {
    QString s = status.toLower();
    if (s.contains("blocked"))  return QColor("#ff5c5c");
    if (s.contains("online"))   return QColor("#34e4a0");
    if (s.contains("idle"))     return QColor("#f5a623");
    return QColor("#4d5666"); // offline / unknown
}

QPixmap TopologyPage::iconForDeviceType(const QString &deviceType, const QColor &color) {
    QString t = deviceType.toLower();
    QString path = ":/resources/monitor.svg";
    if (t.contains("router"))                              path = ":/resources/router.svg";
    else if (t.contains("android") || t.contains("apple") || t.contains("phone"))
                                                            path = ":/resources/wifi.svg";
    return Theme::tintedSvgPixmap(path, 18, color);
}

void TopologyPage::setupUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Header ───────────────────────────────────────────────────────────────
    QWidget *headerBar = new QWidget(this);
    headerBar->setObjectName("HeaderBar");
    QHBoxLayout *headerLayout = new QHBoxLayout(headerBar);
    headerLayout->setContentsMargins(24, 20, 24, 20);

    QVBoxLayout *titleLayout = new QVBoxLayout();
    titleLayout->setSpacing(2);
    QLabel *eyebrow = new QLabel("NETWORK MAP · LIVE", this);
    eyebrow->setStyleSheet("color: #7c8798; font-family: 'JetBrains Mono', monospace; font-size: 10px; font-weight: bold; letter-spacing: 0.15em;");
    QLabel *title = new QLabel("Topology", this);
    title->setStyleSheet("color: #dbe4ee; font-size: 22px; font-weight: bold; font-family: 'Inter', sans-serif;");
    titleLayout->addWidget(eyebrow);
    titleLayout->addWidget(title);
    headerLayout->addLayout(titleLayout);
    headerLayout->addStretch();

    QPushButton *zoomOutBtn = new QPushButton("−", this);
    QPushButton *zoomInBtn  = new QPushButton("+", this);
    QPushButton *resetBtn   = new QPushButton("Reset View", this);
    for (auto *b : {zoomOutBtn, zoomInBtn}) {
        b->setObjectName("ZoomBtn");
        b->setFixedSize(30, 30);
        b->setCursor(Qt::PointingHandCursor);
    }
    resetBtn->setObjectName("ResetBtn");
    resetBtn->setFixedHeight(30);
    resetBtn->setCursor(Qt::PointingHandCursor);
    connect(zoomInBtn,  &QPushButton::clicked, this, &TopologyPage::onZoomIn);
    connect(zoomOutBtn, &QPushButton::clicked, this, &TopologyPage::onZoomOut);
    connect(resetBtn,   &QPushButton::clicked, this, &TopologyPage::onResetView);
    headerLayout->addWidget(zoomOutBtn);
    headerLayout->addWidget(zoomInBtn);
    headerLayout->addWidget(resetBtn);

    root->addWidget(headerBar);

    // ── Stat strip ───────────────────────────────────────────────────────────
    QWidget *statStrip = new QWidget(this);
    QHBoxLayout *statLayout = new QHBoxLayout(statStrip);
    statLayout->setContentsMargins(24, 16, 24, 12);
    statLayout->setSpacing(16);

    auto makeStat = [&](const QString &label, const QString &color, QLabel **valPtr) {
        QWidget *card = new QWidget(this);
        card->setObjectName("StatCard");
        auto *l = new QVBoxLayout(card);
        l->setContentsMargins(16, 16, 16, 16);
        l->setSpacing(4);
        auto *lbl = new QLabel(label, this);
        lbl->setStyleSheet(QString("font-family: 'JetBrains Mono', monospace; font-size: 10px; font-weight: bold; color: %1; letter-spacing: 0.1em;").arg(color));
        *valPtr = new QLabel("0", this);
        (*valPtr)->setStyleSheet("font-family: 'JetBrains Mono', monospace; font-size: 24px; font-weight: 300; color: #dbe4ee;");
        l->addWidget(lbl);
        l->addWidget(*valPtr);
        return card;
    };
    statLayout->addWidget(makeStat("TOTAL NODES", "#5eead4", &m_statNodes));
    statLayout->addWidget(makeStat("ONLINE", "#34e4a0", &m_statOnline));
    statLayout->addWidget(makeStat("BLOCKED", "#ff5c5c", &m_statBlocked));
    statLayout->addStretch(2);
    root->addWidget(statStrip);

    // ── Canvas ───────────────────────────────────────────────────────────────
    m_scene = new QGraphicsScene(this);
    m_view = new QGraphicsView(m_scene, this);
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QVBoxLayout *canvasWrap = new QVBoxLayout();
    canvasWrap->setContentsMargins(24, 0, 24, 16);
    canvasWrap->addWidget(m_view);
    root->addLayout(canvasWrap, 1);

    // Hub node, created once
    m_hubNode = new TopologyNodeItem(true);
    m_hubNode->setIcon(Theme::tintedSvgPixmap(":/resources/router.svg", 24, Theme::AccentOrange));
    m_hubNode->setLabel(m_gatewayLabel);
    m_hubNode->setStatusColor(QColor("#ff9142"));
    m_hubNode->setPos(0, 0);
    m_scene->addItem(m_hubNode);

    // ── Footer ───────────────────────────────────────────────────────────────
    QWidget *footerBar = new QWidget(this);
    footerBar->setObjectName("FooterBar");
    QHBoxLayout *footerLayout = new QHBoxLayout(footerBar);
    footerLayout->setContentsMargins(24, 12, 24, 12);
    m_footerLabel = new QLabel("Scroll to zoom, drag to pan", this);
    m_footerLabel->setStyleSheet("color: #7c8798; font-size: 11px; font-family: 'Inter', sans-serif;");
    footerLayout->addWidget(m_footerLabel);
    footerLayout->addStretch();
    root->addWidget(footerBar);
}

void TopologyPage::onRouterInfoReady(const core::RouterInfo &info) {
    QString label = !info.friendlyName.isEmpty() ? info.friendlyName
                    : !info.model.isEmpty()       ? info.model
                    : "Gateway";
    m_gatewayLabel = label;
    if (m_hubNode) m_hubNode->setLabel(label);
}

void TopologyPage::onDevicesUpdated(const QList<core::Device> &devices) {
    QSet<QString> seenIps;
    int online = 0, blocked = 0;

    for (const auto &dev : devices) {
        if (dev.ip().isEmpty()) continue;
        seenIps.insert(dev.ip());

        QString status = dev.status();
        if (status.toLower().contains("online")) online++;
        if (status.toLower().contains("blocked")) blocked++;

        TopologyNodeItem *node = m_deviceNodes.value(dev.ip(), nullptr);
        if (!node) {
            node = new TopologyNodeItem(false);
            m_scene->addItem(node);
            node->setPos(0, 0); // spawn at hub, animate out on relayout
            m_deviceNodes.insert(dev.ip(), node);

            auto *line = new QGraphicsLineItem();
            line->setZValue(-1);
            m_scene->addItem(line);
            m_spokeLines.insert(dev.ip(), line);
        }
        node->deviceIp = dev.ip();
        QColor statusColor = colorForStatus(status);
        node->setIcon(iconForDeviceType(dev.deviceType(), statusColor));
        node->setStatusColor(statusColor);
        QString label = dev.alias().isEmpty()
                         ? (dev.hostname().isEmpty() || dev.hostname() == "Unknown" ? dev.ip() : dev.hostname())
                         : dev.alias();
        node->setLabel(label);
        node->setToolTip(QString("%1\nIP: %2\nMAC: %3\nVendor: %4\nStatus: %5")
                          .arg(label, dev.ip(), dev.mac(), dev.vendor(), status));
    }

    // Remove nodes for devices that disappeared
    QStringList toRemove;
    for (auto it = m_deviceNodes.constBegin(); it != m_deviceNodes.constEnd(); ++it) {
        if (!seenIps.contains(it.key())) toRemove << it.key();
    }
    for (const QString &ip : toRemove) {
        delete m_deviceNodes.take(ip);
        delete m_spokeLines.take(ip);
    }

    m_statNodes->setText(QString::number(devices.size()));
    m_statOnline->setText(QString::number(online));
    m_statBlocked->setText(QString::number(blocked));

    relayout();
}

void TopologyPage::relayout() {
    int n = m_deviceNodes.size();
    if (n == 0) return;

    qreal radius = qMax(140.0, 40.0 + n * 18.0);
    int i = 0;
    for (auto it = m_deviceNodes.begin(); it != m_deviceNodes.end(); ++it, ++i) {
        qreal angle = (2 * M_PI * i) / n;
        QPointF target(radius * qCos(angle), radius * qSin(angle));
        it.value()->animateTo(target);

        if (auto *line = m_spokeLines.value(it.key(), nullptr)) {
            line->setPen(QPen(QColor(255, 255, 255, 25), 1.2));
            line->setLine(QLineF(QPointF(0, 0), target));
        }
    }

    m_scene->setSceneRect(m_scene->itemsBoundingRect().adjusted(-60, -60, 60, 60));
}

void TopologyPage::onZoomIn()  { m_view->scale(1.2, 1.2); }
void TopologyPage::onZoomOut() { m_view->scale(1 / 1.2, 1 / 1.2); }
void TopologyPage::onResetView() {
    m_view->resetTransform();
    m_view->centerOn(0, 0);
}

void TopologyPage::applyTheme() {
    setStyleSheet(
        "gui--TopologyPage { background-color: #0a0d12; }"
        "QWidget#HeaderBar { background-color: transparent; border-bottom: 1px solid #1c232c; }"
        "QWidget#FooterBar { background-color: #0f141b; border-top: 1px solid #1c232c; }"
        "QWidget#StatCard { background-color: #0f141b; border: 1px solid #1c232c; border-radius: 8px; }"
        "QGraphicsView { background-color: #0a0d12; border: 1px solid #1c232c; border-radius: 10px; }"
        "QPushButton#ZoomBtn { background: #0f141b; border: 1px solid #1c232c; color: #dbe4ee; "
        "font-size: 16px; font-weight: 600; border-radius: 15px; }"
        "QPushButton#ZoomBtn:hover { border: 1px solid rgba(94,234,212,0.4); }"
        "QPushButton#ResetBtn { background: #0f141b; border: 1px solid #1c232c; color: #dbe4ee; "
        "font-size: 12px; font-weight: 600; border-radius: 7px; padding: 0 14px; }"
        "QPushButton#ResetBtn:hover { border: 1px solid rgba(94,234,212,0.4); }"
    );
}

} // namespace gui
