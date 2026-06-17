#include "library/rest/restlibraryfeature.h"

#include <algorithm>

#include <QDir>
#include <QMenu>

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
        : LibraryFeature(pLibrary, std::move(pConfig), QStringLiteral("network-workgroup")),
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
    setStatusText(tr("%n REST Library recommendation(s).", nullptr, m_recommendationCount));
}

void RestLibraryFeature::clearRecommendations() {
    m_pTableModel->setTracks({});
    m_cacheStates.clear();
    m_recommendationCount = 0;
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
