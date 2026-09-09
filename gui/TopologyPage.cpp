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
    QString display = text;
    if (display.length() > 18) {
        display = display.left(16) + "…";
    }
    m_labelItem->setPlainText(display);
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
        connect(m_nm, &core::NetworkManager::dhcpStatusUpdate, this, &TopologyPage::onDhcpStatusUpdate);
        connect(m_nm, &core::NetworkManager::bandwidthUpdated, this, &TopologyPage::onBandwidthUpdated);
        m_dhcpRunning = m_nm->isGatewayModeActive();
    }
}

void TopologyPage::onDhcpStatusUpdate(bool running) {
    m_dhcpRunning = running;
    if (m_nm) {
        onDevicesUpdated(m_nm->getDevices());
    }
}

QColor TopologyPage::colorForStatus(const QString &status) {
    QString s = status.toLower();
    if (s.contains("blocked"))  return QColor("#ff5c5c");
    if (s.contains("online"))   return QColor("#34e4a0");
    if (s.contains("idle"))     return QColor("#f5a623");
    return QColor("#7c8798"); // offline / unknown
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

    headerLayout->addStretch();
    QLabel *title = new QLabel("NETWORK MAP · LIVE", this);
    title->setStyleSheet("color: #dbe4ee; font-family: 'Inter', sans-serif; font-size: 16px; font-weight: bold; letter-spacing: 0.1em;");
    headerLayout->addWidget(title);
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
    m_layoutBtn = new QPushButton(QString::fromUtf8("\xe2\x87\x86"), this); // ⇆ character
    m_layoutBtn->setObjectName("LayoutBtn");
    m_layoutBtn->setFixedSize(30, 30);
    m_layoutBtn->setCursor(Qt::PointingHandCursor);
    m_layoutBtn->setToolTip("Switch Layout (Current: Radial)");
    connect(m_layoutBtn, &QPushButton::clicked, this, &TopologyPage::onLayoutBtnClicked);
    
    headerLayout->addWidget(m_layoutBtn);
    headerLayout->addWidget(zoomOutBtn);
    headerLayout->addWidget(zoomInBtn);
    headerLayout->addWidget(resetBtn);

    root->addWidget(headerBar);

    // ── Canvas ───────────────────────────────────────────────────────────────
    m_scene = new QGraphicsScene(this);
    m_view = new QGraphicsView(m_scene, this);
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->viewport()->installEventFilter(this);

    QGridLayout *canvasWrap = new QGridLayout();
    canvasWrap->setContentsMargins(24, 0, 24, 16);
    canvasWrap->addWidget(m_view, 0, 0);

    // Overlay Stats
    QWidget *overlay = new QWidget(this);
    overlay->setObjectName("StatCard");
    overlay->setStyleSheet("QWidget#StatCard { background-color: rgba(15, 20, 27, 0.85); border: 1px solid #1c232c; border-radius: 8px; }");
    QHBoxLayout *overlayLayout = new QHBoxLayout(overlay);
    overlayLayout->setContentsMargins(16, 8, 16, 8);
    overlayLayout->setSpacing(20);

    auto makeSmallStat = [&](const QString &label, const QString &color, QLabel **valPtr) {
        QWidget *w = new QWidget(this);
        QHBoxLayout *l = new QHBoxLayout(w);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(8);
        QLabel *lbl = new QLabel(label, this);
        lbl->setStyleSheet(QString("font-family: 'Inter', sans-serif; font-size: 11px; font-weight: bold; color: %1;").arg(color));
        *valPtr = new QLabel("0", this);
        (*valPtr)->setStyleSheet("font-family: 'JetBrains Mono', monospace; font-size: 14px; font-weight: bold; color: #dbe4ee;");
        l->addWidget(lbl);
        l->addWidget(*valPtr);
        return w;
    };
    
    overlayLayout->addWidget(makeSmallStat("NODES", "#5eead4", &m_statNodes));
    overlayLayout->addWidget(makeSmallStat("ONLINE", "#34e4a0", &m_statOnline));
    overlayLayout->addWidget(makeSmallStat("OFFLINE", "#7c8798", &m_statOffline));
    overlayLayout->addWidget(makeSmallStat("BLOCKED", "#ff5c5c", &m_statBlocked));

    // Align to top-right with some padding
    canvasWrap->addWidget(overlay, 0, 0, Qt::AlignTop | Qt::AlignRight);

    // To add margin for the overlay inside the grid cell, we can wrap it or just rely on layout margins.
    // We'll wrap it in another widget to apply margins from the edge.
    QWidget *overlayContainer = new QWidget(this);
    QVBoxLayout *containerLayout = new QVBoxLayout(overlayContainer);
    containerLayout->setContentsMargins(0, 16, 16, 0);
    containerLayout->addWidget(overlay);
    canvasWrap->addWidget(overlayContainer, 0, 0, Qt::AlignTop | Qt::AlignRight);

    root->addLayout(canvasWrap, 1);

    // Hub node, created once
    m_hubNode = new TopologyNodeItem(true);
    m_hubNode->setIcon(Theme::tintedSvgPixmap(":/resources/router.svg", 24, Theme::AccentOrange));
    m_hubNode->setLabel(m_gatewayLabel);
    m_hubNode->setStatusColor(QColor("#ff9142"));
    m_hubNode->setPos(0, 0);
    m_scene->addItem(m_hubNode);
    
    m_view->centerOn(0, 0);

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
    int online = 0, offline = 0, blocked = 0;

    QString hubIp = m_dhcpRunning ? m_nm->myIp() : m_nm->gatewayIp();

    if (m_dhcpRunning) {
        m_hubNode->setLabel("This PC (DHCP)");
        m_hubNode->setIcon(Theme::tintedSvgPixmap(":/resources/router.svg", 24, Theme::AccentOrange));
    } else {
        m_hubNode->setLabel(m_gatewayLabel);
        m_hubNode->setIcon(Theme::tintedSvgPixmap(":/resources/router.svg", 24, Theme::AccentOrange));
    }

    for (const auto &dev : devices) {
        if (dev.ip().isEmpty()) continue;

        QString status = dev.status();
        if (status.toLower().contains("online")) online++;
        else if (status.toLower().contains("blocked")) blocked++;
        else offline++;

        if (dev.ip() == hubIp) {
            QColor statusColor = colorForStatus(status);
            m_hubNode->setStatusColor(statusColor);
            m_hubNode->setToolTip(QString("%1\nIP: %2\nMAC: %3\nVendor: %4\nStatus: %5")
                              .arg(m_dhcpRunning ? "This PC (DHCP)" : m_gatewayLabel, dev.ip(), dev.mac(), dev.vendor(), status));
            
            if (TopologyNodeItem *oldNode = m_deviceNodes.take(hubIp)) {
                m_scene->removeItem(oldNode);
                delete oldNode;
            }
            if (QGraphicsLineItem *oldLine = m_spokeLines.take(hubIp)) {
                m_scene->removeItem(oldLine);
                delete oldLine;
            }
            continue;
        }

        seenIps.insert(dev.ip());

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
    m_statOffline->setText(QString::number(offline));
    m_statBlocked->setText(QString::number(blocked));

    relayout();
}

void TopologyPage::relayout() {
    int n = m_deviceNodes.size();
    if (n == 0) return;

    if (m_layoutMode == LayoutMode::Radial) {
        m_hubNode->animateTo(QPointF(0, 0));
        qreal radius = qMax(160.0, 60.0 + n * 24.0);
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
    } else {
        if (m_dhcpRunning) {
            QPointF hubTarget(0, 0);
            m_hubNode->animateTo(hubTarget);
            
            QString gatewayIp = m_nm ? m_nm->gatewayIp() : "";
            int otherNodes = 0;
            for (auto it = m_deviceNodes.begin(); it != m_deviceNodes.end(); ++it) {
                if (it.key() != gatewayIp) otherNodes++;
            }
            
            int otherIdx = 0;
            qreal spacing = 160.0;
            qreal startX = -((otherNodes - 1) * spacing) / 2.0;
            
            for (auto it = m_deviceNodes.begin(); it != m_deviceNodes.end(); ++it) {
                QPointF target;
                if (it.key() == gatewayIp) {
                    target = QPointF(0, -150); // Router is above
                } else {
                    target = QPointF(startX + (otherIdx++) * spacing, 150);
                }
                it.value()->animateTo(target);
                
                if (auto *line = m_spokeLines.value(it.key(), nullptr)) {
                    line->setPen(QPen(QColor(255, 255, 255, 25), 1.2));
                    line->setLine(QLineF(hubTarget, target));
                }
            }
        } else {
            QPointF hubTarget(0, -100);
            m_hubNode->animateTo(hubTarget);
            
            qreal spacing = 160.0;
            qreal startX = -((n - 1) * spacing) / 2.0;
            int i = 0;
            
            for (auto it = m_deviceNodes.begin(); it != m_deviceNodes.end(); ++it, ++i) {
                QPointF target(startX + i * spacing, 100);
                it.value()->animateTo(target);
                
                if (auto *line = m_spokeLines.value(it.key(), nullptr)) {
                    line->setPen(QPen(QColor(255, 255, 255, 25), 1.2));
                    line->setLine(QLineF(hubTarget, target));
                }
            }
        }
    }

    m_scene->setSceneRect(-2000, -2000, 4000, 4000);
}

void TopologyPage::onBandwidthUpdated(const QList<core::DeviceBandwidth> &devices) {
    if (!m_nm || !m_scene || !m_hubNode) return;
    
    QString hubIp = m_dhcpRunning ? m_nm->myIp() : m_nm->gatewayIp();

    static int tickCount = 0;
    tickCount++;
    bool isIdleTick = (tickCount % 4 == 0); // Spawn idle dots every 4 seconds

    for (const auto &dev : devices) {
        if (dev.ip == hubIp) continue;
        
        TopologyNodeItem *node = m_deviceNodes.value(dev.ip, nullptr);
        if (!node) continue;
        
        QPointF start = m_hubNode->pos();
        QPointF end = node->pos();

        auto createDot = [&](bool upload, quint32 rate) {
            auto *dot = new QGraphicsEllipseItem(-3, -3, 6, 6);
            dot->setBrush(QBrush(upload ? QColor("#3b82f6") : QColor("#34e4a0")));
            dot->setPen(Qt::NoPen);
            dot->setZValue(2);
            m_scene->addItem(dot);
            
            auto *anim = new QVariantAnimation(this);
            
            int duration = 3000; // constant speed for idle
            if (rate > 0) {
                // Scale duration based on rate (max speed at 5 MB/s)
                double speedFactor = qMin(1.0, (double)rate / 5000000.0);
                duration = 2000 - (1700 * speedFactor); // ranges from 2000 down to 300
            }
            
            anim->setDuration(duration); 
            if (upload) {
                anim->setStartValue(end);
                anim->setEndValue(start);
            } else {
                anim->setStartValue(start);
                anim->setEndValue(end);
            }
            connect(anim, &QVariantAnimation::valueChanged, [dot](const QVariant &value) {
                dot->setPos(value.toPointF());
            });
            connect(anim, &QVariantAnimation::finished, [dot, anim, this]() {
                m_scene->removeItem(dot);
                delete dot;
                anim->deleteLater();
            });
            anim->start();
        };

        if (dev.txRate > 0) {
            createDot(true, dev.txRate);
        } else if (isIdleTick) {
            createDot(true, 0); // occasional constant idle speed dot
        }

        if (dev.rxRate > 0) {
            createDot(false, dev.rxRate);
        } else if (isIdleTick) {
            createDot(false, 0); // occasional constant idle speed dot
        }
    }
}

void TopologyPage::onZoomIn()  { m_view->scale(1.2, 1.2); }
void TopologyPage::onZoomOut() { m_view->scale(1 / 1.2, 1 / 1.2); }
void TopologyPage::onResetView() {
    m_view->resetTransform();
    m_view->centerOn(0, 0);
}

void TopologyPage::onLayoutBtnClicked() {
    if (m_layoutMode == LayoutMode::Radial) {
        m_layoutMode = LayoutMode::Hierarchical;
        m_layoutBtn->setToolTip("Switch Layout (Current: Hierarchical)");
    } else {
        m_layoutMode = LayoutMode::Radial;
        m_layoutBtn->setToolTip("Switch Layout (Current: Radial)");
    }
    relayout();
}

void TopologyPage::applyTheme() {
    setStyleSheet(
        "gui--TopologyPage { background-color: #0a0d12; }"
        "QWidget#HeaderBar { background-color: transparent; border-bottom: 1px solid #1c232c; }"
        "QWidget#FooterBar { background-color: #0f141b; border-top: 1px solid #1c232c; }"
        "QWidget#StatCard { background-color: #0f141b; border: 1px solid #1c232c; border-radius: 8px; }"
        "QGraphicsView { background-color: #0a0d12; border: 1px solid #1c232c; border-radius: 10px; }"
        "QPushButton#ZoomBtn { background: #0f141b; border: 1px solid #1c232c; color: #dbe4ee; "
        "font-size: 16px; font-weight: 600; border-radius: 15px; padding: 0px; }"
        "QPushButton#ZoomBtn:hover { border: 1px solid rgba(94,234,212,0.4); }"
        "QPushButton#LayoutBtn { background: #0f141b; border: 1px solid #1c232c; color: #dbe4ee; "
        "font-size: 18px; font-weight: bold; border-radius: 15px; padding: 0px; margin-bottom: 2px; }"
        "QPushButton#LayoutBtn:hover { border: 1px solid rgba(94,234,212,0.4); color: #5eead4; }"
        "QPushButton#ResetBtn { background: #0f141b; border: 1px solid #1c232c; color: #dbe4ee; "
        "font-size: 12px; font-weight: 600; border-radius: 7px; padding: 0 14px; }"
        "QPushButton#ResetBtn:hover { border: 1px solid rgba(94,234,212,0.4); }"
    );
}

bool TopologyPage::eventFilter(QObject *obj, QEvent *event) {
    if (obj == m_view->viewport() && event->type() == QEvent::Wheel) {
        auto *wheel = static_cast<QWheelEvent*>(event);
        if (wheel->angleDelta().y() > 0) {
            onZoomIn();
        } else {
            onZoomOut();
        }
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

void TopologyPage::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    static bool firstShow = true;
    if (firstShow) {
        m_view->centerOn(0, 0);
        firstShow = false;
    }
}

} // namespace gui
