#include "library/rest/restlibraryfeature.h"

#include <algorithm>

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QNetworkReply>
#include <QStringList>
#include <QUrl>
#include <QUuid>

#include "controllers/keyboard/keyboardeventfilter.h"
#include "library/library.h"
#include "library/rest/dlgrestlibrary.h"
#include "library/rest/restlibrarysettings.h"
#include "library/treeitem.h"
#include "mixer/playermanager.h"
#include "mixer/playerinfo.h"
#include "moc_restlibraryfeature.cpp"
#include "track/track.h"
#include "util/logger.h"
#include "widget/wlibrary.h"

namespace mixxx::library::rest {

namespace {

const Logger kLogger("RestLibraryFeature");
const QString kViewName = QStringLiteral("REST Recommendations");
const QString kSessionCreateOperation = QStringLiteral("session_create");
const QString kSessionPlaybackOperation = QStringLiteral("session_playback");
const QString kPlaybackControlClaimOperation =
        QStringLiteral("session_playback_control_claim");
const QString kPlaybackControlRenewOperation =
        QStringLiteral("session_playback_control_renew");
const QString kPlaybackControlReleaseOperation =
        QStringLiteral("session_playback_control_release");
constexpr int kSessionHeartbeatIntervalMillis = 30000;
constexpr int kDefaultPlaybackLeaseRenewIntervalMillis = 10000;
constexpr int kDefaultPlaybackPauseGraceMillis = 15000;
constexpr qsizetype kMaxRecentRemoteIds = 20;

QString conciseDiagnosticText(const RestLibraryRequestDiagnostic& diagnostic) {
    if (diagnostic.networkError == static_cast<int>(QNetworkReply::OperationCanceledError)) {
        return QObject::tr("%1 canceled").arg(diagnostic.stage);
    }
    if (diagnostic.networkError == static_cast<int>(QNetworkReply::TimeoutError)) {
        return QObject::tr("%1 timed out").arg(diagnostic.stage);
    }
    if (diagnostic.networkError != static_cast<int>(QNetworkReply::NoError)) {
        return QObject::tr("%1 network error").arg(diagnostic.stage);
    }
    switch (diagnostic.statusCode) {
    case 401:
        return QObject::tr("Authentication failed");
    case 403:
        return QObject::tr("Permission denied");
    case 404:
        return QObject::tr("Endpoint not found");
    case 409:
        return QObject::tr("MixMan conflict");
    default:
        if (diagnostic.statusCode >= 500) {
            return QObject::tr("Server error");
        }
        break;
    }
    return diagnostic.summary;
}

} // namespace

RestLibraryFeature::RestLibraryFeature(
        Library* pLibrary,
        UserSettingsPointer pConfig)
        : LibraryFeature(pLibrary, std::move(pConfig), QStringLiteral("computer")),
          m_pSidebarModel(make_parented<TreeItemModel>(this)),
          m_pTableModel(make_parented<RestLibraryTableModel>(
                  this,
                  pLibrary->trackCollectionManager())),
          m_pRefreshAction(make_parented<QAction>(tr("Refresh"), this)),
          m_client(&m_networkAccessManager, this),
          m_cacheManager(&m_networkAccessManager, this) {
    m_clientId = QStringLiteral("mixxx-%1").arg(
            QUuid::createUuid().toString(QUuid::Id128));
    m_sessionHeartbeatTimer.setInterval(kSessionHeartbeatIntervalMillis);
    m_sessionHeartbeatTimer.setSingleShot(false);
    connect(&m_sessionHeartbeatTimer,
            &QTimer::timeout,
            this,
            &RestLibraryFeature::slotSessionHeartbeat);
    m_playbackLeaseRenewTimer.setInterval(kDefaultPlaybackLeaseRenewIntervalMillis);
    m_playbackLeaseRenewTimer.setSingleShot(false);
    connect(&m_playbackLeaseRenewTimer,
            &QTimer::timeout,
            this,
            &RestLibraryFeature::slotPlaybackLeaseRenew);
    m_playbackLeaseReleaseTimer.setInterval(kDefaultPlaybackPauseGraceMillis);
    m_playbackLeaseReleaseTimer.setSingleShot(true);
    connect(&m_playbackLeaseReleaseTimer,
            &QTimer::timeout,
            this,
            &RestLibraryFeature::slotPlaybackLeaseRelease);

    auto pRootItem = TreeItem::newRoot(this);
    m_pSidebarModel->setRootItem(std::move(pRootItem));

    connect(m_pRefreshAction,
            &QAction::triggered,
            this,
            &RestLibraryFeature::slotRefresh);
    connect(&m_client,
            &RestLibraryClient::tracksFetched,
            this,
            &RestLibraryFeature::slotTracksFetched);
    connect(&m_client,
            &RestLibraryClient::trackLookupSucceeded,
            this,
            &RestLibraryFeature::slotTrackLookupSucceeded);
    connect(&m_client,
            &RestLibraryClient::trackLookupMissed,
            this,
            &RestLibraryFeature::slotTrackLookupMissed);
    connect(&m_client,
            &RestLibraryClient::recommendationsFetched,
            this,
            &RestLibraryFeature::slotRecommendationsFetched);
    connect(&m_client,
            &RestLibraryClient::diagnosticsUpdated,
            this,
            &RestLibraryFeature::slotDiagnosticsUpdated);
    connect(&m_client,
            &RestLibraryClient::policyPresetsFetched,
            this,
            &RestLibraryFeature::slotPolicyPresetsFetched);
    connect(&m_client,
            &RestLibraryClient::mixManPolicyPathFetched,
            this,
            &RestLibraryFeature::slotMixManPolicyPathFetched);
    connect(&m_client,
            &RestLibraryClient::mixManSessionCreated,
            this,
            &RestLibraryFeature::slotMixManSessionCreated);
    connect(&m_client,
            &RestLibraryClient::mixManSessionFetched,
            this,
            &RestLibraryFeature::slotMixManSessionFetched);
    connect(&m_client,
            &RestLibraryClient::mixManSessionWriteStatusUpdated,
            this,
            &RestLibraryFeature::slotMixManSessionWriteStatusUpdated);
    connect(&m_client,
            &RestLibraryClient::mixManSessionContractVerified,
            this,
            &RestLibraryFeature::slotMixManSessionContractVerified);
    connect(&m_client,
            &RestLibraryClient::requestDiagnosticUpdated,
            this,
            &RestLibraryFeature::slotRequestDiagnosticUpdated);
    connect(&m_client,
            &RestLibraryClient::fetchFailed,
            this,
            &RestLibraryFeature::slotFetchFailed);
    connect(&m_cacheManager,
            &RestLibraryCacheManager::trackCacheStateChanged,
            this,
            &RestLibraryFeature::slotTrackCacheStateChanged);
    connect(&m_cacheManager,
            &RestLibraryCacheManager::requestDiagnosticUpdated,
            this,
            &RestLibraryFeature::slotRequestDiagnosticUpdated);
    connect(&PlayerInfo::instance(),
            &PlayerInfo::currentPlayingTrackChanged,
            this,
            &RestLibraryFeature::slotCurrentPlayingTrackChanged);
    connect(&PlayerInfo::instance(),
            &PlayerInfo::currentPlayingDeckChanged,
            this,
            &RestLibraryFeature::slotCurrentPlayingDeckChanged);
}

QVariant RestLibraryFeature::title() {
    return tr("REST Recommendations");
}

TreeItemModel* RestLibraryFeature::sidebarModel() const {
    return m_pSidebarModel;
}

void RestLibraryFeature::bindLibraryWidget(
        WLibrary* pLibraryWidget,
        KeyboardEventFilter* pKeyboard) {
    m_pRestLibraryView = new DlgRestLibrary(
            pLibraryWidget,
            m_pConfig,
            m_pLibrary,
            m_pTableModel,
            pKeyboard);
    m_pRestLibraryView->installEventFilter(pKeyboard);
    pLibraryWidget->registerView(kViewName, m_pRestLibraryView);

    connect(m_pRestLibraryView,
            &DlgRestLibrary::refreshRequested,
            this,
            &RestLibraryFeature::slotRefresh);
    connect(m_pRestLibraryView,
            &DlgRestLibrary::followCurrentTrackChanged,
            this,
            &RestLibraryFeature::slotFollowCurrentTrackChanged);
    connect(m_pRestLibraryView,
            &DlgRestLibrary::loadTrack,
            this,
            &RestLibraryFeature::slotLoadTrackRequested);
    connect(m_pRestLibraryView,
            &DlgRestLibrary::loadTrackToPlayer,
            this,
            &RestLibraryFeature::slotLoadTrackToPlayerRequested);
    connect(m_pRestLibraryView,
            &DlgRestLibrary::trackSelected,
            this,
            &RestLibraryFeature::trackSelected);
    connect(m_pRestLibraryView,
            &DlgRestLibrary::policyPresetChanged,
            this,
            &RestLibraryFeature::slotPolicyPresetChanged);
    connect(m_pRestLibraryView,
            &DlgRestLibrary::targetEnergyChanged,
            this,
            &RestLibraryFeature::slotTargetEnergyChanged);
    connect(m_pRestLibraryView,
            &DlgRestLibrary::targetColorChanged,
            this,
            &RestLibraryFeature::slotTargetColorChanged);
    connect(m_pRestLibraryView,
            &DlgRestLibrary::targetBpmChanged,
            this,
            &RestLibraryFeature::slotTargetBpmChanged);
    connect(m_pRestLibraryView,
            &DlgRestLibrary::rerollRequested,
            this,
            &RestLibraryFeature::slotRerollRequested);
    connect(this,
            &RestLibraryFeature::statusTextChanged,
            m_pRestLibraryView,
            &DlgRestLibrary::setStatusText);

    if (!m_statusText.isEmpty()) {
        emit statusTextChanged(m_statusText);
    }
    refreshMixManControls(RestLibrarySettings::fromConfig(m_pConfig));
}

void RestLibraryFeature::activate() {
    emit saveModelState();
    emit switchToView(kViewName);
    if (m_pRestLibraryView) {
        emit restoreSearch(m_pRestLibraryView->currentSearch());
    }
    emit enableCoverArtDisplay(false);
    slotRefresh();
}

void RestLibraryFeature::onRightClick(const QPoint& globalPos) {
    QMenu menu;
    menu.addAction(m_pRefreshAction);
    menu.exec(globalPos);
}

void RestLibraryFeature::slotRefresh() {
    refreshForTrack(PlayerInfo::instance().getCurrentPlayingTrack(), true);
}

void RestLibraryFeature::slotFollowCurrentTrackChanged(bool follow) {
    m_followCurrentTrack = follow;
    if (m_followCurrentTrack) {
        slotRefresh();
    }
}

void RestLibraryFeature::slotCurrentPlayingTrackChanged(TrackPointer pTrack) {
    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(m_pConfig);
    if (settings.isConfigured() && settings.useMixManDefaults) {
        ensureMixManSession(settings);
        const QString remoteId = remoteIdForTrack(pTrack);
        if (!remoteId.isEmpty()) {
            rememberRemoteId(remoteId);
            publishMixManPlayback(settings, pTrack, remoteId, QStringLiteral("playing"));
        }
    }
    if (!m_followCurrentTrack) {
        return;
    }
    refreshForTrack(pTrack, false, false);
}

void RestLibraryFeature::slotCurrentPlayingDeckChanged(int deck) {
    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(m_pConfig);
    if (!settings.isConfigured() ||
            !settings.useMixManDefaults ||
            m_mixManSession.id.isEmpty()) {
        return;
    }
    const TrackPointer pTrack = PlayerInfo::instance().getCurrentPlayingTrack();
    QString remoteId = remoteIdForTrack(pTrack);
    if (remoteId.isEmpty()) {
        remoteId = m_currentRemoteId;
    }
    if (remoteId.isEmpty()) {
        return;
    }
    publishMixManPlayback(
            settings,
            pTrack,
            remoteId,
            deck >= 0 ? QStringLiteral("playing") : QStringLiteral("paused"));
}

void RestLibraryFeature::refreshForTrack(
        const TrackPointer& pTrack, bool force, bool publishPlayback) {
    const RestLibrarySettings settings =
            RestLibrarySettings::fromConfig(m_pConfig);
    refreshMixManControls(settings);
    if (!settings.isConfigured()) {
        resetMixManSessionState();
        m_cacheManager.abortAll();
        m_pTableModel->setCacheLoadCapabilitiesEnabled(false);
        clearRecommendations();
        m_lastRequestedTrackLocation.clear();
        m_currentRemoteId.clear();
        setStatusText(tr("REST Library is not configured."));
        kLogger.info() << "REST library is not configured";
        return;
    }

    m_cacheManager.abortAll();
    m_pTableModel->setCacheLoadCapabilitiesEnabled(
            settings.hasAudioDownloadConfigured());
    if (settings.useMixManDefaults) {
        const QString sessionConfigKey = mixManSessionConfigKey(settings);
        if (m_mixManSessionConfigKey != sessionConfigKey) {
            resetMixManSessionState();
            m_mixManSessionConfigKey = sessionConfigKey;
        }
        ensureMixManSession(settings);
        m_client.fetchMixManDiagnostics(settings);
        m_client.fetchMixManPolicyPresets(settings);
    } else {
        resetMixManSessionState();
    }

    const QString trackLocation =
            pTrack ? normalizedTrackLocation(pTrack->getLocation()) : QString();
    if (!force && trackLocation == m_lastRequestedTrackLocation) {
        return;
    }
    m_lastRequestedTrackLocation = trackLocation;

    if (!pTrack) {
        m_currentRemoteId.clear();
        clearRecommendations();
        setStatusText(tr(
                "No current track. Play or select a track to load recommendations."));
        return;
    }

    const QString remoteId = remoteIdForTrack(pTrack);
    if (!remoteId.isEmpty()) {
        rememberRemoteId(remoteId);
        if (publishPlayback) {
            publishMixManPlayback(settings, pTrack, remoteId, QStringLiteral("playing"));
        }
        if (settings.useMixManDefaults && !m_mixManSession.id.isEmpty()) {
            setStatusText(tr("Loading MixMan authoritative recommendations."));
            m_client.fetchMixManSession(settings, m_mixManSession.id);
        } else {
            requestRecommendationsForRemoteId(settings, remoteId);
        }
        return;
    }

    if (settings.hasTrackLookupConfigured()) {
        m_currentRemoteId.clear();
        clearRecommendations();
        setStatusText(tr("Looking up the current track in the REST Library."));
        m_client.lookupTrack(settings, pTrack);
        return;
    }

    m_currentRemoteId.clear();
    clearRecommendations();
    setStatusText(tr("Current track is not mapped to a REST Library track."));
    kLogger.info() << "Current track is not mapped to a REST library remote id";
}

void RestLibraryFeature::slotTracksFetched(const QList<RestLibraryTrack>& tracks) {
    setRecommendationTracks(tracks);
}

void RestLibraryFeature::slotTrackLookupSucceeded(const QString& remoteId) {
    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(m_pConfig);
    rememberRemoteId(remoteId);
    publishMixManPlayback(
            settings,
            PlayerInfo::instance().getCurrentPlayingTrack(),
            remoteId,
            QStringLiteral("playing"));
    if (settings.useMixManDefaults && !m_mixManSession.id.isEmpty()) {
        m_client.fetchMixManSession(settings, m_mixManSession.id);
    } else {
        requestRecommendationsForRemoteId(settings, remoteId);
    }
}

void RestLibraryFeature::slotTrackLookupMissed(const QString& message) {
    m_currentRemoteId.clear();
    clearRecommendations();
    setStatusText(message.isEmpty()
                    ? tr("Current track was not found in the REST Library.")
                    : message);
    kLogger.info() << message;
}

void RestLibraryFeature::slotRecommendationsFetched(const QList<RestLibraryTrack>& tracks) {
    setRecommendationTracks(tracks);
}

void RestLibraryFeature::slotDiagnosticsUpdated(const RestLibraryDiagnostics& diagnostics) {
    if (diagnostics.healthKnown) {
        m_diagnostics.healthKnown = true;
        m_diagnostics.healthOk = diagnostics.healthOk;
        m_diagnostics.healthError = diagnostics.healthError;
    }
    if (diagnostics.indexKnown) {
        m_diagnostics.indexKnown = true;
        m_diagnostics.indexReady = diagnostics.indexReady;
        m_diagnostics.indexCount = diagnostics.indexCount;
        m_diagnostics.indexDimension = diagnostics.indexDimension;
        m_diagnostics.indexError = diagnostics.indexError;
    }
    if (diagnostics.lastStatusCode > 0) {
        m_diagnostics.lastStatusCode = diagnostics.lastStatusCode;
    }
    if (!m_diagnostics.healthError.isEmpty()) {
        m_diagnostics.lastError = m_diagnostics.healthError;
    } else if (!m_diagnostics.indexError.isEmpty()) {
        m_diagnostics.lastError = m_diagnostics.indexError;
    } else if (diagnostics.healthKnown || diagnostics.indexKnown) {
        m_diagnostics.lastError.clear();
    }
    updateDiagnosticsText();
}

void RestLibraryFeature::slotPolicyPresetsFetched(
        const QList<RestLibraryPolicyPreset>& presets) {
    if (!m_pRestLibraryView) {
        return;
    }
    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(m_pConfig);
    m_pRestLibraryView->setPolicyPresets(presets, settings.mixManPolicyPreset);
}

void RestLibraryFeature::slotMixManPolicyPathFetched(const RestLibraryPolicyPath& policyPath) {
    setPathSummary(policyPath);
    setRecommendationTracks(policyPath.candidates);
}

void RestLibraryFeature::slotMixManSessionCreated(const RestLibrarySession& session) {
    m_mixManSession = session;
    m_sessionStatusText = session.displayName.isEmpty()
            ? tr("Publishing session %1").arg(session.id)
            : tr("Publishing session %1").arg(session.displayName);
    updateDiagnosticsText();

    if (!m_sessionHeartbeatTimer.isActive()) {
        m_sessionHeartbeatTimer.start();
    }

    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(m_pConfig);
    updateMixManIntent(settings);
    if (!m_currentRemoteId.isEmpty()) {
        const int currentPlayingDeck = PlayerInfo::instance().getCurrentPlayingDeck();
        publishMixManPlayback(
                settings,
                PlayerInfo::instance().getCurrentPlayingTrack(),
                m_currentRemoteId,
                currentPlayingDeck >= 0 ? QStringLiteral("playing")
                                        : QStringLiteral("loaded"));
        m_client.fetchMixManSession(settings, m_mixManSession.id);
    }
    m_client.sendMixManSessionHeartbeat(
            settings,
            m_mixManSession.id,
            m_clientId,
            mixManSessionMetadata());
}

void RestLibraryFeature::slotMixManSessionFetched(const RestLibrarySession& session) {
    if (!session.id.isEmpty()) {
        m_mixManSession.id = session.id;
        m_mixManSession.displayName = session.displayName;
        m_mixManSession.status = session.status;
    }
    if (session.authoritative.raw.isEmpty()) {
        return;
    }
    if (m_authoritativeState.revision > 0 &&
            session.authoritative.revision <= 0) {
        return;
    }
    if (m_authoritativeState.revision > 0 &&
            session.authoritative.revision > 0 &&
            session.authoritative.revision < m_authoritativeState.revision) {
        return;
    }

    m_authoritativeState = session.authoritative;
    setPathSummary(m_authoritativeState.policyPath);
    if (!m_authoritativeState.policyPath.candidates.isEmpty()) {
        setRecommendationTracks(m_authoritativeState.policyPath.candidates);
    }
    updateDiagnosticsText();
}

void RestLibraryFeature::slotMixManSessionContractVerified(
        const RestLibrarySessionContract& contract) {
    if (!contract.valid) {
        return;
    }
    m_playbackLeaseRenewTimer.setInterval(
            std::max(1000, contract.leaseRenewIntervalSeconds * 1000));
    m_playbackLeaseReleaseTimer.setInterval(
            std::max(1000, contract.pauseGraceSeconds * 1000));
}

void RestLibraryFeature::slotMixManSessionWriteStatusUpdated(
        const RestLibrarySessionWriteStatus& status) {
    if (status.operation.isEmpty()) {
        return;
    }
    if (status.operation == kPlaybackControlClaimOperation) {
        m_playbackControlClaimPending = false;
        m_playbackLeaseOwned = status.success;
        if (status.success) {
            flushMixManPlaybackMutations(RestLibrarySettings::fromConfig(m_pConfig));
        } else {
            m_hasPendingPlayback = false;
            m_pendingCandidateTrackId.clear();
            m_pendingCandidateSelectionOrigin.clear();
            m_pendingCandidateMetadata = {};
            m_pendingCandidateAllowExternal = false;
        }
    } else if (status.operation == kPlaybackControlRenewOperation) {
        if (!status.success) {
            m_playbackLeaseOwned = false;
            m_playbackLeaseRenewTimer.stop();
        }
    } else if (status.operation == kPlaybackControlReleaseOperation) {
        m_playbackControlReleasePending = false;
        if (status.success || status.statusCode == 409) {
            m_playbackLeaseOwned = false;
        }
        m_playbackLeaseRenewTimer.stop();
        if (m_hasPendingPlayback || !m_pendingCandidateTrackId.isEmpty()) {
            ensureMixManPlaybackControl(RestLibrarySettings::fromConfig(m_pConfig));
        }
    } else if (status.operation == kSessionPlaybackOperation &&
            !status.success && status.statusCode == 409) {
        m_playbackLeaseOwned = false;
        m_playbackLeaseRenewTimer.stop();
    }
    if (!status.success) {
        if (status.operation == kSessionCreateOperation) {
            m_sessionCreateAttempted = false;
            m_mixManSession = {};
            m_sessionHeartbeatTimer.stop();
        }
        if (status.statusCode == 409) {
            m_sessionStatusText = tr("MixMan control conflict; DJ action was not accepted.");
        } else {
            m_sessionStatusText = status.errorText.isEmpty()
                    ? tr("Session publishing failed")
                    : status.errorText;
        }
        updateDiagnosticsText();
        return;
    }
    if (!m_mixManSession.id.isEmpty()) {
        m_sessionStatusText = m_mixManSession.displayName.isEmpty()
                ? tr("Publishing session %1").arg(m_mixManSession.id)
                : tr("Publishing session %1").arg(m_mixManSession.displayName);
        updateDiagnosticsText();
    }
}

void RestLibraryFeature::slotRequestDiagnosticUpdated(
        const RestLibraryRequestDiagnostic& diagnostic) {
    if (diagnostic.success) {
        if (!m_requestDiagnosticText.isEmpty() &&
                (diagnostic.stage == tr("Health check") ||
                        diagnostic.stage == tr("Session contract") ||
                        diagnostic.stage == tr("Index status"))) {
            m_requestDiagnosticText.clear();
            updateDiagnosticsText();
        }
        return;
    }

    QStringList parts;
    const QString summary = conciseDiagnosticText(diagnostic);
    if (!summary.isEmpty()) {
        parts.append(summary);
    }
    if (diagnostic.statusCode > 0) {
        parts.append(tr("HTTP %1").arg(diagnostic.statusCode));
    }
    m_requestDiagnosticText = parts.join(QStringLiteral(" | "));
    updateDiagnosticsText();
}

void RestLibraryFeature::slotSessionHeartbeat() {
    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(m_pConfig);
    if (!settings.isConfigured() || !settings.useMixManDefaults || m_mixManSession.id.isEmpty()) {
        m_sessionHeartbeatTimer.stop();
        return;
    }
    m_client.sendMixManSessionHeartbeat(
            settings,
            m_mixManSession.id,
            m_clientId,
            mixManSessionMetadata());
}

void RestLibraryFeature::slotPlaybackLeaseRenew() {
    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(m_pConfig);
    if (!m_playbackLeaseOwned ||
            !settings.isConfigured() ||
            !settings.useMixManDefaults ||
            m_mixManSession.id.isEmpty()) {
        m_playbackLeaseRenewTimer.stop();
        return;
    }
    QJsonObject metadata = mixManSessionMetadata();
    metadata.insert(QStringLiteral("reason"), QStringLiteral("active Mixxx playback"));
    m_client.renewMixManPlaybackControl(
            settings,
            m_mixManSession.id,
            m_clientId,
            metadata);
}

void RestLibraryFeature::slotPlaybackLeaseRelease() {
    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(m_pConfig);
    if (!m_playbackLeaseOwned ||
            !settings.isConfigured() ||
            !settings.useMixManDefaults ||
            m_mixManSession.id.isEmpty()) {
        return;
    }
    QJsonObject metadata = mixManSessionMetadata();
    metadata.insert(QStringLiteral("reason"), QStringLiteral("Mixxx playback paused"));
    m_playbackControlReleasePending = true;
    m_client.releaseMixManPlaybackControl(
            settings,
            m_mixManSession.id,
            m_clientId,
            metadata);
}

void RestLibraryFeature::slotPolicyPresetChanged(const QString& presetKey) {
    if (presetKey.trimmed().isEmpty()) {
        return;
    }
    m_pConfig->setValue(config::kMixManPolicyPresetKey, presetKey.trimmed());
    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(m_pConfig);
    updateMixManIntent(settings);
    requestMixManPolicyRefresh(settings);
}

void RestLibraryFeature::slotTargetEnergyChanged(bool enabled, int energy) {
    m_pConfig->setValue(config::kMixManTargetEnergyEnabledKey, enabled);
    m_pConfig->setValue(config::kMixManTargetEnergyKey, energy);
    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(m_pConfig);
    updateMixManIntent(settings);
    requestMixManPolicyRefresh(settings);
}

void RestLibraryFeature::slotTargetColorChanged(bool enabled, const QString& color) {
    m_pConfig->setValue(config::kMixManTargetColorEnabledKey, enabled);
    m_pConfig->setValue(config::kMixManTargetColorKey, color.trimmed());
    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(m_pConfig);
    updateMixManIntent(settings);
    requestMixManPolicyRefresh(settings);
}

void RestLibraryFeature::slotTargetBpmChanged(bool enabled, int bpm) {
    m_pConfig->setValue(config::kMixManTargetBpmEnabledKey, enabled);
    m_pConfig->setValue(config::kMixManTargetBpmKey, bpm);
    requestMixManPolicyRefresh(RestLibrarySettings::fromConfig(m_pConfig));
}

void RestLibraryFeature::slotRerollRequested() {
    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(m_pConfig);
    if (!settings.useMixManDefaults) {
        slotRefresh();
        return;
    }
    requestMixManPolicyRefresh(settings);
}

void RestLibraryFeature::slotLoadTrackRequested(TrackPointer pTrack) {
    selectMixManCandidateForTrack(pTrack);
    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(m_pConfig);
    const QString remoteId = remoteIdForTrack(pTrack);
    if (!remoteId.isEmpty()) {
        rememberRemoteId(remoteId);
        publishMixManPlayback(settings, pTrack, remoteId, QStringLiteral("loaded"));
    }
    emit loadTrack(pTrack);
}

void RestLibraryFeature::slotLoadTrackToPlayerRequested(
        TrackPointer pTrack,
        const QString& group,
        bool play) {
    selectMixManCandidateForTrack(pTrack);
    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(m_pConfig);
    const QString remoteId = remoteIdForTrack(pTrack);
    if (!remoteId.isEmpty()) {
        rememberRemoteId(remoteId);
        publishMixManPlayback(
                settings,
                pTrack,
                remoteId,
                play ? QStringLiteral("playing") : QStringLiteral("loaded"));
    }
#ifdef __STEM__
    emit loadTrackToPlayer(pTrack, group, mixxx::StemChannelSelection(), play);
#else
    emit loadTrackToPlayer(pTrack, group, play);
#endif
}

void RestLibraryFeature::requestRecommendationsForRemoteId(
        const RestLibrarySettings& settings,
        const QString& remoteId) {
    if (!settings.hasRecommendationsConfigured()) {
        m_currentRemoteId.clear();
        clearRecommendations();
        setStatusText(tr("REST Library recommendations are not configured."));
        kLogger.info() << "REST library recommendations are not configured";
        return;
    }

    rememberRemoteId(remoteId);
    clearRecommendations();
    if (settings.useMixManDefaults) {
        setStatusText(tr("Loading MixMan policy recommendations."));
        m_client.fetchMixManPolicyPath(
                settings,
                remoteId,
                m_mixManSession.id,
                m_previousRemoteId,
                recentRemoteIdsForRequest(remoteId));
        return;
    }
    setStatusText(tr("Loading REST Library recommendations."));
    m_client.fetchRecommendations(settings, remoteId);
}

void RestLibraryFeature::rememberRemoteId(const QString& remoteId) {
    const QString normalizedRemoteId = remoteId.trimmed();
    if (normalizedRemoteId.isEmpty()) {
        m_currentRemoteId.clear();
        return;
    }
    if (normalizedRemoteId == m_currentRemoteId) {
        return;
    }
    if (!m_currentRemoteId.isEmpty()) {
        m_previousRemoteId = m_currentRemoteId;
        m_recentRemoteIds.removeAll(m_currentRemoteId);
        m_recentRemoteIds.prepend(m_currentRemoteId);
        while (m_recentRemoteIds.size() > kMaxRecentRemoteIds) {
            m_recentRemoteIds.removeLast();
        }
    }
    m_currentRemoteId = normalizedRemoteId;
}

QStringList RestLibraryFeature::recentRemoteIdsForRequest(const QString& remoteId) const {
    QStringList recentRemoteIds = m_recentRemoteIds;
    recentRemoteIds.removeAll(remoteId.trimmed());
    return recentRemoteIds;
}

void RestLibraryFeature::setRecommendationTracks(const QList<RestLibraryTrack>& tracks) {
    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(m_pConfig);
    m_pTableModel->setTracks(tracks);
    m_cacheStates.clear();
    m_recommendationCount = tracks.size();
    for (const auto& track : tracks) {
        if (!track.remoteId.isEmpty()) {
            m_cacheStates.insert(track.remoteId, track.cacheState);
        }
    }
    double qualityTotal = 0.0;
    int qualityCount = 0;
    for (const auto& track : tracks) {
        if (track.quality > 0.0) {
            qualityTotal += track.quality;
            ++qualityCount;
        }
    }
    m_averageQuality = qualityCount > 0 ? qualityTotal / qualityCount : 0.0;
    updateReadyStatus();
    if (!settings.hasAudioDownloadConfigured()) {
        return;
    }

    m_cacheManager.reconcileTracks(tracks, settings);

    QList<RestLibraryTrack> tracksToCache;
    const int cacheLimit = std::min(settings.recommendationLimit, static_cast<int>(tracks.size()));
    tracksToCache.reserve(cacheLimit);
    for (int i = 0; i < cacheLimit; ++i) {
        tracksToCache.append(tracks.at(i));
    }
    m_cacheManager.cacheTracks(tracksToCache, settings);
}

void RestLibraryFeature::ensureMixManSession(const RestLibrarySettings& settings) {
    if (!settings.isConfigured() ||
            !settings.useMixManDefaults ||
            m_sessionCreateAttempted ||
            !m_mixManSession.id.isEmpty()) {
        return;
    }
    m_sessionCreateAttempted = true;
    m_sessionStatusText = tr("Creating MixMan session");
    updateDiagnosticsText();
    m_client.createMixManSession(settings, m_clientId, mixManSessionMetadata());
}

void RestLibraryFeature::resetMixManSessionState() {
    m_client.invalidateMixManRequests();
    m_mixManSession = {};
    m_authoritativeState = {};
    m_sessionCreateAttempted = false;
    m_sessionStatusText.clear();
    m_requestDiagnosticText.clear();
    m_mixManSessionConfigKey.clear();
    m_sessionHeartbeatTimer.stop();
    m_playbackLeaseRenewTimer.stop();
    m_playbackLeaseReleaseTimer.stop();
    m_playbackControlClaimPending = false;
    m_playbackControlReleasePending = false;
    m_playbackLeaseOwned = false;
    m_hasPendingPlayback = false;
    m_pendingCandidateTrackId.clear();
    m_pendingCandidateSelectionOrigin.clear();
    m_pendingCandidateMetadata = {};
    m_pendingCandidateAllowExternal = false;
    updateDiagnosticsText();
}

QString RestLibraryFeature::mixManSessionConfigKey(const RestLibrarySettings& settings) const {
    return settings.baseUrl.toString(QUrl::RemoveUserInfo);
}

void RestLibraryFeature::publishMixManSnapshot(
        const RestLibrarySettings& settings,
        const TrackPointer& pTrack,
        const QString& remoteId) {
    if (!settings.isConfigured() ||
            !settings.useMixManDefaults ||
            m_mixManSession.id.isEmpty()) {
        return;
    }

    RestLibrarySessionSnapshot snapshot;
    snapshot.clientId = m_clientId;
    snapshot.source = QStringLiteral("mixxx");
    snapshot.surface = QStringLiteral("rest_library");
    snapshot.currentTrackId = remoteId;
    const int currentPlayingDeck = PlayerInfo::instance().getCurrentPlayingDeck();
    snapshot.playbackState = currentPlayingDeck >= 0
            ? QStringLiteral("playing")
            : (pTrack ? QStringLiteral("loaded") : QStringLiteral("idle"));
    snapshot.snapshot = mixManTrackSnapshot(pTrack, remoteId);
    snapshot.metadata = mixManSessionMetadata();
    m_client.publishMixManSessionSnapshot(settings, m_mixManSession.id, snapshot);
}

void RestLibraryFeature::publishMixManPlayback(
        const RestLibrarySettings& settings,
        const TrackPointer& pTrack,
        const QString& remoteId,
        const QString& playbackState) {
    if (!settings.isConfigured() ||
            !settings.useMixManDefaults ||
            m_mixManSession.id.isEmpty()) {
        return;
    }

    RestLibrarySessionPlayback playback;
    playback.clientId = m_clientId;
    playback.source = QStringLiteral("mixxx");
    playback.surface = QStringLiteral("rest_library");
    playback.currentTrackId = remoteId;
    playback.playbackState = playbackState;
    playback.cue = QStringLiteral("library");
    playback.metadata = mixManSessionMetadata();
    playback.metadata.insert(QStringLiteral("schema_version"), QStringLiteral("session_snapshot_v1"));

    bool ok = false;
    const int trackId = remoteId.toInt(&ok);
    if (ok && trackId > 0) {
        playback.currentTrack.insert(QStringLiteral("track_id"), trackId);
    }
    if (pTrack) {
        playback.currentTrack.insert(QStringLiteral("title"), pTrack->getTitle());
        playback.currentTrack.insert(QStringLiteral("artist"), pTrack->getArtist());
        playback.currentTrack.insert(QStringLiteral("album"), pTrack->getAlbum());
        playback.currentTrack.insert(QStringLiteral("genre"), pTrack->getGenre());
        playback.currentTrack.insert(QStringLiteral("bpm"), pTrack->getBpm());
        playback.currentTrack.insert(QStringLiteral("key"), pTrack->getKeyText());
        playback.currentTrack.insert(QStringLiteral("duration"), pTrack->getDuration());
    }

    const int currentPlayingDeck = PlayerInfo::instance().getCurrentPlayingDeck();
    if (currentPlayingDeck >= 0) {
        playback.metadata.insert(QStringLiteral("deck_index"), currentPlayingDeck);
        playback.metadata.insert(QStringLiteral("deck"), currentPlayingDeck + 1);
        playback.metadata.insert(
                QStringLiteral("player_group"),
                PlayerManager::groupForDeck(currentPlayingDeck));
    }

    m_pendingPlayback = playback;
    m_hasPendingPlayback = true;
    publishMixManSnapshot(settings, pTrack, remoteId);
    ensureMixManPlaybackControl(settings);
}

void RestLibraryFeature::ensureMixManPlaybackControl(
        const RestLibrarySettings& settings) {
    if (!settings.isConfigured() ||
            !settings.useMixManDefaults ||
            m_mixManSession.id.isEmpty()) {
        return;
    }
    if (m_playbackControlReleasePending) {
        return;
    }
    if (m_playbackLeaseOwned) {
        flushMixManPlaybackMutations(settings);
        return;
    }
    if (m_playbackControlClaimPending) {
        return;
    }
    m_playbackControlClaimPending = true;
    QJsonObject metadata = mixManSessionMetadata();
    metadata.insert(QStringLiteral("reason"), QStringLiteral("mixxx dj action"));
    m_client.claimMixManPlaybackControl(
            settings,
            m_mixManSession.id,
            m_clientId,
            metadata);
}

void RestLibraryFeature::flushMixManPlaybackMutations(
        const RestLibrarySettings& settings) {
    if (!m_playbackLeaseOwned ||
            m_playbackControlReleasePending ||
            m_mixManSession.id.isEmpty()) {
        return;
    }
    if (!m_pendingCandidateTrackId.isEmpty()) {
        const QString trackId = m_pendingCandidateTrackId;
        const QString selectionOrigin = m_pendingCandidateSelectionOrigin;
        const bool allowExternalCandidate = m_pendingCandidateAllowExternal;
        const QJsonObject metadata = m_pendingCandidateMetadata;
        m_pendingCandidateTrackId.clear();
        m_pendingCandidateSelectionOrigin.clear();
        m_pendingCandidateMetadata = {};
        m_pendingCandidateAllowExternal = false;
        m_client.selectMixManSessionCandidate(
                settings,
                m_mixManSession.id,
                trackId,
                m_clientId,
                selectionOrigin,
                allowExternalCandidate,
                metadata);
    }
    if (!m_hasPendingPlayback) {
        return;
    }
    const RestLibrarySessionPlayback playback = m_pendingPlayback;
    m_hasPendingPlayback = false;
    m_client.publishMixManSessionPlayback(settings, m_mixManSession.id, playback);
    if (playback.playbackState == QStringLiteral("playing")) {
        m_playbackLeaseReleaseTimer.stop();
        if (!m_playbackLeaseRenewTimer.isActive()) {
            m_playbackLeaseRenewTimer.start();
        }
    } else {
        m_playbackLeaseRenewTimer.stop();
        m_playbackLeaseReleaseTimer.start();
    }
}

void RestLibraryFeature::selectMixManCandidateForTrack(const TrackPointer& pTrack) {
    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(m_pConfig);
    if (!settings.isConfigured() ||
            !settings.useMixManDefaults ||
            m_mixManSession.id.isEmpty()) {
        return;
    }

    const QString remoteId = remoteIdForTrack(pTrack);
    if (remoteId.isEmpty()) {
        return;
    }

    const QString selectionOrigin = selectionOriginForRemoteId(remoteId);
    const bool allowExternalCandidate =
            selectionOrigin != QStringLiteral("authoritative_candidate");
    QJsonObject metadata = mixManSessionMetadata();
    metadata.insert(
            QStringLiteral("reason"),
            allowExternalCandidate ? QStringLiteral("DJ loaded reroll result")
                                   : QStringLiteral("DJ loaded authoritative candidate"));
    m_pendingCandidateTrackId = remoteId;
    m_pendingCandidateSelectionOrigin = selectionOrigin;
    m_pendingCandidateAllowExternal = allowExternalCandidate;
    m_pendingCandidateMetadata = metadata;
    ensureMixManPlaybackControl(settings);
}

void RestLibraryFeature::requestMixManPolicyRefresh(const RestLibrarySettings& settings) {
    if (!settings.isConfigured() ||
            !settings.useMixManDefaults ||
            m_mixManSession.id.isEmpty()) {
        slotRefresh();
        return;
    }

    QJsonObject metadata = mixManSessionMetadata();
    metadata.insert(QStringLiteral("reason"), QStringLiteral("DJ requested policy refresh"));
    m_client.publishMixManPolicyRefreshAction(
            settings,
            m_mixManSession.id,
            m_clientId,
            metadata);
}

QString RestLibraryFeature::selectionOriginForRemoteId(const QString& remoteId) const {
    const QString normalizedRemoteId = remoteId.trimmed();
    if (normalizedRemoteId.isEmpty()) {
        return QStringLiteral("manual");
    }
    for (const RestLibraryTrack& candidate : m_authoritativeState.policyPath.candidates) {
        if (candidate.remoteId == normalizedRemoteId) {
            return QStringLiteral("authoritative_candidate");
        }
    }
    return QStringLiteral("recommendation_reroll");
}

void RestLibraryFeature::updateMixManIntent(const RestLibrarySettings& settings) {
    if (!settings.isConfigured() ||
            !settings.useMixManDefaults ||
            m_mixManSession.id.isEmpty()) {
        return;
    }

    RestLibrarySessionIntent intent;
    intent.clientId = m_clientId;
    intent.source = QStringLiteral("mixxx");
    intent.surface = QStringLiteral("rest_library");
    const bool hasTargetEnergy = settings.mixManTargetEnergyEnabled;
    const bool hasTargetColor = settings.mixManTargetColorEnabled &&
            !settings.mixManTargetColor.trimmed().isEmpty();
    intent.status = hasTargetEnergy || hasTargetColor ? QStringLiteral("active")
                                                      : QStringLiteral("cleared");
    intent.policyPreset = settings.mixManPolicyPreset;
    intent.targetEnergyEnabled = settings.mixManTargetEnergyEnabled;
    intent.targetEnergy = settings.mixManTargetEnergyNormalized();
    intent.targetColorEnabled = settings.mixManTargetColorEnabled;
    intent.targetColor = settings.mixManTargetColor;
    intent.metadata = mixManSessionMetadata();
    m_client.updateMixManSessionIntent(settings, m_mixManSession.id, intent);
}

QJsonObject RestLibraryFeature::mixManSessionMetadata() const {
    QJsonObject metadata;
    metadata.insert(QStringLiteral("client"), QStringLiteral("mixxx"));
    metadata.insert(QStringLiteral("surface"), QStringLiteral("rest_library"));
    metadata.insert(QStringLiteral("feature"), QStringLiteral("REST Library"));
    return metadata;
}

QJsonObject RestLibraryFeature::mixManTrackSnapshot(
        const TrackPointer& pTrack,
        const QString& remoteId) const {
    QJsonObject snapshot;
    if (!remoteId.trimmed().isEmpty()) {
        snapshot.insert(QStringLiteral("current_remote_id"), remoteId.trimmed());
    }
    if (!m_previousRemoteId.trimmed().isEmpty()) {
        snapshot.insert(QStringLiteral("previous_track_id"), m_previousRemoteId.trimmed());
    }
    const QStringList recentRemoteIds = recentRemoteIdsForRequest(remoteId);
    if (!recentRemoteIds.isEmpty()) {
        QJsonArray recentTrackIds;
        for (const QString& recentRemoteId : recentRemoteIds) {
            recentTrackIds.append(recentRemoteId);
        }
        snapshot.insert(QStringLiteral("recent_track_ids"), recentTrackIds);
    }

    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(m_pConfig);
    QJsonObject policy;
    policy.insert(QStringLiteral("policy_preset"), settings.mixManPolicyPreset);
    policy.insert(QStringLiteral("admin_approved_only"), settings.mixManAdminApprovedOnly);
    policy.insert(QStringLiteral("path_depth"), settings.mixManPathDepth);
    policy.insert(QStringLiteral("candidate_limit"), settings.recommendationLimit);
    if (settings.mixManTargetEnergyEnabled) {
        policy.insert(QStringLiteral("target_energy"), settings.mixManTargetEnergyNormalized());
    }
    if (settings.mixManTargetColorEnabled && !settings.mixManTargetColor.trimmed().isEmpty()) {
        policy.insert(QStringLiteral("target_color"), settings.mixManTargetColor.trimmed());
    }
    if (settings.mixManTargetBpmEnabled) {
        policy.insert(QStringLiteral("target_bpm"), settings.mixManTargetBpm);
    }
    snapshot.insert(QStringLiteral("policy"), policy);

    const int currentPlayingDeck = PlayerInfo::instance().getCurrentPlayingDeck();
    QJsonObject playback;
    playback.insert(
            QStringLiteral("state"),
            currentPlayingDeck >= 0
                    ? QStringLiteral("playing")
                    : (pTrack ? QStringLiteral("loaded") : QStringLiteral("idle")));
    if (currentPlayingDeck >= 0) {
        playback.insert(QStringLiteral("deck_index"), currentPlayingDeck);
        playback.insert(QStringLiteral("deck"), currentPlayingDeck + 1);
        playback.insert(
                QStringLiteral("player_group"),
                PlayerManager::groupForDeck(currentPlayingDeck));
    }
    snapshot.insert(QStringLiteral("playback"), playback);

    if (!pTrack) {
        return snapshot;
    }

    QJsonObject track;
    track.insert(QStringLiteral("title"), pTrack->getTitle());
    track.insert(QStringLiteral("artist"), pTrack->getArtist());
    track.insert(QStringLiteral("album"), pTrack->getAlbum());
    track.insert(QStringLiteral("genre"), pTrack->getGenre());
    track.insert(QStringLiteral("bpm"), pTrack->getBpm());
    track.insert(QStringLiteral("key"), pTrack->getKeyText());
    track.insert(QStringLiteral("duration"), pTrack->getDuration());
    if (pTrack->getRating() > 0) {
        track.insert(QStringLiteral("rating"), pTrack->getRating());
    }
    snapshot.insert(QStringLiteral("current_track"), track);
    return snapshot;
}

void RestLibraryFeature::refreshMixManControls(const RestLibrarySettings& settings) {
    if (!m_pRestLibraryView) {
        return;
    }
    m_pRestLibraryView->setMixManTargets(
            settings.mixManTargetEnergyEnabled,
            settings.mixManTargetEnergy,
            settings.mixManTargetColorEnabled,
            settings.mixManTargetColor,
            settings.mixManTargetBpmEnabled,
            settings.mixManTargetBpm);
}

void RestLibraryFeature::updateDiagnosticsText() {
    if (!m_pRestLibraryView) {
        return;
    }
    QStringList parts;
    if (m_diagnostics.healthKnown) {
        parts.append(m_diagnostics.healthOk ? tr("API online") : tr("API offline"));
    }
    if (m_diagnostics.indexKnown) {
        parts.append(m_diagnostics.indexReady
                        ? tr("Index ready: %1 tracks").arg(m_diagnostics.indexCount)
                        : tr("Index building: %1 tracks").arg(m_diagnostics.indexCount));
    }
    if (!m_diagnostics.lastError.isEmpty()) {
        parts.append(m_diagnostics.lastError);
    }
    if (!m_requestDiagnosticText.isEmpty() &&
            m_requestDiagnosticText != m_diagnostics.lastError) {
        parts.append(m_requestDiagnosticText);
    }
    if (!m_sessionStatusText.isEmpty()) {
        parts.append(m_sessionStatusText);
    }
    if (m_authoritativeState.revision > 0) {
        parts.append(tr("Authoritative rev %1").arg(m_authoritativeState.revision));
    }
    if (!m_authoritativeState.playbackController.isEmpty()) {
        const QString controllerId =
                m_authoritativeState.playbackController
                        .value(QStringLiteral("client_id"))
                        .toString();
        const QString controllerKind =
                m_authoritativeState.playbackController
                        .value(QStringLiteral("client_kind"))
                        .toString();
        if (!controllerId.isEmpty()) {
            parts.append(controllerKind.isEmpty()
                            ? tr("Controller: %1").arg(controllerId)
                            : tr("Controller: %1 (%2)").arg(controllerId, controllerKind));
        }
    }
    if (!m_authoritativeState.intents.isEmpty()) {
        parts.append(tr("%n MixMan intent(s)", nullptr, m_authoritativeState.intents.size()));
    }
    if (!m_authoritativeState.blocked.isEmpty()) {
        parts.append(tr("Blocked MixMan intent"));
    }
    if (!m_authoritativeState.pressureState.isEmpty()) {
        parts.append(tr("Pressure active"));
    }
    m_pRestLibraryView->setDiagnosticsText(parts.join(QStringLiteral(" | ")));
}

void RestLibraryFeature::setPathSummary(const RestLibraryPolicyPath& policyPath) {
    if (!m_pRestLibraryView) {
        return;
    }
    if (policyPath.path.isEmpty()) {
        m_pRestLibraryView->setPathSummaryText({});
        return;
    }

    QStringList labels;
    const int labelLimit = std::min(3, static_cast<int>(policyPath.path.size()));
    labels.reserve(labelLimit);
    for (int i = 0; i < labelLimit; ++i) {
        const auto& step = policyPath.path.at(i);
        labels.append(step.title.isEmpty() ? step.remoteId : step.title);
    }
    QString summary = tr("Path: %1 step(s)").arg(policyPath.path.size());
    if (!labels.isEmpty()) {
        summary += QStringLiteral(" - ") + labels.join(QStringLiteral(" -> "));
    }
    if (policyPath.selectedBranchScore > 0.0) {
        summary += tr(" (%1)").arg(policyPath.selectedBranchScore, 0, 'f', 2);
    }
    m_pRestLibraryView->setPathSummaryText(summary);
}

void RestLibraryFeature::slotFetchFailed(const QString& message) {
    clearRecommendations();
    setStatusText(message.isEmpty()
                    ? tr("REST Library request failed.")
                    : message);
    kLogger.warning() << message;
}

void RestLibraryFeature::slotTrackCacheStateChanged(const RestLibraryCacheResult& result) {
    if (result.cacheState == RestLibraryCacheState::Ready &&
            !result.cachedFilePath.trimmed().isEmpty()) {
        m_cachedPathToRemoteId.insert(
                normalizedTrackLocation(result.cachedFilePath),
                result.remoteId);
    }
    const bool isDisplayedTrack =
            !result.remoteId.isEmpty() && m_cacheStates.contains(result.remoteId);
    if (isDisplayedTrack) {
        m_cacheStates.insert(result.remoteId, result.cacheState);
    }
    m_pTableModel->updateTrackCacheState(result);
    if (isDisplayedTrack) {
        updateReadyStatus();
    }
}

void RestLibraryFeature::setStatusText(const QString& statusText) {
    if (m_statusText == statusText) {
        return;
    }
    m_statusText = statusText;
    emit statusTextChanged(m_statusText);
}

void RestLibraryFeature::updateReadyStatus() {
    if (m_recommendationCount == 0) {
        setStatusText(tr("No REST Library recommendations found."));
        return;
    }

    int readyCount = 0;
    int downloadingCount = 0;
    int failedCount = 0;
    for (auto it = m_cacheStates.cbegin(); it != m_cacheStates.cend(); ++it) {
        switch (it.value()) {
        case RestLibraryCacheState::Ready:
            ++readyCount;
            break;
        case RestLibraryCacheState::Downloading:
            ++downloadingCount;
            break;
        case RestLibraryCacheState::Failed:
            ++failedCount;
            break;
        case RestLibraryCacheState::Missing:
        case RestLibraryCacheState::Stale:
            break;
        }
    }

    if (downloadingCount > 0) {
        setStatusText(tr("%n REST Library recommendation(s). Caching %1 download(s).",
                              nullptr,
                              m_recommendationCount)
                              .arg(downloadingCount));
        return;
    }
    if (readyCount > 0 || failedCount > 0) {
        setStatusText(tr("%n REST Library recommendation(s). %1 cached, %2 failed.",
                              nullptr,
                              m_recommendationCount)
                              .arg(readyCount)
                              .arg(failedCount));
        return;
    }
    if (m_averageQuality > 0.0) {
        setStatusText(tr("%n REST Library recommendation(s). Avg quality %1.",
                              nullptr,
                              m_recommendationCount)
                              .arg(m_averageQuality, 0, 'f', 2));
        return;
    }
    setStatusText(tr("%n REST Library recommendation(s).", nullptr, m_recommendationCount));
}

void RestLibraryFeature::clearRecommendations() {
    m_pTableModel->setTracks({});
    m_cacheStates.clear();
    m_recommendationCount = 0;
    m_averageQuality = 0.0;
    if (m_pRestLibraryView) {
        m_pRestLibraryView->setPathSummaryText({});
    }
}

QString RestLibraryFeature::remoteIdForTrack(const TrackPointer& pTrack) const {
    if (!pTrack) {
        return {};
    }
    return m_cachedPathToRemoteId.value(normalizedTrackLocation(pTrack->getLocation()));
}

QString RestLibraryFeature::normalizedTrackLocation(const QString& location) {
    return QDir::cleanPath(QDir::fromNativeSeparators(location)).toCaseFolded();
}

} // namespace mixxx::library::rest
