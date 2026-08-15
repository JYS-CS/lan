#include "DeviceTableDelegate.h"
#include "DeviceTableModel.h"
#include <QPainter>
#include <QPainterPath>
#include <QFontDatabase>
#include <QFontMetrics>

namespace gui {

DeviceTableDelegate::DeviceTableDelegate(QObject *parent) : QStyledItemDelegate(parent) {
    loadFonts();
}

void DeviceTableDelegate::loadFonts() {
    QFontDatabase::addApplicationFont(":/resources/JetBrainsMono-Regular.ttf");
    QFontDatabase::addApplicationFont(":/resources/JetBrainsMono-Bold.ttf");
    QFontDatabase::addApplicationFont(":/resources/Inter-Regular.ttf");
    QFontDatabase::addApplicationFont(":/resources/Inter-Bold.ttf");
    
    m_monoFont = QFont("JetBrains Mono", 10);
    m_monoFont.setStyleHint(QFont::Monospace);
    
    m_boldMonoFont = m_monoFont;
    m_boldMonoFont.setBold(true);
    
    m_standardFont = QFont("Inter", 10);
    m_standardFont.setStyleHint(QFont::SansSerif);
}

QSize DeviceTableDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    size.setHeight(48);
    return size;
}

void DeviceTableDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    if (option.state & QStyle::State_Selected) {
        painter->fillRect(option.rect, QColor(79, 127, 255, 30));
    } else if (option.state & QStyle::State_MouseOver) {
        painter->fillRect(option.rect, Theme::OpsAltPanel);
    } else {
        painter->fillRect(option.rect, Qt::transparent);
    }
    
    painter->setPen(Theme::OpsBorderSoft);
    painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());

    QString text = index.data(Qt::DisplayRole).toString();
    QRect contentRect = option.rect.adjusted(12, 0, -12, 0);

    switch (index.column()) {
        case DeviceTableModel::ColIP: {
            painter->setFont(m_boldMonoFont);
            painter->setPen(Theme::AccentBlue);
            painter->drawText(contentRect, Qt::AlignLeft | Qt::AlignVCenter, text);
            break;
        }
        case DeviceTableModel::ColMAC: {
            painter->setFont(m_monoFont);
            painter->setPen(Theme::OpsTextDim);
            painter->drawText(contentRect, Qt::AlignLeft | Qt::AlignVCenter, text);
            break;
        }
        case DeviceTableModel::ColHostname: {
            painter->setFont(m_standardFont);
            painter->setPen(Theme::OpsTextPrimary);
            
            bool isHost = index.data(DeviceTableModel::IsHostRole).toBool();
            if (isHost) {
                QFontMetrics fm(m_standardFont);
                int textW = fm.horizontalAdvance(text);
                painter->drawText(contentRect, Qt::AlignLeft | Qt::AlignVCenter, text);
                
                QRect pillRect(contentRect.left() + textW + 8, contentRect.center().y() - 10, 60, 20);
                QPainterPath pillPath;
                pillPath.addRoundedRect(pillRect, 10, 10);
                painter->fillPath(pillPath, QColor(52, 228, 160, 30));
                
                painter->setPen(Theme::OpsAccentGreen);
                QFont pillFont = m_standardFont;
                pillFont.setPointSize(8);
                pillFont.setBold(true);
                painter->setFont(pillFont);
                painter->drawText(pillRect, Qt::AlignCenter, "THIS HOST");
            } else {
                painter->drawText(contentRect, Qt::AlignLeft | Qt::AlignVCenter, text);
            }
            break;
        }
        case DeviceTableModel::ColUp:
        case DeviceTableModel::ColDown: {
            qreal maxBw = index.data(DeviceTableModel::MaxBwRole).toReal();
            qreal rawBw = index.data(DeviceTableModel::RawDataRole).toReal();
            
            if (maxBw > 0 && rawBw > 0) {
                double ratio = rawBw / maxBw;
                int barWidth = (contentRect.width() - 10) * ratio;
                if (barWidth > 0) {
                    QRect barRect(contentRect.left(), contentRect.center().y() - 12, barWidth, 24);
                    QPainterPath barPath;
                    barPath.addRoundedRect(barRect, 4, 4);
                    QColor barColor = (index.column() == DeviceTableModel::ColUp) ? QColor(79, 127, 255, 30) : QColor(52, 228, 160, 30);
                    painter->fillPath(barPath, barColor);
                }
            }
            
            painter->setFont(m_monoFont);
            painter->setPen((index.column() == DeviceTableModel::ColUp) ? Theme::AccentBlue : Theme::OpsTextDim);
            painter->drawText(contentRect, Qt::AlignLeft | Qt::AlignVCenter, text);
            break;
        }
        case DeviceTableModel::ColStatus: {
            QString s = text.toLower();
            QColor accent;
            if (s.contains("online") || s.contains("self")) accent = Theme::OpsAccentGreen;
            else if (s.contains("idle") || s.contains("gateway")) accent = Theme::OpsAccentTeal;
            else accent = Theme::OpsTextFaint;

            QFont pillFont = m_standardFont;
            pillFont.setPointSize(9);
            pillFont.setBold(true);
            QFontMetrics fm(pillFont);
            
            int padding = 8;
            int dotSize = 6;
            int spacing = 6;
            int textW = fm.horizontalAdvance(text.toUpper());
            int pillWidth = padding + dotSize + spacing + textW + padding;
            
            QRect pillRect(contentRect.left(), contentRect.center().y() - 12, pillWidth, 24);
            QPainterPath pillPath;
            pillPath.addRoundedRect(pillRect, 12, 12);
            
            QColor bgColor = accent;
            bgColor.setAlpha(25);
            painter->fillPath(pillPath, bgColor);
            painter->setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 60), 0.5));
            painter->drawPath(pillPath);
            
            painter->setPen(Qt::NoPen);
            painter->setBrush(accent);
            painter->drawEllipse(pillRect.left() + padding, pillRect.center().y() - dotSize/2, dotSize, dotSize);
            
            painter->setPen(accent);
            painter->setFont(pillFont);
            QRect textRect(pillRect.left() + padding + dotSize + spacing, pillRect.top(), textW, pillRect.height());
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text.toUpper());
            
            break;
        }
        case DeviceTableModel::ColVendor: {
            bool isUnknown = text.toLower().contains("unknown");
            QColor accent = isUnknown ? Theme::OpsAccentAmber : Theme::OpsTextDim;
            
            int dotSize = 6;
            
            if (isUnknown) {
                QRadialGradient grad(contentRect.left() + dotSize/2, contentRect.center().y(), dotSize * 2);
                QColor center = accent;
                QColor edge = accent;
                edge.setAlpha(0);
                grad.setColorAt(0, center);
                grad.setColorAt(1, edge);
                painter->setPen(Qt::NoPen);
                painter->setBrush(grad);
                painter->drawEllipse(contentRect.left() - dotSize/2, contentRect.center().y() - dotSize, dotSize*3, dotSize*3);
            }
            
            painter->setPen(Qt::NoPen);
            painter->setBrush(accent);
            painter->drawEllipse(contentRect.left(), contentRect.center().y() - dotSize/2, dotSize, dotSize);
            
            painter->setPen(Theme::OpsTextDim);
            painter->setFont(m_standardFont);
            QRect textRect = contentRect.adjusted(dotSize + 8, 0, 0, 0);
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);
            break;
        }
    }

    painter->restore();
}

} // namespace gui
