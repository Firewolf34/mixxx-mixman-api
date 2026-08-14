#pragma once

#include <QAction>
#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QStringList>
#include <QTimer>

#include "library/libraryfeature.h"
#include "library/rest/restlibrarycachemanager.h"
#include "library/rest/restlibraryclient.h"
#include "library/rest/restlibrarymutationsequencer.h"
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
    ~RestLibraryFeature() override;

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
    void slotCurrentPlayingDeckChanged(int deck);
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
    void slotMixManSessionCreated(
            const mixxx::library::rest::RestLibrarySession& session);
    void slotMixManSessionInstanceRegistered(
            const mixxx::library::rest::RestLibrarySessionRegistration& registration);
    void slotMixManSessionFetched(
            const mixxx::library::rest::RestLibrarySession& session);
    void slotMixManSessionWriteStatusUpdated(
            const mixxx::library::rest::RestLibrarySessionWriteStatus& status);
    void slotMixManSessionContractVerified(
            const mixxx::library::rest::RestLibrarySessionContract& contract);
    void slotRequestDiagnosticUpdated(
            const mixxx::library::rest::RestLibraryRequestDiagnostic& diagnostic);
    void slotSessionHeartbeat();
    void slotPlaybackLeaseRenew();
    void slotPlaybackLeaseRelease();
    void slotAuthorityReconcile();
    void slotPolicyPresetChanged(const QString& presetKey);
    void slotTargetEnergyChanged(bool enabled, int energy);
    void slotTargetColorChanged(bool enabled, const QString& color);
    void slotTargetBpmChanged(bool enabled, int bpm);
    void slotRerollRequested();
    void slotLoadTrackRequested(TrackPointer pTrack);
    void slotLoadTrackToPlayerRequested(TrackPointer pTrack, const QString& group, bool play);
    void slotFetchFailed(const QString& message);
    void slotTrackCacheStateChanged(
            const mixxx::library::rest::RestLibraryCacheResult& result);

  private:
    void refreshForTrack(const TrackPointer& pTrack, bool force, bool publishPlayback = true);
    void requestRecommendationsForRemoteId(
            const RestLibrarySettings& settings,
            const QString& remoteId);
    void rememberRemoteId(const QString& remoteId);
    QStringList recentRemoteIdsForRequest(const QString& remoteId) const;
    void setRecommendationTracks(const QList<RestLibraryTrack>& tracks);
    void ensureMixManSession(const RestLibrarySettings& settings);
    void resetMixManSessionState();
    QString mixManSessionConfigKey(const RestLibrarySettings& settings) const;
    void publishMixManSnapshot(
            const RestLibrarySettings& settings,
            const TrackPointer& pTrack,
            const QString& remoteId);
    void publishMixManPlayback(
            const RestLibrarySettings& settings,
            const TrackPointer& pTrack,
            const QString& remoteId,
            const QString& playbackState);
    void ensureMixManPlaybackControl(const RestLibrarySettings& settings);
    void flushMixManPlaybackMutations(const RestLibrarySettings& settings);
    void selectMixManCandidateForTrack(const TrackPointer& pTrack);
    void requestMixManPolicyRefresh(const RestLibrarySettings& settings);
    QString selectionOriginForRemoteId(const QString& remoteId) const;
    void updateMixManIntent(const RestLibrarySettings& settings);
    QJsonObject mixManSessionMetadata() const;
    QJsonObject mixManTrackSnapshot(const TrackPointer& pTrack, const QString& remoteId) const;
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
    RestLibrarySession m_mixManSession;
    RestLibrarySettings m_mixManSessionSettings;
    RestLibrarySessionRegistration m_mixManRegistration;
    RestLibraryAuthoritativeState m_authoritativeState;
    RestLibraryPlaybackLease m_playbackLease;
    RestLibraryMutationSequencer m_mutationSequencer;
    QTimer m_sessionHeartbeatTimer;
    QTimer m_playbackLeaseRenewTimer;
    QTimer m_playbackLeaseReleaseTimer;
    QTimer m_authorityReconcileTimer;
    QHash<QString, QString> m_cachedPathToRemoteId;
    QHash<QString, RestLibraryCacheState> m_cacheStates;
    QString m_lastRequestedTrackLocation;
    QString m_currentRemoteId;
    QString m_previousRemoteId;
    QStringList m_recentRemoteIds;
    QString m_mixManSessionConfigKey;
    QString m_sessionStatusText;
    QString m_requestDiagnosticText;
    QString m_statusText;
    RestLibrarySessionPlayback m_pendingPlayback;
    RestLibrarySessionSnapshot m_pendingSnapshot;
    QString m_pendingCandidateTrackId;
    QString m_pendingCandidateSelectionOrigin;
    QJsonObject m_pendingCandidateMetadata;
    int m_recommendationCount = 0;
    int m_playbackLeaseTtlSeconds = 30;
    double m_averageQuality = 0.0;
    bool m_followCurrentTrack = true;
    bool m_sessionCreateAttempted = false;
    bool m_playbackControlClaimPending = false;
    bool m_playbackControlReleasePending = false;
    bool m_playbackLeaseOwned = false;

  signals:
    void statusTextChanged(const QString& statusText);
};

} // namespace mixxx::library::rest
