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

class DlgRestLibrary;

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
    void slotFollowCurrentTrackChanged(bool follow);
    void slotCurrentPlayingTrackChanged(TrackPointer pTrack);
    void slotTracksFetched(const QList<mixxx::library::rest::RestLibraryTrack>& tracks);
    void slotTrackLookupSucceeded(const QString& remoteId);
    void slotTrackLookupMissed(const QString& message);
    void slotRecommendationsFetched(
            const QList<mixxx::library::rest::RestLibraryTrack>& tracks);
    void slotDiagnosticsUpdated(const mixxx::library::rest::RestLibraryDiagnostics& diagnostics);
    void slotPolicyPresetsFetched(
            const QList<mixxx::library::rest::RestLibraryPolicyPreset>& presets);
    void slotMixManPolicyPathFetched(
            const mixxx::library::rest::RestLibraryPolicyPath& policyPath);
    void slotPolicyPresetChanged(const QString& presetKey);
    void slotTargetEnergyChanged(bool enabled, int energy);
    void slotTargetColorChanged(bool enabled, const QString& color);
    void slotFetchFailed(const QString& message);
    void slotTrackCacheStateChanged(
            const mixxx::library::rest::RestLibraryCacheResult& result);

  private:
    void refreshForTrack(const TrackPointer& pTrack, bool force);
    void requestRecommendationsForRemoteId(
            const RestLibrarySettings& settings,
            const QString& remoteId);
    void setRecommendationTracks(const QList<RestLibraryTrack>& tracks);
    void refreshMixManControls(const RestLibrarySettings& settings);
    void updateDiagnosticsText();
    void setPathSummary(const RestLibraryPolicyPath& policyPath);
    void setStatusText(const QString& statusText);
    void updateReadyStatus();
    void clearRecommendations();
    QString remoteIdForTrack(const TrackPointer& pTrack) const;
    static QString normalizedTrackLocation(const QString& location);

    parented_ptr<TreeItemModel> m_pSidebarModel;
    parented_ptr<RestLibraryTableModel> m_pTableModel;
    parented_ptr<QAction> m_pRefreshAction;
    DlgRestLibrary* m_pRestLibraryView = nullptr;
    QNetworkAccessManager m_networkAccessManager;
    RestLibraryClient m_client;
    RestLibraryCacheManager m_cacheManager;
    RestLibraryDiagnostics m_diagnostics;
    QHash<QString, QString> m_cachedPathToRemoteId;
    QHash<QString, RestLibraryCacheState> m_cacheStates;
    QString m_lastRequestedTrackLocation;
    QString m_currentRemoteId;
    QString m_statusText;
    int m_recommendationCount = 0;
    double m_averageQuality = 0.0;
    bool m_followCurrentTrack = true;

  signals:
    void statusTextChanged(const QString& statusText);
};

} // namespace mixxx::library::rest
