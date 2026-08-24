#pragma once

#include <QAction>
#include <QHash>
#include <QSet>

#include <memory>

#include "library/dao/playlistdao.h"
#include "library/libraryfeature.h"
#include "library/rest/restlibrarybackend.h"
#include "library/rest/restlibrarycatalogprovider.h"
#include "library/rest/restlibrarycataloglimits.h"
#include "library/rest/restlibraryloudnessmanager.h"
#include "library/rest/restlibrarytablemodel.h"
#include "library/treeitemmodel.h"
#include "util/parented_ptr.h"

class RestLibraryBrowserFeatureTest;
class TrackCollectionManager;

namespace mixxx::library::rest {

class DlgRestLibraryBrowser;

class RestLibraryBrowserFeature final : public LibraryFeature {
    Q_OBJECT

  public:
    RestLibraryBrowserFeature(
            Library* pLibrary,
            UserSettingsPointer pConfig,
            RestLibraryBackend* pBackend);

    QVariant title() override;
    TreeItemModel* sidebarModel() const override;
    void bindLibraryWidget(WLibrary* pLibraryWidget, KeyboardEventFilter* pKeyboard) override;
    bool hasTrackTable() override {
        return true;
    }

  public slots:
    void activate() override;
    void onRightClick(const QPoint& globalPos) override;

  private slots:
    void slotRefresh();
    void slotCatalogPageFetched(
            const QString& scopeIdentity,
            const RestLibraryCatalogPage& page);
    void slotCatalogFetchFailed(
            const QString& scopeIdentity,
            const QString& message);
    void slotTrackCacheStateChanged(const RestLibraryCacheResult& result);
    void slotTrackLoudnessPrepared(const RestLibraryLoudnessResult& result);
    void slotUnresolvedTrackLoad(const QModelIndex& index);
#ifdef __STEM__
    void slotUnresolvedTrackLoadToPlayer(const QModelIndex& index,
            const QString& group,
            mixxx::StemChannelSelection stemMask,
            bool play);
#else
    void slotUnresolvedTrackLoadToPlayer(
            const QModelIndex& index,
            const QString& group,
            bool play);
#endif
    void slotUnresolvedTracksAddToAutoDJ(
            const QModelIndexList& indices,
            PlaylistDAO::AutoDJSendLoc location);
    void slotFavourBumpRequested(int direction);
    void slotDjNoteRequested();
    void slotReturnToReviewRequested();
    void slotMutationMetadataFetched(
            const QString& scopeIdentity,
            const RestLibraryMutationMetadata& metadata);
    void slotTrackMutationFinished(
            const QString& scopeIdentity,
            const RestLibraryTrackMutationResult& result);

  private:
    friend class ::RestLibraryBrowserFeatureTest;

    RestLibraryBrowserFeature(
            Library* pLibrary,
            UserSettingsPointer pConfig,
            RestLibraryBackend* pBackend,
            TrackCollectionManager* pTrackCollectionManager,
            RestLibraryLoudnessManager* pLoudnessManager = nullptr,
            RestLibraryCatalogProvider* pCatalogProvider = nullptr);

    struct PlayerLoadIntent {
        QString remoteId;
        QString group;
        bool play = false;
#ifdef __STEM__
        mixxx::StemChannelSelection stemMask;
#endif
    };

    struct AutoDJIntent {
        QStringList remoteIds;
        QSet<QString> pendingIds;
        QSet<QString> failedIds;
        PlaylistDAO::AutoDJSendLoc location = PlaylistDAO::AutoDJSendLoc::BOTTOM;

        bool active() const {
            return !remoteIds.isEmpty();
        }

        void clear() {
            remoteIds.clear();
            pendingIds.clear();
            failedIds.clear();
        }
    };

    void requestNextCatalogPage(const QString& cursor);
    void requestTrackCache(
            const QString& remoteId,
            const RestLibraryCatalogContext& context);
    bool finishPendingLoads(const QString& remoteId);
    RestLibraryLoudnessResult prepareTrackForPlayback(const QString& remoteId);
    void failPendingLoads(const QString& remoteId, const QString& errorText);
    void finishAutoDJIfReady();
    bool mayLoadToGroup(const QString& group) const;
    bool resetIfContextChanged(const RestLibraryCatalogContext& context);
    bool validateDownloadContext(RestLibraryCatalogContext* pContext);
    void clearPendingIntents();
    void updateLoadCapabilities(const RestLibraryCatalogContext& context);
    void setStatusText(const QString& text);
    void updateStatusSummary();
    void refreshMutationMetadata(const RestLibraryCatalogContext& context);
    void updateMaintenanceControls();
    void startFavourMutation(
            const QString& remoteId,
            int stepCount);
    QString selectedRemoteId() const;

    parented_ptr<TreeItemModel> m_pSidebarModel;
    parented_ptr<RestLibraryTableModel> m_pTableModel;
    parented_ptr<QAction> m_pRefreshAction;
    DlgRestLibraryBrowser* m_pView = nullptr;
    TrackCollectionManager* const m_pTrackCollectionManager;
    RestLibraryLoudnessManager* const m_pLoudnessManager;
    std::unique_ptr<RestLibraryCatalogProvider> m_pOwnedCatalogProvider;
    RestLibraryCatalogProvider* m_pCatalogProvider = nullptr;
    RestLibraryCatalogContext m_currentContext;
    RestLibraryCatalogContext m_refreshContext;
    QList<RestLibraryTrack> m_stagingTracks;
    QSet<QString> m_stagingRemoteIds;
    QSet<QString> m_seenCursors;
    RestLibraryCatalogLimits m_catalogLimits;
    QString m_pendingDefaultLoadRemoteId;
    QHash<QString, PlayerLoadIntent> m_pendingPlayerLoads;
    QHash<TrackId, QSet<QString>> m_loudnessRemoteIds;
    AutoDJIntent m_autoDJIntent;
    QString m_statusText;
    bool m_catalogLoaded = false;
    bool m_refreshing = false;
    RestLibraryMutationMetadata m_mutationMetadata;
    QString m_mutationMetadataScopeIdentity;
    RestLibraryCatalogContext m_mutationContext;
    QString m_mutatingRemoteId;
    RestLibraryTrackMutation m_activeMutation =
            RestLibraryTrackMutation::Favour;
    int m_queuedFavourSteps = 0;
    bool m_mutationBusy = false;
    bool m_mutationMetadataLoading = false;

  signals:
    void statusTextChanged(const QString& text);
};

} // namespace mixxx::library::rest
