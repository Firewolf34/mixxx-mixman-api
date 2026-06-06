#pragma once

#include <QAction>
#include <QHash>
#include <QNetworkAccessManager>

#include "library/libraryfeature.h"
#include "library/rest/restlibrarycachemanager.h"
#include "library/rest/restlibraryclient.h"
#include "library/rest/restlibrarytablemodel.h"
#include "library/treeitemmodel.h"
#include "track/track_decl.h"
#include "util/parented_ptr.h"

namespace mixxx::library::rest {

class RestLibraryFeature final : public LibraryFeature {
    Q_OBJECT

  public:
    RestLibraryFeature(
            Library* pLibrary,
            UserSettingsPointer pConfig);
    ~RestLibraryFeature() override = default;

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
    void slotCurrentPlayingTrackChanged(TrackPointer pTrack);
    void slotTracksFetched(const QList<mixxx::library::rest::RestLibraryTrack>& tracks);
    void slotTrackLookupSucceeded(const QString& remoteId);
    void slotTrackLookupMissed(const QString& message);
    void slotRecommendationsFetched(
            const QList<mixxx::library::rest::RestLibraryTrack>& tracks);
    void slotFetchFailed(const QString& message);
    void slotTrackCacheStateChanged(
            const mixxx::library::rest::RestLibraryCacheResult& result);

  private:
    void refreshForTrack(const TrackPointer& pTrack, bool force);
    void requestRecommendationsForRemoteId(
            const RestLibrarySettings& settings,
            const QString& remoteId);
    void setRecommendationTracks(const QList<RestLibraryTrack>& tracks);
    QString remoteIdForTrack(const TrackPointer& pTrack) const;
    static QString normalizedTrackLocation(const QString& location);

    parented_ptr<TreeItemModel> m_pSidebarModel;
    parented_ptr<RestLibraryTableModel> m_pTableModel;
    parented_ptr<QAction> m_pRefreshAction;
    QNetworkAccessManager m_networkAccessManager;
    RestLibraryClient m_client;
    RestLibraryCacheManager m_cacheManager;
    QHash<QString, QString> m_cachedPathToRemoteId;
    QString m_lastRequestedTrackLocation;
    QString m_currentRemoteId;
};

} // namespace mixxx::library::rest
