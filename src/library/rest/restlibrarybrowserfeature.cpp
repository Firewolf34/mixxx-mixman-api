#include "library/rest/restlibrarybrowserfeature.h"

#include <algorithm>

#include <QMenu>
#include <QUrl>
#include <utility>

#include "control/controlobject.h"
#include "controllers/keyboard/keyboardeventfilter.h"
#include "library/library.h"
#include "library/rest/dlgrestlibrarybrowser.h"
#include "library/rest/restlibrarysettings.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "library/treeitem.h"
#include "mixer/playermanager.h"
#include "moc_restlibrarybrowserfeature.cpp"
#include "preferences/dialog/dlgprefdeck.h"
#include "track/track.h"
#include "util/logger.h"
#include "widget/wlibrary.h"

namespace mixxx::library::rest {

namespace {
const QString kViewName = QStringLiteral("REST Library");
const Logger kLogger("RestLibraryBrowserFeature");
}

RestLibraryBrowserFeature::RestLibraryBrowserFeature(
        Library* pLibrary,
        UserSettingsPointer pConfig,
        RestLibraryBackend* pBackend)
        : RestLibraryBrowserFeature(
                  pLibrary,
                  std::move(pConfig),
                  pBackend,
                  pLibrary->trackCollectionManager(),
                  pBackend->loudnessManager()) {
}

RestLibraryBrowserFeature::RestLibraryBrowserFeature(
        Library* pLibrary,
        UserSettingsPointer pConfig,
        RestLibraryBackend* pBackend,
        TrackCollectionManager* pTrackCollectionManager,
        RestLibraryLoudnessManager* pLoudnessManager)
        : LibraryFeature(pLibrary, std::move(pConfig), QStringLiteral("computer")),
          m_pSidebarModel(make_parented<TreeItemModel>(this)),
          m_pTableModel(make_parented<RestLibraryTableModel>(
                  this,
                  pTrackCollectionManager,
                  RestLibraryTableModel::Mode::Catalog)),
          m_pRefreshAction(make_parented<QAction>(tr("Refresh"), this)),
          m_pBackend(pBackend),
          m_pTrackCollectionManager(pTrackCollectionManager),
          m_pLoudnessManager(pLoudnessManager),
          m_client(pBackend->networkAccessManager(), this),
          m_settingsIdentity(
                  settingsIdentity(RestLibrarySettings::fromConfig(m_pConfig))) {
    m_pTableModel->setCacheIdentity(RestLibraryCacheManager::cacheIdentity(
            RestLibrarySettings::fromConfig(m_pConfig)));
    m_pSidebarModel->setRootItem(TreeItem::newRoot(this));
    connect(m_pRefreshAction,
            &QAction::triggered,
            this,
            &RestLibraryBrowserFeature::slotRefresh);
    connect(&m_client,
            &RestLibraryClient::trackCatalogPageFetched,
            this,
            &RestLibraryBrowserFeature::slotCatalogPageFetched);
    connect(&m_client,
            &RestLibraryClient::trackCatalogFetchFailed,
            this,
            &RestLibraryBrowserFeature::slotCatalogFetchFailed);
    connect(m_pBackend->cacheManager(),
            &RestLibraryCacheManager::trackCacheStateChanged,
            this,
            &RestLibraryBrowserFeature::slotTrackCacheStateChanged);
    if (m_pLoudnessManager) {
        connect(m_pLoudnessManager,
                &RestLibraryLoudnessManager::trackLoudnessPrepared,
                this,
                &RestLibraryBrowserFeature::slotTrackLoudnessPrepared);
    }
}

QVariant RestLibraryBrowserFeature::title() {
    return tr("REST Library");
}

TreeItemModel* RestLibraryBrowserFeature::sidebarModel() const {
    return m_pSidebarModel;
}

void RestLibraryBrowserFeature::bindLibraryWidget(
        WLibrary* pLibraryWidget,
        KeyboardEventFilter* pKeyboard) {
    m_pView = new DlgRestLibraryBrowser(
            pLibraryWidget,
            m_pConfig,
            m_pLibrary,
            m_pTableModel,
            pKeyboard);
    m_pView->installEventFilter(pKeyboard);
    pLibraryWidget->registerView(kViewName, m_pView);
    connect(m_pView,
            &DlgRestLibraryBrowser::refreshRequested,
            this,
            &RestLibraryBrowserFeature::slotRefresh);
    connect(m_pView,
            &DlgRestLibraryBrowser::loadTrack,
            this,
            [this](const TrackPointer& pTrack) { emit loadTrack(pTrack); });
    connect(m_pView,
            &DlgRestLibraryBrowser::loadTrackToPlayer,
            this,
            [this](const TrackPointer& pTrack,
                    const QString& group,
#ifdef __STEM__
                    mixxx::StemChannelSelection stemMask,
#endif
                    bool play) {
#ifdef __STEM__
                emit loadTrackToPlayer(pTrack, group, stemMask, play);
#else
                emit loadTrackToPlayer(pTrack, group, play);
#endif
            });
    connect(m_pView,
            &DlgRestLibraryBrowser::unresolvedTrackLoadRequested,
            this,
            &RestLibraryBrowserFeature::slotUnresolvedTrackLoad);
    connect(m_pView,
            &DlgRestLibraryBrowser::unresolvedTrackLoadToPlayerRequested,
            this,
            &RestLibraryBrowserFeature::slotUnresolvedTrackLoadToPlayer);
    connect(m_pView,
            &DlgRestLibraryBrowser::unresolvedTracksAddToAutoDJRequested,
            this,
            &RestLibraryBrowserFeature::slotUnresolvedTracksAddToAutoDJ);
    connect(m_pView,
            &DlgRestLibraryBrowser::trackSelected,
            this,
            &RestLibraryBrowserFeature::trackSelected);
    connect(this,
            &RestLibraryBrowserFeature::statusTextChanged,
            m_pView,
            &DlgRestLibraryBrowser::setStatusText);
    if (!m_statusText.isEmpty()) {
        emit statusTextChanged(m_statusText);
    }
}

void RestLibraryBrowserFeature::activate() {
    emit saveModelState();
    emit switchToView(kViewName);
    if (m_pView) {
        emit restoreSearch(m_pView->currentSearch());
    }
    emit enableCoverArtDisplay(false);

    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(m_pConfig);
    resetIfSettingsChanged(settings);
    updateLoadCapabilities(settings);
    if (!m_catalogLoaded && !m_refreshing) {
        slotRefresh();
    }
}

void RestLibraryBrowserFeature::onRightClick(const QPoint& globalPos) {
    QMenu menu;
    menu.addAction(m_pRefreshAction);
    menu.exec(globalPos);
}

void RestLibraryBrowserFeature::slotRefresh() {
    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(m_pConfig);
    resetIfSettingsChanged(settings);
    updateLoadCapabilities(settings);
    if (!settings.isConfigured() || !settings.useMixManDefaults) {
        m_client.cancelTrackCatalogRequest();
        m_refreshing = false;
        m_pRefreshAction->setEnabled(true);
        clearPendingIntents();
        setStatusText(tr("Configure a MixMan REST Library connection in Preferences."));
        return;
    }
    m_client.cancelTrackCatalogRequest();
    m_stagingTracks.clear();
    m_stagingRemoteIds.clear();
    m_seenCursors.clear();
    m_catalogLimits.reset(settings.maxCatalogPages, settings.maxCatalogTracks);
    m_refreshSettings = settings;
    m_refreshing = true;
    m_pRefreshAction->setEnabled(false);
    setStatusText(tr("Loading REST Library catalog…"));
    requestNextCatalogPage({});
}

void RestLibraryBrowserFeature::requestNextCatalogPage(const QString& cursor) {
    if (!cursor.isEmpty() && m_seenCursors.contains(cursor)) {
        slotCatalogFetchFailed(tr("MixMan returned a repeated catalog cursor."));
        return;
    }
    if (!cursor.isEmpty()) {
        m_seenCursors.insert(cursor);
    }
    if (m_catalogLimits.requestNextPage() ==
            RestLibraryCatalogLimits::Result::PageLimitReached) {
        const QString message = tr(
                "REST Library catalog refresh reached the configured maximum of %1 pages.")
                                        .arg(m_catalogLimits.maxPages());
        kLogger.warning() << message;
        slotCatalogFetchFailed(message);
        return;
    }
    m_client.fetchTrackCatalogPage(m_refreshSettings, cursor);
}

void RestLibraryBrowserFeature::slotCatalogPageFetched(
        const RestLibraryCatalogPage& page) {
    const RestLibrarySettings currentSettings =
            RestLibrarySettings::fromConfig(m_pConfig);
    if (resetIfSettingsChanged(currentSettings)) {
        updateLoadCapabilities(currentSettings);
        if (currentSettings.isConfigured() && currentSettings.useMixManDefaults) {
            slotRefresh();
        } else {
            setStatusText(tr("Configure a MixMan REST Library connection in Preferences."));
        }
        return;
    }
    if (m_catalogLimits.acceptPage(page.tracks, m_stagingRemoteIds) ==
            RestLibraryCatalogLimits::Result::TrackLimitReached) {
        const QString message = tr(
                "REST Library catalog refresh reached the configured maximum of %1 tracks.")
                                        .arg(m_catalogLimits.maxTracks());
        kLogger.warning() << message;
        slotCatalogFetchFailed(message);
        return;
    }
    for (const RestLibraryTrack& track : page.tracks) {
        if (!m_stagingRemoteIds.contains(track.remoteId)) {
            m_stagingRemoteIds.insert(track.remoteId);
            m_stagingTracks.append(track);
        }
    }
    setStatusText(tr("Loading REST Library catalog… %1 tracks")
                          .arg(m_stagingTracks.size()));
    if (!page.nextCursor.isEmpty()) {
        requestNextCatalogPage(page.nextCursor);
        return;
    }

    updateLoadCapabilities(currentSettings);
    const QStringList selectedRemoteIds =
            m_pView ? m_pView->selectedRemoteIds() : QStringList{};
    const QList<RestLibraryTrack> completedTracks = std::move(m_stagingTracks);
    m_pTableModel->setTracks(completedTracks);
    if (m_pView) {
        m_pView->restoreSelectedRemoteIds(selectedRemoteIds);
    }
    m_pBackend->cacheManager()->reconcileTracks(completedTracks, currentSettings);
    m_stagingRemoteIds.clear();
    m_seenCursors.clear();
    m_catalogLimits.reset(
            currentSettings.maxCatalogPages,
            currentSettings.maxCatalogTracks);
    m_catalogLoaded = true;
    m_refreshing = false;
    m_pRefreshAction->setEnabled(true);
    updateStatusSummary();
}

void RestLibraryBrowserFeature::slotCatalogFetchFailed(const QString& message) {
    const RestLibrarySettings currentSettings =
            RestLibrarySettings::fromConfig(m_pConfig);
    if (resetIfSettingsChanged(currentSettings)) {
        updateLoadCapabilities(currentSettings);
        if (currentSettings.isConfigured() && currentSettings.useMixManDefaults) {
            slotRefresh();
        } else {
            setStatusText(tr("Configure a MixMan REST Library connection in Preferences."));
        }
        return;
    }
    m_refreshing = false;
    m_pRefreshAction->setEnabled(true);
    m_stagingTracks.clear();
    m_stagingRemoteIds.clear();
    m_seenCursors.clear();
    m_catalogLimits.reset(
            currentSettings.maxCatalogPages,
            currentSettings.maxCatalogTracks);
    setStatusText(message.isEmpty() ? tr("REST Library catalog refresh failed.") : message);
}

void RestLibraryBrowserFeature::slotTrackCacheStateChanged(
        const RestLibraryCacheResult& result) {
    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(m_pConfig);
    if (resetIfSettingsChanged(settings)) {
        updateLoadCapabilities(settings);
        if (settings.isConfigured() && settings.useMixManDefaults) {
            slotRefresh();
        }
        return;
    }
    if (result.cacheIdentity != RestLibraryCacheManager::cacheIdentity(settings)) {
        return;
    }
    m_pTableModel->updateTrackCacheState(result);
    const bool wasPendingForAutoDJ = m_autoDJIntent.pendingIds.contains(result.remoteId);
    const bool hasPendingManualLoad =
            m_pendingDefaultLoadRemoteId == result.remoteId ||
            std::any_of(m_pendingPlayerLoads.cbegin(),
                    m_pendingPlayerLoads.cend(),
                    [&result](const PlayerLoadIntent& intent) {
                        return intent.remoteId == result.remoteId;
                    });
    bool completedManualLoad = true;
    if (result.cacheState == RestLibraryCacheState::Ready && hasPendingManualLoad) {
        completedManualLoad = finishPendingLoads(result.remoteId);
    } else if (result.cacheState == RestLibraryCacheState::Failed) {
        failPendingLoads(
                result.remoteId,
                result.errorText.isEmpty()
                        ? tr("REST track download failed.")
                        : result.errorText);
    }
    if (m_autoDJIntent.pendingIds.contains(result.remoteId) &&
            (result.cacheState == RestLibraryCacheState::Ready ||
                    result.cacheState == RestLibraryCacheState::Failed)) {
        if (result.cacheState == RestLibraryCacheState::Failed) {
            m_autoDJIntent.pendingIds.remove(result.remoteId);
            m_autoDJIntent.failedIds.insert(result.remoteId);
        } else {
            const RestLibraryLoudnessResult loudness =
                    prepareTrackForPlayback(result.remoteId);
            if (loudness.state != RestLibraryLoudnessState::Analyzing) {
                m_autoDJIntent.pendingIds.remove(result.remoteId);
            }
            if (loudness.state == RestLibraryLoudnessState::Failed) {
                m_autoDJIntent.failedIds.insert(result.remoteId);
                setStatusText(loudness.errorText);
            }
        }
        finishAutoDJIfReady();
    }
    if (result.cacheState == RestLibraryCacheState::Ready &&
            !wasPendingForAutoDJ && completedManualLoad) {
        updateStatusSummary();
    }
}

void RestLibraryBrowserFeature::slotUnresolvedTrackLoad(const QModelIndex& index) {
    RestLibrarySettings settings;
    if (!validateDownloadSettings(&settings)) {
        return;
    }
    const QString remoteId = m_pTableModel->remoteIdForIndex(index);
    if (remoteId.isEmpty()) {
        return;
    }
    m_pendingDefaultLoadRemoteId = remoteId;
    requestTrackCache(remoteId, settings);
}

#ifdef __STEM__
void RestLibraryBrowserFeature::slotUnresolvedTrackLoadToPlayer(
        const QModelIndex& index,
        const QString& group,
        mixxx::StemChannelSelection stemMask,
        bool play) {
#else
void RestLibraryBrowserFeature::slotUnresolvedTrackLoadToPlayer(
        const QModelIndex& index,
        const QString& group,
        bool play) {
#endif
    RestLibrarySettings settings;
    if (!validateDownloadSettings(&settings)) {
        return;
    }
    const QString remoteId = m_pTableModel->remoteIdForIndex(index);
    if (remoteId.isEmpty()) {
        return;
    }
    PlayerLoadIntent intent;
    intent.remoteId = remoteId;
    intent.group = group;
    intent.play = play;
#ifdef __STEM__
    intent.stemMask = stemMask;
#endif
    m_pendingPlayerLoads.insert(group, intent);
    requestTrackCache(remoteId, settings);
}

void RestLibraryBrowserFeature::slotUnresolvedTracksAddToAutoDJ(
        const QModelIndexList& indices,
        PlaylistDAO::AutoDJSendLoc location) {
    RestLibrarySettings settings;
    if (!validateDownloadSettings(&settings)) {
        return;
    }
    if (m_autoDJIntent.active()) {
        setStatusText(tr("An AutoDJ download batch is already in progress."));
        return;
    }
    m_autoDJIntent.location = location;
    QList<RestLibraryTrack> tracksToCache;
    for (const QModelIndex& index : indices) {
        const QString remoteId = m_pTableModel->remoteIdForIndex(index);
        if (remoteId.isEmpty() || m_autoDJIntent.remoteIds.contains(remoteId)) {
            continue;
        }
        m_autoDJIntent.remoteIds.append(remoteId);
        const RestLibraryTrack track = m_pTableModel->trackForRemoteId(remoteId);
        if (!m_pTableModel->materializeTrack(remoteId) &&
                (track.cacheState != RestLibraryCacheState::Ready ||
                        track.cachedFilePath.isEmpty())) {
            m_autoDJIntent.pendingIds.insert(remoteId);
            tracksToCache.append(track);
        } else {
            const RestLibraryLoudnessResult loudness =
                    prepareTrackForPlayback(remoteId);
            if (loudness.state == RestLibraryLoudnessState::Analyzing) {
                m_autoDJIntent.pendingIds.insert(remoteId);
            } else if (loudness.state == RestLibraryLoudnessState::Failed) {
                m_autoDJIntent.failedIds.insert(remoteId);
                setStatusText(loudness.errorText);
            }
        }
    }
    if (m_autoDJIntent.remoteIds.isEmpty()) {
        m_autoDJIntent.clear();
        return;
    }
    if (!tracksToCache.isEmpty()) {
        m_pBackend->cacheManager()->cacheTracks(
                tracksToCache,
                settings,
                RestLibraryCacheRequestOwner::BrowserAutoDJ);
        setStatusText(tr("Downloading %1 tracks for AutoDJ…")
                              .arg(tracksToCache.size()));
    }
    finishAutoDJIfReady();
}

void RestLibraryBrowserFeature::requestTrackCache(
        const QString& remoteId,
        const RestLibrarySettings& settings) {
    const RestLibraryTrack track = m_pTableModel->trackForRemoteId(remoteId);
    if (track.remoteId.isEmpty()) {
        return;
    }
    if (track.cacheState == RestLibraryCacheState::Ready &&
            !track.cachedFilePath.isEmpty()) {
        finishPendingLoads(remoteId);
        return;
    }
    m_pBackend->cacheManager()->cacheTracks(
            {track},
            settings,
            RestLibraryCacheRequestOwner::BrowserLoad);
    setStatusText(tr("Downloading %1 — %2…").arg(track.artist, track.title));
}

bool RestLibraryBrowserFeature::finishPendingLoads(const QString& remoteId) {
    const TrackPointer pTrack = m_pTableModel->materializeTrack(remoteId);
    if (!pTrack) {
        setStatusText(tr("Track was cached but could not be loaded."));
        return false;
    }
    const RestLibraryLoudnessResult loudness = prepareTrackForPlayback(remoteId);
    if (loudness.state == RestLibraryLoudnessState::Analyzing) {
        setStatusText(tr("Analyzing ReplayGain for %1 — %2…")
                              .arg(pTrack->getArtist(), pTrack->getTitle()));
        return false;
    }
    if (loudness.state == RestLibraryLoudnessState::Failed) {
        failPendingLoads(remoteId, loudness.errorText);
        return false;
    }
    bool completedCleanly = true;
    if (m_pendingDefaultLoadRemoteId == remoteId) {
        m_pendingDefaultLoadRemoteId.clear();
        emit loadTrack(pTrack);
    }

    for (auto it = m_pendingPlayerLoads.begin(); it != m_pendingPlayerLoads.end();) {
        if (it->remoteId != remoteId) {
            ++it;
            continue;
        }
        const PlayerLoadIntent intent = it.value();
        it = m_pendingPlayerLoads.erase(it);
        if (!mayLoadToGroup(intent.group)) {
            setStatusText(tr("Track cached; the destination deck became busy. Load it again."));
            completedCleanly = false;
            continue;
        }
#ifdef __STEM__
        emit loadTrackToPlayer(
                pTrack, intent.group, intent.stemMask, intent.play);
#else
        emit loadTrackToPlayer(pTrack, intent.group, intent.play);
#endif
    }
    return completedCleanly;
}

RestLibraryLoudnessResult RestLibraryBrowserFeature::prepareTrackForPlayback(
        const QString& remoteId) {
    const TrackPointer pTrack = m_pTableModel->materializeTrack(remoteId);
    if (!pTrack) {
        return {{},
                RestLibraryLoudnessState::Failed,
                tr("Track was cached but could not be prepared for playback.")};
    }
    if (!m_pLoudnessManager) {
        return {pTrack->getId(), RestLibraryLoudnessState::Ready, {}};
    }
    const RestLibraryLoudnessResult result =
            m_pLoudnessManager->prepareTrack(pTrack);
    if (result.state == RestLibraryLoudnessState::Analyzing) {
        m_loudnessRemoteIds[result.trackId].insert(remoteId);
    }
    return result;
}

void RestLibraryBrowserFeature::failPendingLoads(
        const QString& remoteId,
        const QString& errorText) {
    if (m_pendingDefaultLoadRemoteId == remoteId) {
        m_pendingDefaultLoadRemoteId.clear();
    }
    for (auto it = m_pendingPlayerLoads.begin();
            it != m_pendingPlayerLoads.end();) {
        if (it->remoteId == remoteId) {
            it = m_pendingPlayerLoads.erase(it);
        } else {
            ++it;
        }
    }
    setStatusText(errorText);
}

void RestLibraryBrowserFeature::slotTrackLoudnessPrepared(
        const RestLibraryLoudnessResult& result) {
    const QSet<QString> remoteIds = m_loudnessRemoteIds.take(result.trackId);
    for (const QString& remoteId : remoteIds) {
        if (result.state == RestLibraryLoudnessState::Ready) {
            finishPendingLoads(remoteId);
        } else {
            failPendingLoads(remoteId, result.errorText);
        }
        if (m_autoDJIntent.pendingIds.contains(remoteId)) {
            m_autoDJIntent.pendingIds.remove(remoteId);
            if (result.state != RestLibraryLoudnessState::Ready) {
                m_autoDJIntent.failedIds.insert(remoteId);
            }
        }
    }
    finishAutoDJIfReady();
}

void RestLibraryBrowserFeature::finishAutoDJIfReady() {
    if (!m_autoDJIntent.active() || !m_autoDJIntent.pendingIds.isEmpty()) {
        return;
    }
    QList<TrackId> trackIds;
    for (const QString& remoteId : std::as_const(m_autoDJIntent.remoteIds)) {
        if (m_autoDJIntent.failedIds.contains(remoteId)) {
            continue;
        }
        const TrackPointer pTrack = m_pTableModel->materializeTrack(remoteId);
        if (pTrack && pTrack->getId().isValid()) {
            trackIds.append(pTrack->getId());
        }
    }
    const int failedCount =
            m_autoDJIntent.remoteIds.size() - trackIds.size();
    if (!trackIds.isEmpty()) {
        m_pTrackCollectionManager->internalCollection()
                ->getPlaylistDAO()
                .addTracksToAutoDJQueue(trackIds, m_autoDJIntent.location);
    }
    setStatusText(trackIds.isEmpty()
                    ? tr("No selected REST tracks could be added to AutoDJ.")
                    : failedCount > 0
                    ? tr("Added %1 tracks to AutoDJ; %2 failed.")
                              .arg(trackIds.size())
                              .arg(failedCount)
                    : tr("Added %1 tracks to AutoDJ.").arg(trackIds.size()));
    m_autoDJIntent.clear();
}

bool RestLibraryBrowserFeature::mayLoadToGroup(const QString& group) const {
    if (PlayerManager::isPreviewDeckGroup(group)) {
        return true;
    }
    bool allow = false;
    if (m_pConfig->exists(kConfigKeyLoadWhenDeckPlaying)) {
        const auto setting = static_cast<LoadWhenDeckPlaying>(
                m_pConfig->getValueString(kConfigKeyLoadWhenDeckPlaying).toInt());
        allow = setting == LoadWhenDeckPlaying::Allow ||
                setting == LoadWhenDeckPlaying::AllowButStopDeck;
    } else {
        allow = m_pConfig->getValue<bool>(kConfigKeyAllowTrackLoadToPlayingDeck);
    }
    return allow || ControlObject::get(ConfigKey(group, QStringLiteral("play"))) <= 0.0;
}

bool RestLibraryBrowserFeature::resetIfSettingsChanged(
        const RestLibrarySettings& settings) {
    const QString identity = settingsIdentity(settings);
    if (identity == m_settingsIdentity) {
        return false;
    }
    m_client.cancelTrackCatalogRequest();
    m_pBackend->cacheManager()->abortAll();
    clearPendingIntents();
    m_stagingTracks.clear();
    m_stagingRemoteIds.clear();
    m_seenCursors.clear();
    m_catalogLimits.reset(settings.maxCatalogPages, settings.maxCatalogTracks);
    m_pTableModel->setTracks({});
    m_pTableModel->setCacheIdentity(
            RestLibraryCacheManager::cacheIdentity(settings));
    m_catalogLoaded = false;
    m_refreshing = false;
    m_pRefreshAction->setEnabled(true);
    m_settingsIdentity = identity;
    return true;
}

bool RestLibraryBrowserFeature::validateDownloadSettings(
        RestLibrarySettings* pSettings) {
    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(m_pConfig);
    if (resetIfSettingsChanged(settings)) {
        updateLoadCapabilities(settings);
        if (settings.isConfigured() && settings.useMixManDefaults) {
            slotRefresh();
        }
        return false;
    }
    updateLoadCapabilities(settings);
    if (!settings.isConfigured() || !settings.useMixManDefaults ||
            !settings.hasAudioDownloadConfigured()) {
        clearPendingIntents();
        setStatusText(tr("REST Library audio download is not configured."));
        return false;
    }
    if (pSettings) {
        *pSettings = settings;
    }
    return true;
}

void RestLibraryBrowserFeature::clearPendingIntents() {
    m_pendingDefaultLoadRemoteId.clear();
    m_pendingPlayerLoads.clear();
    m_autoDJIntent.clear();
    m_loudnessRemoteIds.clear();
}

void RestLibraryBrowserFeature::updateLoadCapabilities(
        const RestLibrarySettings& settings) {
    m_pTableModel->setCacheLoadCapabilitiesEnabled(
            settings.isConfigured() && settings.useMixManDefaults &&
            settings.hasAudioDownloadConfigured());
}

QString RestLibraryBrowserFeature::settingsIdentity(
        const RestLibrarySettings& settings) const {
    QUrl url = settings.baseUrl.adjusted(
            QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment);
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    url.setPath(path);
    return url.toString(QUrl::FullyEncoded) + QLatin1Char('|') +
            settings.trackListPath + QLatin1Char('|') +
            (settings.useMixManDefaults ? QStringLiteral("mixman") : QStringLiteral("custom")) +
            QLatin1Char('|') + settings.credentialContextNamespace();
}

void RestLibraryBrowserFeature::setStatusText(const QString& text) {
    m_statusText = text;
    emit statusTextChanged(text);
}

void RestLibraryBrowserFeature::updateStatusSummary() {
    const int trackCount = m_pTableModel->trackCount();
    QString summary = trackCount == 1
            ? tr("1 REST Library track")
            : tr("%1 REST Library tracks").arg(trackCount);
    QStringList details;
    const int readyCount =
            m_pTableModel->cacheStateCount(RestLibraryCacheState::Ready);
    const int downloadingCount =
            m_pTableModel->cacheStateCount(RestLibraryCacheState::Downloading);
    const int failedCount =
            m_pTableModel->cacheStateCount(RestLibraryCacheState::Failed);
    if (readyCount > 0) {
        details.append(tr("%1 cached").arg(readyCount));
    }
    if (downloadingCount > 0) {
        details.append(tr("%1 downloading").arg(downloadingCount));
    }
    if (failedCount > 0) {
        details.append(tr("%1 failed").arg(failedCount));
    }
    if (!details.isEmpty()) {
        summary += QStringLiteral(" \u00b7 ") + details.join(QStringLiteral(" \u00b7 "));
    }
    setStatusText(summary);
}

} // namespace mixxx::library::rest
