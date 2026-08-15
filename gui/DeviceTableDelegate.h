#pragma once

#include <QStyledItemDelegate>
#include <QFont>
#include "Theme.h"

namespace gui {

class DeviceTableDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit DeviceTableDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    QFont m_monoFont;
    QFont m_boldMonoFont;
    QFont m_standardFont;
    
    void loadFonts();
};

} // namespace gui
