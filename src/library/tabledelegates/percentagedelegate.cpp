#include "library/tabledelegates/percentagedelegate.h"

#include <algorithm>

#include <QPainter>
#include <QStyle>
#include <QTableView>

void PercentageDelegate::paintItem(
        QPainter* painter,
        const QStyleOptionViewItem& option,
        const QModelIndex& index) const {
    paintItemBackground(painter, option, index);

    constexpr int kMeterWidth = 4;
    constexpr int kHorizontalPadding = 2;
    constexpr int kVerticalPadding = 2;
    constexpr int kTextGap = 4;

    const QVariant rawValue = index.data(Qt::EditRole);
    if (rawValue.isValid() && !rawValue.isNull()) {
        const double normalized = std::clamp(rawValue.toDouble(), 0.0, 1.0);
        const QRect meterRect(
                option.rect.x() + kHorizontalPadding,
                option.rect.y() + kVerticalPadding,
                kMeterWidth,
                std::max(0, option.rect.height() - 2 * kVerticalPadding));

        QColor meterColor = (option.state & QStyle::State_Selected)
                ? option.palette.highlightedText().color()
                : option.palette.highlight().color();
        QColor railColor = option.palette.text().color();
        railColor.setAlphaF(0.2);
        painter->fillRect(meterRect, railColor);

        const int fillHeight = qRound(meterRect.height() * normalized);
        if (fillHeight > 0) {
            painter->fillRect(
                    meterRect.x(),
                    meterRect.bottom() - fillHeight + 1,
                    meterRect.width(),
                    fillHeight,
                    meterColor);
        }
    }

    QStyleOptionViewItem opt = option;
    setTextColor(opt, index);
    painter->setPen((opt.state & QStyle::State_Selected)
                    ? opt.palette.highlightedText().color()
                    : opt.palette.text().color());
    const int textLeft = option.rect.x() + kHorizontalPadding + kMeterWidth + kTextGap;
    painter->drawText(
            textLeft,
            option.rect.y(),
            std::max(0, option.rect.right() - textLeft - kHorizontalPadding + 1),
            option.rect.height(),
            Qt::AlignVCenter | Qt::AlignRight,
            index.data(Qt::DisplayRole).toString());

    if (option.state & QStyle::State_HasFocus) {
        drawBorder(painter, m_focusBorderColor, option.rect);
    }
}
