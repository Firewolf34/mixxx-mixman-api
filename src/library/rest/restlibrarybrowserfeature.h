#pragma once

#include <QAction>
#include <QHash>
#include <QSet>

#include "library/dao/playlistdao.h"
#include "library/libraryfeature.h"
#include "library/rest/restlibrarybackend.h"
#include "library/rest/restlibraryclient.h"
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
    void slotCatalogPageFetched(const RestLibraryCatalogPage& page);
    void slotCatalogFetchFailed(const QString& message);
    void slotTrackCacheStateChanged(const RestLibraryCacheResult& result);
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

  private:
    friend class ::RestLibraryBrowserFeatureTest;

    RestLibraryBrowserFeature(
            Library* pLibrary,
            UserSettingsPointer pConfig,
            RestLibraryBackend* pBackend,
            TrackCollectionManager* pTrackCollectionManager);

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
            const RestLibrarySettings& settings);
    void finishPendingLoads(const QString& remoteId);
    void finishAutoDJIfReady();
    bool mayLoadToGroup(const QString& group) const;
    bool resetIfSettingsChanged(const RestLibrarySettings& settings);
    bool validateDownloadSettings(RestLibrarySettings* pSettings);
    void clearPendingIntents();
    void updateLoadCapabilities(const RestLibrarySettings& settings);
    QString settingsIdentity(const RestLibrarySettings& settings) const;
    void setStatusText(const QString& text);

    parented_ptr<TreeItemModel> m_pSidebarModel;
    parented_ptr<RestLibraryTableModel> m_pTableModel;
    parented_ptr<QAction> m_pRefreshAction;
    DlgRestLibraryBrowser* m_pView = nullptr;
    RestLibraryBackend* const m_pBackend;
    TrackCollectionManager* const m_pTrackCollectionManager;
    RestLibraryClient m_client;
    RestLibrarySettings m_refreshSettings;
    QList<RestLibraryTrack> m_stagingTracks;
    QSet<QString> m_stagingRemoteIds;
    QSet<QString> m_seenCursors;
    QString m_settingsIdentity;
    QString m_pendingDefaultLoadRemoteId;
    QHash<QString, PlayerLoadIntent> m_pendingPlayerLoads;
    AutoDJIntent m_autoDJIntent;
    QString m_statusText;
    bool m_catalogLoaded = false;
    bool m_refreshing = false;

  signals:
    void statusTextChanged(const QString& text);
};

} // namespace mixxx::library::rest
