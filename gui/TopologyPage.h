#pragma once

#include <QWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsEllipseItem>
#include <QGraphicsTextItem>
#include <QGraphicsLineItem>
#include <QLabel>
#include <QPushButton>
#include <QHash>
#include <QPropertyAnimation>
#include "../core/NetworkManager.h"

namespace gui {

// A single device (or the gateway hub) rendered on the topology canvas.
// Owns its circle, icon, and label as child items so they move together.
class TopologyNodeItem : public QObject, public QGraphicsEllipseItem {
    Q_OBJECT
    Q_PROPERTY(QPointF pos READ pos WRITE setPos)
public:
    explicit TopologyNodeItem(bool isHub, QGraphicsItem *parent = nullptr);

    void setIcon(const QPixmap &pixmap);
    void setLabel(const QString &text);
    void setStatusColor(const QColor &color);
    void animateTo(const QPointF &target);

    QString deviceIp; // empty for the hub

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;

private:
    QGraphicsPixmapItem *m_iconItem;
    QGraphicsTextItem   *m_labelItem;
    bool m_isHub;
    qreal m_radius;
};

// Radial network topology: the gateway sits at the center, every known
// device is placed around it as a spoke, colored by live status. Inspired
// by infrastructure-mapping tools like Homelable, adapted to this app's
// existing device data (NetworkManager) and ops-console visual language.
class TopologyPage : public QWidget {
    Q_OBJECT
public:
    explicit TopologyPage(core::NetworkManager *nm, QWidget *parent = nullptr);

private slots:
    void onDevicesUpdated(const QList<core::Device> &devices);
    void onRouterInfoReady(const core::RouterInfo &info);
    void onZoomIn();
    void onZoomOut();
    void onResetView();

private:
    void setupUi();
    void applyTheme();
    void relayout();
    QPixmap iconForDeviceType(const QString &deviceType, const QColor &color);
    QColor colorForStatus(const QString &status);

    core::NetworkManager *m_nm;

    QGraphicsScene *m_scene;
    QGraphicsView  *m_view;
    TopologyNodeItem *m_hubNode = nullptr;
    QHash<QString, TopologyNodeItem*> m_deviceNodes;   // keyed by IP
    QHash<QString, QGraphicsLineItem*> m_spokeLines;    // keyed by IP

    QLabel *m_statNodes;
    QLabel *m_statOnline;
    QLabel *m_statBlocked;
    QLabel *m_footerLabel;

    QString m_gatewayLabel = "Gateway";
};

} // namespace gui
