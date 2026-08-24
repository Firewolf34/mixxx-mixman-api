#pragma once

#include <optional>

#include <QModelIndex>
#include <QStringList>
#include <QWidget>

#include "library/dao/playlistdao.h"
#include "library/libraryview.h"
#include "preferences/usersettings.h"
#include "track/track_decl.h"
#ifdef __STEM__
#include "engine/engine.h"
#endif

class KeyboardEventFilter;
class QLabel;
class QToolButton;
class Library;
class WLibrary;
class WTrackTableView;

namespace mixxx::library::rest {

class RestLibraryTableModel;
struct RestLibraryTrack;

class DlgRestLibraryBrowser final : public QWidget, public LibraryView {
    Q_OBJECT

  public:
    DlgRestLibraryBrowser(
            WLibrary* parent,
            UserSettingsPointer pConfig,
            Library* pLibrary,
            RestLibraryTableModel* pTableModel,
            KeyboardEventFilter* pKeyboard);

    void onSearch(const QString& text) override;
    void onShow() override;
    bool hasFocus() const override;
    void setFocus() override;
    void saveCurrentViewState() override;
    bool restoreCurrentViewState() override;
    QString currentSearch() const;
    QStringList selectedRemoteIds() const;
    void restoreSelectedRemoteIds(const QStringList& remoteIds);
    void setMaintenanceControlState(
            bool favourEnabled,
            bool djNoteEnabled,
            bool returnToReviewEnabled,
            bool refreshEnabled,
            const QString& returnToReviewToolTip = {});
    std::optional<QString> editDjNote(
            const RestLibraryTrack& track,
            const QStringList& presets);
    std::optional<QString> confirmReturnToReview(
            const RestLibraryTrack& track);

  public slots:
    void setStatusText(const QString& text);

  signals:
    void refreshRequested();
    void favourBumpRequested(int direction);
    void djNoteRequested();
    void returnToReviewRequested();
    void selectedRemoteIdsChanged();
    void loadTrack(TrackPointer pTrack);
#ifdef __STEM__
    void loadTrackToPlayer(TrackPointer pTrack,
            const QString& group,
            mixxx::StemChannelSelection stemMask,
            bool play);
    void unresolvedTrackLoadToPlayerRequested(const QModelIndex& index,
            const QString& group,
            mixxx::StemChannelSelection stemMask,
            bool play);
#else
    void loadTrackToPlayer(TrackPointer pTrack, const QString& group, bool play);
    void unresolvedTrackLoadToPlayerRequested(
            const QModelIndex& index,
            const QString& group,
            bool play);
#endif
    void unresolvedTrackLoadRequested(const QModelIndex& index);
    void unresolvedTracksAddToAutoDJRequested(
            const QModelIndexList& indices,
            PlaylistDAO::AutoDJSendLoc location);
    void trackSelected(TrackPointer pTrack);

  private:
    WTrackTableView* const m_pTrackTableView;
    RestLibraryTableModel* const m_pTableModel;
    QLabel* const m_pStatusLabel;
    QToolButton* const m_pRefreshButton;
    QToolButton* const m_pFavourUpButton;
    QToolButton* const m_pFavourDownButton;
    QToolButton* const m_pDjNoteButton;
    QToolButton* const m_pReturnToReviewButton;
};

} // namespace mixxx::library::rest
