#include "library/rest/dlgrestlibrarybrowser.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "controllers/keyboard/keyboardeventfilter.h"
#include "library/library.h"
#include "library/rest/restlibrarytablemodel.h"
#include "moc_dlgrestlibrarybrowser.cpp"
#include "widget/wlibrary.h"
#include "widget/wtracktableview.h"
#include "widget/wtracktableviewheader.h"

namespace mixxx::library::rest {

DlgRestLibraryBrowser::DlgRestLibraryBrowser(
        WLibrary* parent,
        UserSettingsPointer pConfig,
        Library* pLibrary,
        RestLibraryTableModel* pTableModel,
        KeyboardEventFilter* pKeyboard)
        : QWidget(parent),
          m_pTrackTableView(new WTrackTableView(
                  this,
                  std::move(pConfig),
                  pLibrary,
                  parent->getTrackTableBackgroundColorOpacity())),
          m_pTableModel(pTableModel),
          m_pStatusLabel(new QLabel(tr("Open REST Library to load the catalog."), this)) {
    auto* pRefreshButton = new QPushButton(tr("Refresh"), this);
    auto* pToolbar = new QHBoxLayout();
    pToolbar->addWidget(pRefreshButton);
    pToolbar->addWidget(m_pStatusLabel, 1);

    auto* pLayout = new QVBoxLayout(this);
    pLayout->setContentsMargins(0, 0, 0, 0);
    pLayout->addLayout(pToolbar);
    pLayout->addWidget(m_pTrackTableView, 1);

    m_pTrackTableView->installEventFilter(pKeyboard);
    m_pTrackTableView->loadTrackModel(m_pTableModel);
    auto* pHeader = qobject_cast<WTrackTableViewHeader*>(
            m_pTrackTableView->horizontalHeader());
    if (pHeader && !pHeader->hasPersistedHeaderState()) {
        const int colorColumn = m_pTableModel->fieldIndex(QStringLiteral("color"));
        const int energyColumn = m_pTableModel->fieldIndex(QStringLiteral("energy"));
        const int from = pHeader->visualIndex(colorColumn);
        const int to = pHeader->visualIndex(energyColumn) + 1;
        if (from >= 0 && to >= 0 && from != to) {
            pHeader->moveSection(from, to);
        }
    }

    connect(pRefreshButton,
            &QPushButton::clicked,
            this,
            &DlgRestLibraryBrowser::refreshRequested);
    connect(m_pTrackTableView,
            &WTrackTableView::loadTrack,
            this,
            &DlgRestLibraryBrowser::loadTrack);
    connect(m_pTrackTableView,
            &WTrackTableView::loadTrackToPlayer,
            this,
            &DlgRestLibraryBrowser::loadTrackToPlayer);
    connect(m_pTrackTableView,
            &WTrackTableView::unresolvedTrackLoadRequested,
            this,
            &DlgRestLibraryBrowser::unresolvedTrackLoadRequested);
    connect(m_pTrackTableView,
            &WTrackTableView::unresolvedTrackLoadToPlayerRequested,
            this,
            &DlgRestLibraryBrowser::unresolvedTrackLoadToPlayerRequested);
    connect(m_pTrackTableView,
            &WTrackTableView::unresolvedTracksAddToAutoDJRequested,
            this,
            &DlgRestLibraryBrowser::unresolvedTracksAddToAutoDJRequested);
    connect(m_pTrackTableView,
            &WTrackTableView::trackSelected,
            this,
            &DlgRestLibraryBrowser::trackSelected);
    connect(pLibrary,
            &Library::setTrackTableFont,
            m_pTrackTableView,
            &WTrackTableView::setTrackTableFont);
    connect(pLibrary,
            &Library::setTrackTableRowHeight,
            m_pTrackTableView,
            &WTrackTableView::setTrackTableRowHeight);
    connect(pLibrary,
            &Library::setSelectedClick,
            m_pTrackTableView,
            &WTrackTableView::setSelectedClick);
}

void DlgRestLibraryBrowser::onSearch(const QString& text) {
    m_pTableModel->search(text);
}

void DlgRestLibraryBrowser::onShow() {
}

bool DlgRestLibraryBrowser::hasFocus() const {
    return m_pTrackTableView->hasFocus();
}

void DlgRestLibraryBrowser::setFocus() {
    m_pTrackTableView->setFocus();
}

void DlgRestLibraryBrowser::saveCurrentViewState() {
    m_pTrackTableView->saveCurrentViewState();
}

bool DlgRestLibraryBrowser::restoreCurrentViewState() {
    return m_pTrackTableView->restoreCurrentViewState();
}

QString DlgRestLibraryBrowser::currentSearch() const {
    return m_pTableModel->currentSearch();
}

QStringList DlgRestLibraryBrowser::selectedRemoteIds() const {
    QStringList remoteIds;
    const QItemSelectionModel* pSelectionModel =
            m_pTrackTableView->selectionModel();
    if (!pSelectionModel) {
        return remoteIds;
    }
    for (const QModelIndex& index : pSelectionModel->selectedRows()) {
        const QString remoteId = m_pTableModel->remoteIdForIndex(index);
        if (!remoteId.isEmpty()) {
            remoteIds.append(remoteId);
        }
    }
    return remoteIds;
}

void DlgRestLibraryBrowser::restoreSelectedRemoteIds(
        const QStringList& remoteIds) {
    QItemSelectionModel* pSelectionModel = m_pTrackTableView->selectionModel();
    if (!pSelectionModel) {
        return;
    }
    pSelectionModel->clearSelection();
    QModelIndex firstIndex;
    for (const QString& remoteId : remoteIds) {
        const int row = m_pTableModel->visibleRowForRemoteId(remoteId);
        if (row < 0) {
            continue;
        }
        const QModelIndex index = m_pTableModel->index(row, 0);
        pSelectionModel->select(
                index,
                QItemSelectionModel::Select | QItemSelectionModel::Rows);
        if (!firstIndex.isValid()) {
            firstIndex = index;
        }
    }
    if (firstIndex.isValid()) {
        pSelectionModel->setCurrentIndex(
                firstIndex,
                QItemSelectionModel::NoUpdate);
        m_pTrackTableView->scrollTo(firstIndex);
    }
}

void DlgRestLibraryBrowser::setStatusText(const QString& text) {
    m_pStatusLabel->setText(text);
}

} // namespace mixxx::library::rest
