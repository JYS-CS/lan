#include "DeviceTableDelegate.h"
#include "DeviceTableModel.h"
#include "AppSettings.h"
#include <QPainter>
#include <QPainterPath>
#include <QFontDatabase>
#include <QFontMetrics>

namespace gui {

DeviceTableDelegate::DeviceTableDelegate(QObject *parent) : QStyledItemDelegate(parent) {
    loadFonts();
    m_svgOnline  = new QSvgRenderer(QString(":/resources/wifi_online.svg"),  this);
    m_svgOffline = new QSvgRenderer(QString(":/resources/wifi_offline.svg"), this);
}

void DeviceTableDelegate::loadFonts() {
    QFontDatabase::addApplicationFont(":/resources/JetBrainsMono-Regular.ttf");
    QFontDatabase::addApplicationFont(":/resources/JetBrainsMono-Bold.ttf");
    QFontDatabase::addApplicationFont(":/resources/Inter-Regular.ttf");
    QFontDatabase::addApplicationFont(":/resources/Inter-Bold.ttf");
    
    m_monoFont = QFont("JetBrains Mono", 11);
    m_monoFont.setStyleHint(QFont::Monospace);
    
    m_boldMonoFont = m_monoFont;
    m_boldMonoFont.setBold(true);
    
    m_standardFont = QFont("Inter", 11);
    m_standardFont.setStyleHint(QFont::SansSerif);
}

QSize DeviceTableDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    size.setHeight(64);
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
            bool isUp = (index.column() == DeviceTableModel::ColUp);
            QColor barColor = isUp ? QColor(79, 127, 255) : QColor(52, 228, 160);

            if (AppSettings::instance()->showSparklines()) {
                // Fetch spike history
                QList<qreal> history = (isUp
                    ? index.data(DeviceTableModel::UpHistoryRole)
                    : index.data(DeviceTableModel::DownHistoryRole))
                    .value<QList<qreal>>();

                if (history.isEmpty()) {
                    qreal raw = index.data(DeviceTableModel::RawDataRole).toReal();
                    history.append(raw);
                }

                qreal localMax = 1.0;
                for (qreal v : history) localMax = qMax(localMax, v);

                int n = history.size();
                int totalW = contentRect.width() - 4;
                int barW = qMax(2, (totalW / n) - 2);
                int gap  = qMax(1, (totalW - n * barW) / qMax(1, n - 1));
                int maxH = contentRect.height() - 12;
                int baseY = contentRect.bottom() - 6;

                for (int i = 0; i < n; ++i) {
                    qreal ratio = history[i] / localMax;
                    int h = qMax(2, (int)(maxH * ratio));
                    int x = contentRect.left() + 2 + i * (barW + gap);
                    int y = baseY - h;

                    QLinearGradient grad(x, y, x, baseY);
                    QColor top = barColor; top.setAlpha(220);
                    QColor bot = barColor; bot.setAlpha(60);
                    grad.setColorAt(0.0, top);
                    grad.setColorAt(1.0, bot);

                    QPainterPath bar;
                    bar.addRoundedRect(QRectF(x, y, barW, h), 1.5, 1.5);
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(grad);
                    painter->drawPath(bar);
                }
            } else {
                painter->setFont(m_monoFont);
                painter->setPen(barColor);
                painter->drawText(contentRect, Qt::AlignLeft | Qt::AlignVCenter, text);
            }
            break;
        }
        case DeviceTableModel::ColStatus: {
            QString s = text.toLower();
            bool isOnline = s.contains("self") || s.contains("online")
                         || s.contains("gateway") || s.contains("idle");

            QSvgRenderer *renderer = isOnline ? m_svgOnline : m_svgOffline;

            // Centre the icon in the cell; keep it square at ~26×26 px
            constexpr int iconSize = 26;
            int cx = contentRect.left() + iconSize / 2 + 4;
            int cy = contentRect.center().y();
            QRect iconRect(cx - iconSize / 2, cy - iconSize / 2, iconSize, iconSize);

            if (renderer && renderer->isValid()) {
                renderer->render(painter, iconRect);
            }

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
        case DeviceTableModel::ColBlock: {
            // Never show the block button for the host device
            bool isHost = index.data(DeviceTableModel::IsHostRole).toBool();
            if (isHost) break;

            // Only show the button when this machine is acting as the gateway
            if (!m_gatewayModeActive) {
                // Draw a dim dash to signal the column is inactive
                QFont dimFont = m_standardFont;
                dimFont.setPointSize(10);
                painter->setFont(dimFont);
                painter->setPen(QColor(60, 70, 85));
                painter->drawText(contentRect, Qt::AlignCenter, "—");
                break;
            }

            // Draw a pill-shaped "Block" button centred in the cell
            constexpr int btnW = 62;
            constexpr int btnH = 24;
            int cx = contentRect.center().x();
            int cy = contentRect.center().y();
            QRect btnRect(cx - btnW / 2, cy - btnH / 2, btnW, btnH);

            // Hover highlight
            bool hovered = (option.state & QStyle::State_MouseOver);
            QColor bg  = hovered ? QColor(255, 92, 92, 45)  : QColor(255, 92, 92, 20);
            QColor bdr = hovered ? QColor(255, 92, 92, 180) : QColor(255, 92, 92, 80);
            QColor txt = QColor(255, 110, 110);

            QPainterPath pill;
            pill.addRoundedRect(btnRect, 6, 6);
            painter->setPen(Qt::NoPen);
            painter->setBrush(bg);
            painter->drawPath(pill);

            painter->setPen(bdr);
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(pill);

            QFont btnFont = m_standardFont;
            btnFont.setPointSize(9);
            btnFont.setBold(true);
            painter->setFont(btnFont);
            painter->setPen(txt);
            painter->drawText(btnRect, Qt::AlignCenter, "Block");
            break;
        }
        }


    painter->restore();
}

} // namespace gui
