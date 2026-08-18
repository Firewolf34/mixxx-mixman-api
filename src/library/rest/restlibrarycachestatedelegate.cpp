#include "library/rest/restlibrarycachestatedelegate.h"

#include <algorithm>

#include <QPainter>
#include <QPainterPath>
#include <QStyle>

#include "library/rest/restlibrarytrack.h"

namespace mixxx::library::rest {

void RestLibraryCacheStateDelegate::paintItem(
        QPainter* painter,
        const QStyleOptionViewItem& option,
        const QModelIndex& index) const {
    paintItemBackground(painter, option, index);

    const int availableSize = std::min(option.rect.width(), option.rect.height()) - 6;
    if (availableSize > 0) {
        const qreal markerSize = std::min(14, availableSize);
        QRectF markerRect(0, 0, markerSize, markerSize);
        markerRect.moveCenter(option.rect.center());

        const QColor markerColor = option.state & QStyle::State_Selected
                ? option.palette.highlightedText().color()
                : option.palette.text().color();
        QPen markerPen(markerColor, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter->setPen(markerPen);
        painter->setBrush(Qt::NoBrush);
        painter->setRenderHint(QPainter::Antialiasing, true);

        const auto state = static_cast<RestLibraryCacheState>(
                index.data(Qt::EditRole).toInt());
        switch (state) {
        case RestLibraryCacheState::Missing:
            painter->drawEllipse(markerRect.adjusted(2, 2, -2, -2));
            break;
        case RestLibraryCacheState::Downloading: {
            const qreal centerX = markerRect.center().x();
            const qreal arrowTipY = markerRect.bottom() - 3;
            painter->drawLine(
                    QPointF(centerX, markerRect.top() + 1),
                    QPointF(centerX, arrowTipY));
            painter->drawLine(
                    QPointF(markerRect.left() + 3, arrowTipY - 3),
                    QPointF(centerX, arrowTipY));
            painter->drawLine(
                    QPointF(markerRect.right() - 3, arrowTipY - 3),
                    QPointF(centerX, arrowTipY));
            painter->drawLine(
                    QPointF(markerRect.left() + 2, markerRect.bottom() - 1),
                    QPointF(markerRect.right() - 2, markerRect.bottom() - 1));
            break;
        }
        case RestLibraryCacheState::Ready: {
            QPainterPath checkPath;
            checkPath.moveTo(markerRect.left() + 1, markerRect.center().y());
            checkPath.lineTo(
                    markerRect.left() + markerRect.width() * 0.42,
                    markerRect.bottom() - 2);
            checkPath.lineTo(markerRect.right() - 1, markerRect.top() + 2);
            painter->drawPath(checkPath);
            break;
        }
        case RestLibraryCacheState::Failed:
            painter->drawLine(
                    markerRect.topLeft() + QPointF(2, 2),
                    markerRect.bottomRight() - QPointF(2, 2));
            painter->drawLine(
                    markerRect.topRight() + QPointF(-2, 2),
                    markerRect.bottomLeft() + QPointF(2, -2));
            break;
        case RestLibraryCacheState::Stale:
            painter->drawEllipse(markerRect.adjusted(1, 1, -1, -1));
            painter->drawLine(
                    markerRect.center(),
                    QPointF(markerRect.center().x(), markerRect.top() + 3));
            painter->drawLine(
                    markerRect.center(),
                    QPointF(markerRect.right() - 3, markerRect.center().y() + 2));
            break;
        }
    }

    if (option.state & QStyle::State_HasFocus) {
        drawBorder(painter, m_focusBorderColor, option.rect);
    }
}

} // namespace mixxx::library::rest
