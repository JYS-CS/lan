#pragma once

#include <QStyledItemDelegate>
#include <QFont>
#include <QSvgRenderer>
#include "Theme.h"

namespace gui {

class DeviceTableDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit DeviceTableDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    void setGatewayModeActive(bool active) { m_gatewayModeActive = active; }

private:
    QFont m_monoFont;
    QFont m_boldMonoFont;
    QFont m_standardFont;

    QSvgRenderer *m_svgOnline  = nullptr;  // green wifi
    QSvgRenderer *m_svgOffline = nullptr;  // red wifi + slash

    bool m_gatewayModeActive = false;  // only paint Block button when gateway is active

    void loadFonts();
};

} // namespace gui
