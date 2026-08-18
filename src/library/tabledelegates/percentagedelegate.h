#pragma once

#include "library/tabledelegates/tableitemdelegate.h"

class PercentageDelegate final : public TableItemDelegate {
  public:
    explicit PercentageDelegate(QTableView* pTableView)
            : TableItemDelegate(pTableView) {
    }

    void paintItem(
            QPainter* painter,
            const QStyleOptionViewItem& option,
            const QModelIndex& index) const override;
};
