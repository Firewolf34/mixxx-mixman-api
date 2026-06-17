#include "library/rest/restlibraryfeature.h"

#include <algorithm>

#include <QDir>
#include <QMenu>
#include <QStringList>

#include "controllers/keyboard/keyboardeventfilter.h"
#include "library/library.h"
#include "library/rest/dlgrestlibrary.h"
#include "library/rest/restlibrarysettings.h"
#include "library/treeitem.h"
#include "mixer/playerinfo.h"
#include "moc_restlibraryfeature.cpp"
#include "track/track.h"
#include "util/logger.h"
#include "widget/wlibrary.h"

namespace mixxx::library::rest {

namespace {

const Logger kLogger("RestLibraryFeature");
const QString kViewName = QStringLiteral("REST Library");

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
            &RestLibraryClient::fetchFailed,
            this,
            &RestLibraryFeature::slotFetchFailed);
    connect(&m_cacheManager,
            &RestLibraryCacheManager::trackCacheStateChanged,
            this,
            &RestLibraryFeature::slotTrackCacheStateChanged);
}

QVariant RestLibraryFeature::title() {
    return tr("REST Library");
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
            &RestLibraryFeature::loadTrack);
    connect(m_pRestLibraryView,
            &DlgRestLibrary::loadTrackToPlayer,
            this,
            &RestLibraryFeature::loadTrackToPlayer);
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
    connect(this,
            &RestLibraryFeature::statusTextChanged,
            m_pRestLibraryView,
            &DlgRestLibrary::setStatusText);

    connect(&PlayerInfo::instance(),
            &PlayerInfo::currentPlayingTrackChanged,
            this,
            &RestLibraryFeature::slotCurrentPlayingTrackChanged);

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
    if (!m_followCurrentTrack) {
        return;
    }
    refreshForTrack(pTrack, false);
}

void RestLibraryFeature::refreshForTrack(const TrackPointer& pTrack, bool force) {
    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(m_pConfig);
    refreshMixManControls(settings);
    if (!settings.isConfigured()) {
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
    m_pTableModel->setCacheLoadCapabilitiesEnabled(settings.hasAudioDownloadConfigured());
    if (settings.useMixManDefaults) {
        m_client.fetchMixManDiagnostics(settings);
        m_client.fetchMixManPolicyPresets(settings);
    }

    const QString trackLocation = pTrack ? normalizedTrackLocation(pTrack->getLocation()) : QString();
    if (!force && trackLocation == m_lastRequestedTrackLocation) {
        return;
    }
    m_lastRequestedTrackLocation = trackLocation;

    if (!pTrack) {
        m_currentRemoteId.clear();
        clearRecommendations();
        setStatusText(tr("No current track. Play or select a track to load recommendations."));
        return;
    }

    const QString remoteId = remoteIdForTrack(pTrack);
    if (!remoteId.isEmpty()) {
        requestRecommendationsForRemoteId(settings, remoteId);
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
    requestRecommendationsForRemoteId(settings, remoteId);
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
    }
    if (diagnostics.indexKnown) {
        m_diagnostics.indexKnown = true;
        m_diagnostics.indexReady = diagnostics.indexReady;
        m_diagnostics.indexCount = diagnostics.indexCount;
        m_diagnostics.indexDimension = diagnostics.indexDimension;
    }
    if (diagnostics.lastStatusCode > 0) {
        m_diagnostics.lastStatusCode = diagnostics.lastStatusCode;
    }
    if (diagnostics.healthKnown || diagnostics.indexKnown) {
        m_diagnostics.lastError = diagnostics.lastError;
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

void RestLibraryFeature::slotPolicyPresetChanged(const QString& presetKey) {
    if (presetKey.trimmed().isEmpty()) {
        return;
    }
    m_pConfig->setValue(config::kMixManPolicyPresetKey, presetKey.trimmed());
    slotRefresh();
}

void RestLibraryFeature::slotTargetEnergyChanged(bool enabled, int energy) {
    m_pConfig->setValue(config::kMixManTargetEnergyEnabledKey, enabled);
    m_pConfig->setValue(config::kMixManTargetEnergyKey, energy);
    slotRefresh();
}

void RestLibraryFeature::slotTargetColorChanged(bool enabled, const QString& color) {
    m_pConfig->setValue(config::kMixManTargetColorEnabledKey, enabled);
    m_pConfig->setValue(config::kMixManTargetColorKey, color.trimmed());
    slotRefresh();
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

    m_currentRemoteId = remoteId;
    clearRecommendations();
    if (settings.useMixManDefaults) {
        setStatusText(tr("Loading MixMan policy recommendations."));
        m_client.fetchMixManPolicyPath(settings, remoteId);
        return;
    }
    setStatusText(tr("Loading REST Library recommendations."));
    m_client.fetchRecommendations(settings, remoteId);
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

void RestLibraryFeature::refreshMixManControls(const RestLibrarySettings& settings) {
    if (!m_pRestLibraryView) {
        return;
    }
    m_pRestLibraryView->setMixManTargets(
            settings.mixManTargetEnergyEnabled,
            settings.mixManTargetEnergy,
            settings.mixManTargetColorEnabled,
            settings.mixManTargetColor);
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
