#include "library/rest/restlibraryfeature.h"

#include <algorithm>

#include <QDir>
#include <QMenu>

#include "library/library.h"
#include "library/rest/restlibrarysettings.h"
#include "library/treeitem.h"
#include "mixer/playerinfo.h"
#include "moc_restlibraryfeature.cpp"
#include "track/track.h"
#include "util/logger.h"

namespace mixxx::library::rest {

namespace {

const Logger kLogger("RestLibraryFeature");

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
    return tr("Recommendations");
}

TreeItemModel* RestLibraryFeature::sidebarModel() const {
    return m_pSidebarModel;
}

void RestLibraryFeature::bindLibraryWidget(
        WLibrary* pLibraryWidget,
        KeyboardEventFilter* pKeyboard) {
    Q_UNUSED(pLibraryWidget);
    Q_UNUSED(pKeyboard);
    connect(&PlayerInfo::instance(),
            &PlayerInfo::currentPlayingTrackChanged,
            this,
            &RestLibraryFeature::slotCurrentPlayingTrackChanged);
}

void RestLibraryFeature::activate() {
    emit saveModelState();
    emit showTrackModel(m_pTableModel);
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

void RestLibraryFeature::slotCurrentPlayingTrackChanged(TrackPointer pTrack) {
    refreshForTrack(pTrack, false);
}

void RestLibraryFeature::refreshForTrack(const TrackPointer& pTrack, bool force) {
    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(m_pConfig);
    if (!settings.isConfigured()) {
        m_cacheManager.abortAll();
        m_pTableModel->setCacheLoadCapabilitiesEnabled(false);
        m_pTableModel->setTracks({});
        m_lastRequestedTrackLocation.clear();
        m_currentRemoteId.clear();
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
        m_pTableModel->setTracks({});
        return;
    }

    const QString remoteId = remoteIdForTrack(pTrack);
    if (!remoteId.isEmpty()) {
        requestRecommendationsForRemoteId(settings, remoteId);
        return;
    }

    if (settings.hasTrackLookupConfigured()) {
        m_currentRemoteId.clear();
        m_pTableModel->setTracks({});
        m_client.lookupTrack(settings, pTrack);
        return;
    }

    m_currentRemoteId.clear();
    m_pTableModel->setTracks({});
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
    m_pTableModel->setTracks({});
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
        m_pTableModel->setTracks({});
        kLogger.info() << "REST library recommendations are not configured";
        return;
    }

    m_currentRemoteId = remoteId;
    m_pTableModel->setTracks({});
    m_client.fetchRecommendations(settings, remoteId);
}

void RestLibraryFeature::setRecommendationTracks(const QList<RestLibraryTrack>& tracks) {
    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(m_pConfig);
    m_pTableModel->setTracks(tracks);
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
    m_pTableModel->setTracks({});
    kLogger.warning() << message;
}

void RestLibraryFeature::slotTrackCacheStateChanged(const RestLibraryCacheResult& result) {
    if (result.cacheState == RestLibraryCacheState::Ready &&
            !result.cachedFilePath.trimmed().isEmpty()) {
        m_cachedPathToRemoteId.insert(
                normalizedTrackLocation(result.cachedFilePath),
                result.remoteId);
    }
    m_pTableModel->updateTrackCacheState(result);
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
