#include "library/rest/dlgrestlibrarybrowser.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

#include "controllers/keyboard/keyboardeventfilter.h"
#include "library/library.h"
#include "library/rest/restlibrarytablemodel.h"
#include "library/rest/restlibrarysettings.h"
#include "moc_dlgrestlibrarybrowser.cpp"
#include "widget/wlibrary.h"
#include "widget/wtracktableview.h"
#include "widget/wtracktableviewheader.h"

namespace mixxx::library::rest {

namespace {

void configureToolbarButton(
        QToolButton* pButton,
        const QString& text,
        const QString& shortText,
        const QString& iconName,
        QStyle::StandardPixmap fallback,
        bool showText) {
    QIcon icon = QIcon::fromTheme(iconName);
    if (icon.isNull()) {
        icon = pButton->style()->standardIcon(fallback);
    }
    pButton->setIcon(icon);
    pButton->setAccessibleName(text);
    pButton->setToolTip(text);
    if (showText) {
        pButton->setText(text);
        pButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    } else if (!icon.isNull()) {
        pButton->setText(text);
        pButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    } else {
        pButton->setText(shortText);
        pButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    }
}

QString trackDisplayName(const RestLibraryTrack& track) {
    return track.artist.isEmpty()
            ? track.title
            : QStringLiteral("%1 — %2").arg(track.artist, track.title);
}

void enforceTextLimit(
        QPlainTextEdit* pEdit,
        QLabel* pCounter,
        int maximum) {
    QObject::connect(pEdit, &QPlainTextEdit::textChanged, pEdit, [=] {
        QString text = pEdit->toPlainText();
        if (text.size() > maximum) {
            text.truncate(maximum);
            const QSignalBlocker blocker(pEdit);
            pEdit->setPlainText(text);
            QTextCursor cursor = pEdit->textCursor();
            cursor.movePosition(QTextCursor::End);
            pEdit->setTextCursor(cursor);
        }
        pCounter->setText(QObject::tr("%1 / %2").arg(text.size()).arg(maximum));
    });
}

} // namespace

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
          m_pStatusLabel(new QLabel(tr("Open REST Library to load the catalog."), this)),
          m_pRefreshButton(new QToolButton(this)),
          m_pFavourUpButton(new QToolButton(this)),
          m_pFavourDownButton(new QToolButton(this)),
          m_pDjNoteButton(new QToolButton(this)),
          m_pReturnToReviewButton(new QToolButton(this)) {
    const bool showButtonText = parent->getShowButtonText();
    configureToolbarButton(m_pRefreshButton,
            tr("Refresh"),
            tr("Refresh"),
            QStringLiteral("view-refresh"),
            QStyle::SP_BrowserReload,
            showButtonText);
    configureToolbarButton(m_pFavourUpButton,
            tr("Favour Up"),
            QStringLiteral("+"),
            QStringLiteral("go-up"),
            QStyle::SP_ArrowUp,
            showButtonText);
    configureToolbarButton(m_pFavourDownButton,
            tr("Favour Down"),
            QStringLiteral("−"),
            QStringLiteral("go-down"),
            QStyle::SP_ArrowDown,
            showButtonText);
    configureToolbarButton(m_pDjNoteButton,
            tr("DJ Note"),
            tr("Note"),
            QStringLiteral("document-edit"),
            QStyle::SP_FileDialogDetailedView,
            showButtonText);
    configureToolbarButton(m_pReturnToReviewButton,
            tr("Return to Review"),
            tr("Review"),
            QStringLiteral("edit-undo"),
            QStyle::SP_ArrowBack,
            showButtonText);

    auto* pToolbar = new QHBoxLayout();
    pToolbar->addWidget(m_pRefreshButton);
    pToolbar->addWidget(m_pStatusLabel, 1);
    pToolbar->addWidget(m_pFavourUpButton);
    pToolbar->addWidget(m_pFavourDownButton);
    pToolbar->addWidget(m_pDjNoteButton);
    pToolbar->addWidget(m_pReturnToReviewButton);

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

    connect(m_pRefreshButton,
            &QToolButton::clicked,
            this,
            &DlgRestLibraryBrowser::refreshRequested);
    connect(m_pFavourUpButton, &QToolButton::clicked, this, [this] {
        emit favourBumpRequested(1);
    });
    connect(m_pFavourDownButton, &QToolButton::clicked, this, [this] {
        emit favourBumpRequested(-1);
    });
    connect(m_pDjNoteButton,
            &QToolButton::clicked,
            this,
            &DlgRestLibraryBrowser::djNoteRequested);
    connect(m_pReturnToReviewButton,
            &QToolButton::clicked,
            this,
            &DlgRestLibraryBrowser::returnToReviewRequested);
    connect(m_pTrackTableView->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this,
            [this] { emit selectedRemoteIdsChanged(); });
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
    setMaintenanceControlState(false, false, false, true);
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

void DlgRestLibraryBrowser::setMaintenanceControlState(
        bool favourEnabled,
        bool djNoteEnabled,
        bool returnToReviewEnabled,
        bool refreshEnabled,
        const QString& returnToReviewToolTip) {
    m_pFavourUpButton->setEnabled(favourEnabled);
    m_pFavourDownButton->setEnabled(favourEnabled);
    m_pDjNoteButton->setEnabled(djNoteEnabled);
    m_pReturnToReviewButton->setEnabled(returnToReviewEnabled);
    m_pRefreshButton->setEnabled(refreshEnabled);
    m_pReturnToReviewButton->setToolTip(returnToReviewToolTip.isEmpty()
                    ? tr("Return to Review")
                    : returnToReviewToolTip);
}

std::optional<QString> DlgRestLibraryBrowser::editDjNote(
        const RestLibraryTrack& track,
        const QStringList& presets) {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Edit DJ Note"));
    auto* pLayout = new QVBoxLayout(&dialog);
    auto* pTrackLabel = new QLabel(trackDisplayName(track), &dialog);
    pTrackLabel->setWordWrap(true);
    pLayout->addWidget(pTrackLabel);

    auto* pEdit = new QPlainTextEdit(&dialog);
    pEdit->setPlainText(track.djComment);
    pEdit->setPlaceholderText(tr("Short warning or maintenance note for this track"));
    pLayout->addWidget(pEdit, 1);

    auto* pPresetLayout = new QHBoxLayout();
    auto* pPresets = new QComboBox(&dialog);
    pPresets->addItem(tr("Choose a preset…"));
    pPresets->addItems(presets);
    auto* pAddPreset = new QPushButton(tr("Add preset"), &dialog);
    pPresetLayout->addWidget(pPresets, 1);
    pPresetLayout->addWidget(pAddPreset);
    pLayout->addLayout(pPresetLayout);

    auto* pCounter = new QLabel(&dialog);
    pCounter->setAlignment(Qt::AlignRight);
    pLayout->addWidget(pCounter);
    enforceTextLimit(pEdit, pCounter, config::kMaxDjNoteLength);
    pCounter->setText(tr("%1 / %2")
                              .arg(pEdit->toPlainText().size())
                              .arg(config::kMaxDjNoteLength));

    connect(pAddPreset, &QPushButton::clicked, &dialog, [=] {
        if (pPresets->currentIndex() <= 0) {
            return;
        }
        const QString preset = pPresets->currentText().trimmed();
        const QStringList lines = pEdit->toPlainText().split(QLatin1Char('\n'));
        if (lines.contains(preset)) {
            return;
        }
        QString text = pEdit->toPlainText();
        if (!text.isEmpty() && !text.endsWith(QLatin1Char('\n'))) {
            text.append(QLatin1Char('\n'));
        }
        text.append(preset);
        pEdit->setPlainText(text.left(config::kMaxDjNoteLength));
    });

    auto* pButtons = new QDialogButtonBox(
            QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    connect(pButtons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(pButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    pLayout->addWidget(pButtons);
    pEdit->setFocus();
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    return pEdit->toPlainText();
}

std::optional<QString> DlgRestLibraryBrowser::confirmReturnToReview(
        const RestLibraryTrack& track) {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Return Track to Review"));
    auto* pLayout = new QVBoxLayout(&dialog);
    auto* pWarning = new QLabel(
            tr("Return “%1” to the MixMan review queue? It will disappear from the promoted catalog.")
                    .arg(trackDisplayName(track)),
            &dialog);
    pWarning->setWordWrap(true);
    pLayout->addWidget(pWarning);
    pLayout->addWidget(new QLabel(tr("Optional reason:"), &dialog));
    auto* pReason = new QPlainTextEdit(&dialog);
    pReason->setPlaceholderText(tr("Why this track needs review"));
    pLayout->addWidget(pReason);
    auto* pCounter = new QLabel(&dialog);
    pCounter->setAlignment(Qt::AlignRight);
    pLayout->addWidget(pCounter);
    enforceTextLimit(pReason, pCounter, config::kMaxDjNoteLength);
    pCounter->setText(tr("0 / %1").arg(config::kMaxDjNoteLength));

    auto* pButtons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    QPushButton* pConfirm = pButtons->addButton(
            tr("Return to Review"), QDialogButtonBox::DestructiveRole);
    pButtons->button(QDialogButtonBox::Cancel)->setDefault(true);
    connect(pConfirm, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(pButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    pLayout->addWidget(pButtons);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    return pReason->toPlainText().trimmed();
}

void DlgRestLibraryBrowser::setStatusText(const QString& text) {
    m_pStatusLabel->setText(text);
}

} // namespace mixxx::library::rest
