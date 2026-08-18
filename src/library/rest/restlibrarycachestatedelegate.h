#pragma once

#include "library/tabledelegates/tableitemdelegate.h"

namespace mixxx::library::rest {

class RestLibraryCacheStateDelegate final : public TableItemDelegate {
  public:
    explicit RestLibraryCacheStateDelegate(QTableView* pTableView)
            : TableItemDelegate(pTableView) {
    }

    void paintItem(
            QPainter* painter,
            const QStyleOptionViewItem& option,
            const QModelIndex& index) const override;
};

} // namespace mixxx::library::rest
