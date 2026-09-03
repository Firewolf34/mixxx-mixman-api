#include "library/rest/restlibrarybrowserfeature.h"

#include <algorithm>
#include <cmath>

#include <QJsonObject>
#include <QMenu>
#include <utility>

#include "control/controlobject.h"
#include "controllers/keyboard/keyboardeventfilter.h"
#include "library/library.h"
#include "library/rest/dlgrestlibrarybrowser.h"
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
        RestLibraryLoudnessManager* pLoudnessManager,
        RestLibraryCatalogProvider* pCatalogProvider)
        : LibraryFeature(pLibrary, std::move(pConfig), QStringLiteral("computer")),
          m_pSidebarModel(make_parented<TreeItemModel>(this)),
          m_pTableModel(make_parented<RestLibraryTableModel>(
                  this,
                  pTrackCollectionManager,
                  RestLibraryTableModel::Mode::Catalog)),
          m_pRefreshAction(make_parented<QAction>(tr("Refresh"), this)),
          m_pTrackCollectionManager(pTrackCollectionManager),
          m_pLoudnessManager(pLoudnessManager) {
    if (pCatalogProvider) {
        m_pCatalogProvider = pCatalogProvider;
    } else {
        m_pOwnedCatalogProvider =
                std::make_unique<MixManRestLibraryCatalogProvider>(
                        m_pConfig,
                        pBackend->networkAccessManager(),
                        pBackend->cacheManager());
        m_pCatalogProvider = m_pOwnedCatalogProvider.get();
    }
    m_currentContext = m_pCatalogProvider->context();
    m_pTableModel->setCacheIdentity(m_currentContext.cacheIdentity);
    m_pSidebarModel->setRootItem(TreeItem::newRoot(this));
    connect(m_pRefreshAction,
            &QAction::triggered,
            this,
            &RestLibraryBrowserFeature::slotRefresh);
    connect(m_pCatalogProvider,
            &RestLibraryCatalogProvider::pageFetched,
            this,
            &RestLibraryBrowserFeature::slotCatalogPageFetched);
    connect(m_pCatalogProvider,
            &RestLibraryCatalogProvider::pageFetchFailed,
            this,
            &RestLibraryBrowserFeature::slotCatalogFetchFailed);
    connect(m_pCatalogProvider,
            &RestLibraryCatalogProvider::mediaStateChanged,
            this,
            &RestLibraryBrowserFeature::slotTrackCacheStateChanged);
    connect(m_pCatalogProvider,
            &RestLibraryCatalogProvider::mutationMetadataFetched,
            this,
            &RestLibraryBrowserFeature::slotMutationMetadataFetched);
    connect(m_pCatalogProvider,
            &RestLibraryCatalogProvider::trackMutationFinished,
            this,
            &RestLibraryBrowserFeature::slotTrackMutationFinished);
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
    connect(m_pView,
            &DlgRestLibraryBrowser::favourBumpRequested,
            this,
            &RestLibraryBrowserFeature::slotFavourBumpRequested);
    connect(m_pView,
            &DlgRestLibraryBrowser::djNoteRequested,
            this,
            &RestLibraryBrowserFeature::slotDjNoteRequested);
    connect(m_pView,
            &DlgRestLibraryBrowser::returnToReviewRequested,
            this,
            &RestLibraryBrowserFeature::slotReturnToReviewRequested);
    connect(m_pView,
            &DlgRestLibraryBrowser::selectedRemoteIdsChanged,
            this,
            &RestLibraryBrowserFeature::updateMaintenanceControls);
    connect(this,
            &RestLibraryBrowserFeature::statusTextChanged,
            m_pView,
            &DlgRestLibraryBrowser::setStatusText);
    if (!m_statusText.isEmpty()) {
        emit statusTextChanged(m_statusText);
    }
    updateMaintenanceControls();
}

void RestLibraryBrowserFeature::activate() {
    emit saveModelState();
    emit switchToView(kViewName);
    if (m_pView) {
        emit restoreSearch(m_pView->currentSearch());
    }
    emit enableCoverArtDisplay(false);

    const RestLibraryCatalogContext context = m_pCatalogProvider->context();
    resetIfContextChanged(context);
    updateLoadCapabilities(context);
    refreshMutationMetadata(context);
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
    if (m_mutationBusy) {
        return;
    }
    const RestLibraryCatalogContext context = m_pCatalogProvider->context();
    resetIfContextChanged(context);
    updateLoadCapabilities(context);
    refreshMutationMetadata(context);
    if (!context.configured) {
        m_pCatalogProvider->cancelPageFetch();
        m_refreshing = false;
        m_pRefreshAction->setEnabled(true);
        updateMaintenanceControls();
        clearPendingIntents();
        setStatusText(tr("Configure a REST Library catalog provider in Preferences."));
        return;
    }
    m_pCatalogProvider->cancelPageFetch();
    m_stagingTracks.clear();
    m_stagingRemoteIds.clear();
    m_seenCursors.clear();
    m_catalogLimits.reset(context.maxPages, context.maxTracks);
    m_refreshContext = context;
    m_refreshing = true;
    m_pRefreshAction->setEnabled(false);
    updateMaintenanceControls();
    setStatusText(tr("Loading REST Library catalog…"));
    requestNextCatalogPage({});
}

void RestLibraryBrowserFeature::requestNextCatalogPage(const QString& cursor) {
    if (!cursor.isEmpty() && m_seenCursors.contains(cursor)) {
        slotCatalogFetchFailed(
                m_refreshContext.scopeIdentity,
                tr("%1 returned a repeated catalog cursor.")
                        .arg(m_refreshContext.displayName));
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
        slotCatalogFetchFailed(m_refreshContext.scopeIdentity, message);
        return;
    }
    m_pCatalogProvider->fetchPage(m_refreshContext, cursor);
}

void RestLibraryBrowserFeature::slotCatalogPageFetched(
        const QString& scopeIdentity,
        const RestLibraryCatalogPage& page) {
    if (scopeIdentity != m_refreshContext.scopeIdentity) {
        return;
    }
    const RestLibraryCatalogContext currentContext = m_pCatalogProvider->context();
    if (resetIfContextChanged(currentContext)) {
        updateLoadCapabilities(currentContext);
        if (currentContext.configured) {
            slotRefresh();
        } else {
            setStatusText(tr("Configure a REST Library catalog provider in Preferences."));
        }
        return;
    }
    if (m_catalogLimits.acceptPage(page.tracks, m_stagingRemoteIds) ==
            RestLibraryCatalogLimits::Result::TrackLimitReached) {
        const QString message = tr(
                "REST Library catalog refresh reached the configured maximum of %1 tracks.")
                                        .arg(m_catalogLimits.maxTracks());
        kLogger.warning() << message;
        slotCatalogFetchFailed(m_refreshContext.scopeIdentity, message);
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

    updateLoadCapabilities(currentContext);
    const QStringList selectedRemoteIds =
            m_pView ? m_pView->selectedRemoteIds() : QStringList{};
    const QList<RestLibraryTrack> completedTracks = std::move(m_stagingTracks);
    m_pTableModel->setTracks(completedTracks);
    if (m_pView) {
        m_pView->restoreSelectedRemoteIds(selectedRemoteIds);
    }
    m_pCatalogProvider->reconcileTracks(currentContext, completedTracks);
    m_stagingRemoteIds.clear();
    m_seenCursors.clear();
    m_catalogLimits.reset(
            currentContext.maxPages,
            currentContext.maxTracks);
    m_catalogLoaded = true;
    m_refreshing = false;
    m_pRefreshAction->setEnabled(true);
    updateMaintenanceControls();
    updateStatusSummary();
}

void RestLibraryBrowserFeature::slotCatalogFetchFailed(
        const QString& scopeIdentity,
        const QString& message) {
    if (scopeIdentity != m_refreshContext.scopeIdentity) {
        return;
    }
    const RestLibraryCatalogContext currentContext = m_pCatalogProvider->context();
    if (resetIfContextChanged(currentContext)) {
        updateLoadCapabilities(currentContext);
        if (currentContext.configured) {
            slotRefresh();
        } else {
            setStatusText(tr("Configure a REST Library catalog provider in Preferences."));
        }
        return;
    }
    m_refreshing = false;
    m_pRefreshAction->setEnabled(true);
    updateMaintenanceControls();
    m_stagingTracks.clear();
    m_stagingRemoteIds.clear();
    m_seenCursors.clear();
    m_catalogLimits.reset(
            currentContext.maxPages,
            currentContext.maxTracks);
    setStatusText(message.isEmpty() ? tr("REST Library catalog refresh failed.") : message);
}

void RestLibraryBrowserFeature::slotTrackCacheStateChanged(
        const RestLibraryCacheResult& result) {
    const RestLibraryCatalogContext context = m_pCatalogProvider->context();
    if (resetIfContextChanged(context)) {
        updateLoadCapabilities(context);
        if (context.configured) {
            slotRefresh();
        }
        return;
    }
    if (result.cacheIdentity != context.cacheIdentity) {
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
    RestLibraryCatalogContext context;
    if (!validateDownloadContext(&context)) {
        return;
    }
    const QString remoteId = m_pTableModel->remoteIdForIndex(index);
    if (remoteId.isEmpty()) {
        return;
    }
    m_pendingDefaultLoadRemoteId = remoteId;
    requestTrackCache(remoteId, context);
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
    RestLibraryCatalogContext context;
    if (!validateDownloadContext(&context)) {
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
    requestTrackCache(remoteId, context);
}

void RestLibraryBrowserFeature::slotUnresolvedTracksAddToAutoDJ(
        const QModelIndexList& indices,
        PlaylistDAO::AutoDJSendLoc location) {
    RestLibraryCatalogContext context;
    if (!validateDownloadContext(&context)) {
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
        m_pCatalogProvider->resolveAudio(
                context,
                tracksToCache,
                RestLibraryCacheRequestOwner::BrowserAutoDJ);
        setStatusText(tr("Downloading %1 tracks for AutoDJ…")
                              .arg(tracksToCache.size()));
    }
    finishAutoDJIfReady();
}

QString RestLibraryBrowserFeature::selectedRemoteId() const {
    if (!m_pView) {
        return {};
    }
    const QStringList remoteIds = m_pView->selectedRemoteIds();
    return remoteIds.size() == 1 ? remoteIds.constFirst() : QString();
}

void RestLibraryBrowserFeature::slotFavourBumpRequested(int direction) {
    direction = direction < 0 ? -1 : 1;
    const QString remoteId = selectedRemoteId();
    if (remoteId.isEmpty()) {
        return;
    }
    if (m_mutationBusy) {
        if (m_activeMutation == RestLibraryTrackMutation::Favour &&
                remoteId == m_mutatingRemoteId) {
            m_queuedFavourSteps += direction;
            setStatusText(m_queuedFavourSteps == 0
                            ? tr("Favour update in progress.")
                            : tr("Favour update in progress; queued net change: %1.")
                                      .arg(m_queuedFavourSteps));
        }
        return;
    }
    startFavourMutation(remoteId, direction);
}

void RestLibraryBrowserFeature::startFavourMutation(
        const QString& remoteId,
        int stepCount) {
    if (!m_mutationMetadata.valid ||
            !m_mutationMetadata.mayWriteFavour || stepCount == 0) {
        updateMaintenanceControls();
        return;
    }
    const RestLibraryTrack track = m_pTableModel->trackForRemoteId(remoteId);
    if (track.remoteId.isEmpty()) {
        return;
    }
    const double currentDisplayFavour =
            (track.favour.has_value() ? *track.favour : 0.5) * 5.0;
    const double targetDisplayFavour = std::clamp(
            currentDisplayFavour +
                    stepCount * m_mutationMetadata.favourStep,
            0.0,
            5.0);
    if (qFuzzyCompare(currentDisplayFavour + 1.0, targetDisplayFavour + 1.0)) {
        setStatusText(targetDisplayFavour <= 0.0
                        ? tr("Favour is already at its minimum.")
                        : tr("Favour is already at its maximum."));
        return;
    }

    m_mutationContext = m_pCatalogProvider->context();
    m_mutatingRemoteId = remoteId;
    m_activeMutation = RestLibraryTrackMutation::Favour;
    m_mutationBusy = true;
    updateMaintenanceControls();
    setStatusText(tr("Updating track favour…"));
    m_pCatalogProvider->updateTrackMetadata(
            m_mutationContext,
            remoteId,
            {{QStringLiteral("favour"), targetDisplayFavour / 5.0}},
            RestLibraryTrackMutation::Favour);
}

void RestLibraryBrowserFeature::slotDjNoteRequested() {
    if (m_mutationBusy || !m_pView ||
            !m_mutationMetadata.valid ||
            !m_mutationMetadata.mayWriteDjComment) {
        return;
    }
    const QString remoteId = selectedRemoteId();
    const RestLibraryTrack track = m_pTableModel->trackForRemoteId(remoteId);
    if (track.remoteId.isEmpty()) {
        return;
    }
    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(m_pConfig);
    const std::optional<QString> note =
            m_pView->editDjNote(track, settings.djNotePresets);
    if (!note.has_value()) {
        return;
    }
    m_mutationContext = m_pCatalogProvider->context();
    m_mutatingRemoteId = remoteId;
    m_activeMutation = RestLibraryTrackMutation::DjComment;
    m_mutationBusy = true;
    updateMaintenanceControls();
    setStatusText(tr("Saving DJ note…"));
    m_pCatalogProvider->updateTrackMetadata(
            m_mutationContext,
            remoteId,
            {{QStringLiteral("dj_comment"), note->left(config::kMaxDjNoteLength)}},
            RestLibraryTrackMutation::DjComment);
}

void RestLibraryBrowserFeature::slotReturnToReviewRequested() {
    if (m_mutationBusy || !m_pView ||
            !m_mutationMetadata.valid ||
            !m_mutationMetadata.mayReturnToReview) {
        return;
    }
    const QString remoteId = selectedRemoteId();
    const RestLibraryTrack track = m_pTableModel->trackForRemoteId(remoteId);
    if (track.remoteId.isEmpty()) {
        return;
    }
    const std::optional<QString> reason =
            m_pView->confirmReturnToReview(track);
    if (!reason.has_value()) {
        return;
    }
    m_mutationContext = m_pCatalogProvider->context();
    m_mutatingRemoteId = remoteId;
    m_activeMutation = RestLibraryTrackMutation::ReturnToReview;
    m_mutationBusy = true;
    updateMaintenanceControls();
    setStatusText(tr("Returning track to review…"));
    m_pCatalogProvider->returnTrackToReview(
            m_mutationContext,
            remoteId,
            reason->left(config::kMaxDjNoteLength));
}

void RestLibraryBrowserFeature::refreshMutationMetadata(
        const RestLibraryCatalogContext& context) {
    if (!m_pView) {
        return;
    }
    if (!context.configured) {
        m_mutationMetadata = {};
        m_mutationMetadataScopeIdentity.clear();
        m_mutationMetadataLoading = false;
        updateMaintenanceControls();
        return;
    }
    if (m_mutationMetadataLoading ||
            (m_mutationMetadata.valid &&
                    m_mutationMetadataScopeIdentity == context.scopeIdentity)) {
        return;
    }
    m_mutationMetadata = {};
    m_mutationMetadata.favourStep = config::kDefaultFavourFeedbackStep;
    m_mutationMetadataScopeIdentity.clear();
    m_mutationMetadataLoading = true;
    updateMaintenanceControls();
    m_pCatalogProvider->fetchMutationMetadata(context);
}

void RestLibraryBrowserFeature::slotMutationMetadataFetched(
        const QString& scopeIdentity,
        const RestLibraryMutationMetadata& metadata) {
    if (scopeIdentity != m_pCatalogProvider->context().scopeIdentity) {
        return;
    }
    m_mutationMetadataLoading = false;
    m_mutationMetadata = metadata;
    m_mutationMetadataScopeIdentity = scopeIdentity;
    // Capabilities are dynamic but do not change catalog identity. Publish the
    // provider's current snapshot so a later activation does not reset rows.
    m_currentContext = m_pCatalogProvider->context();
    updateLoadCapabilities(m_currentContext);
    updateMaintenanceControls();
}

void RestLibraryBrowserFeature::slotTrackMutationFinished(
        const QString& scopeIdentity,
        const RestLibraryTrackMutationResult& result) {
    if (!m_mutationBusy || scopeIdentity != m_mutationContext.scopeIdentity ||
            result.remoteId != m_mutatingRemoteId ||
            result.mutation != m_activeMutation) {
        return;
    }

    const QStringList selectedRemoteIds =
            m_pView ? m_pView->selectedRemoteIds() : QStringList{};
    if (!result.success) {
        m_queuedFavourSteps = 0;
        m_mutationBusy = false;
        m_mutatingRemoteId.clear();
        setStatusText(result.errorText.isEmpty()
                        ? tr("REST Library track update failed.")
                        : result.errorText);
        updateMaintenanceControls();
        return;
    }

    if (result.mutation == RestLibraryTrackMutation::ReturnToReview) {
        m_pTableModel->removeTrack(result.remoteId);
        if (m_pView) {
            m_pView->restoreSelectedRemoteIds(selectedRemoteIds);
        }
        m_mutationBusy = false;
        m_mutatingRemoteId.clear();
        setStatusText(result.reviewHash.isEmpty()
                        ? tr("Track returned to the MixMan review queue.")
                        : tr("Track returned to review as %1.").arg(result.reviewHash));
        updateMaintenanceControls();
        return;
    }

    m_pTableModel->updateTrackMetadata(result.track, result.mutation);
    if (m_pView) {
        m_pView->restoreSelectedRemoteIds(selectedRemoteIds);
    }
    const int queuedFavourSteps = m_queuedFavourSteps;
    m_queuedFavourSteps = 0;
    m_mutationBusy = false;
    const QString remoteId = m_mutatingRemoteId;
    m_mutatingRemoteId.clear();
    if (result.mutation == RestLibraryTrackMutation::Favour &&
            queuedFavourSteps != 0) {
        startFavourMutation(remoteId, queuedFavourSteps);
        updateMaintenanceControls();
        return;
    }
    setStatusText(result.mutation == RestLibraryTrackMutation::Favour
                    ? tr("Track favour updated.")
                    : tr("DJ note saved."));
    updateMaintenanceControls();
}

void RestLibraryBrowserFeature::updateMaintenanceControls() {
    const QString remoteId = selectedRemoteId();
    const bool hasSingleSelection = !remoteId.isEmpty();
    const bool metadataCurrent = m_mutationMetadata.valid &&
            m_mutationMetadataScopeIdentity ==
                    m_pCatalogProvider->context().scopeIdentity;
    const bool favourBusyForSelection = m_mutationBusy &&
            m_activeMutation == RestLibraryTrackMutation::Favour &&
            remoteId == m_mutatingRemoteId;
    const bool favourEnabled = hasSingleSelection && metadataCurrent &&
            m_mutationMetadata.mayWriteFavour && !m_refreshing &&
            (!m_mutationBusy || favourBusyForSelection);
    const bool noteEnabled = hasSingleSelection && metadataCurrent &&
            m_mutationMetadata.mayWriteDjComment && !m_refreshing &&
            !m_mutationBusy;
    const bool reviewEnabled = hasSingleSelection && metadataCurrent &&
            m_mutationMetadata.mayReturnToReview && !m_refreshing &&
            !m_mutationBusy;
    QString reviewToolTip;
    if (hasSingleSelection && metadataCurrent &&
            !m_mutationMetadata.mayReturnToReview) {
        reviewToolTip = tr("Return to Review requires the MixMan administrator role.");
    } else if (hasSingleSelection && !metadataCurrent &&
            !m_mutationMetadataLoading) {
        reviewToolTip = tr("This server did not advertise track maintenance capabilities.");
    }
    const bool refreshEnabled = !m_refreshing && !m_mutationBusy;
    m_pRefreshAction->setEnabled(refreshEnabled);
    if (m_pView) {
        m_pView->setMaintenanceControlState(
                favourEnabled,
                noteEnabled,
                reviewEnabled,
                refreshEnabled,
                reviewToolTip);
    }
}

void RestLibraryBrowserFeature::requestTrackCache(
        const QString& remoteId,
        const RestLibraryCatalogContext& context) {
    const RestLibraryTrack track = m_pTableModel->trackForRemoteId(remoteId);
    if (track.remoteId.isEmpty()) {
        return;
    }
    if (track.cacheState == RestLibraryCacheState::Ready &&
            !track.cachedFilePath.isEmpty()) {
        finishPendingLoads(remoteId);
        return;
    }
    m_pCatalogProvider->resolveAudio(
            context,
            {track},
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

bool RestLibraryBrowserFeature::resetIfContextChanged(
        const RestLibraryCatalogContext& context) {
    if (context == m_currentContext) {
        return false;
    }
    // Publish the new scope before cancellation. Cache cancellation emits
    // synchronously, and reentrant completions from the old scope must not
    // trigger a second reset or an unintended catalog refresh.
    m_currentContext = context;
    m_pCatalogProvider->cancelPageFetch();
    m_pCatalogProvider->cancelAudio(RestLibraryCacheRequestOwner::BrowserLoad);
    m_pCatalogProvider->cancelAudio(RestLibraryCacheRequestOwner::BrowserAutoDJ);
    m_pCatalogProvider->cancelTrackMutations();
    clearPendingIntents();
    m_stagingTracks.clear();
    m_stagingRemoteIds.clear();
    m_seenCursors.clear();
    m_catalogLimits.reset(context.maxPages, context.maxTracks);
    m_pTableModel->setTracks({});
    m_pTableModel->setCacheIdentity(context.cacheIdentity);
    m_catalogLoaded = false;
    m_refreshing = false;
    m_mutationMetadata = {};
    m_mutationMetadataScopeIdentity.clear();
    m_mutationMetadataLoading = false;
    m_mutationBusy = false;
    m_mutatingRemoteId.clear();
    m_queuedFavourSteps = 0;
    updateMaintenanceControls();
    return true;
}

bool RestLibraryBrowserFeature::validateDownloadContext(
        RestLibraryCatalogContext* pContext) {
    const RestLibraryCatalogContext context = m_pCatalogProvider->context();
    if (resetIfContextChanged(context)) {
        updateLoadCapabilities(context);
        if (context.configured) {
            slotRefresh();
        }
        return false;
    }
    updateLoadCapabilities(context);
    if (!context.configured ||
            !context.capabilities.testFlag(
                    RestLibraryCatalogCapability::ResolveAudio)) {
        clearPendingIntents();
        setStatusText(tr("REST Library audio download is not configured."));
        return false;
    }
    if (pContext) {
        *pContext = context;
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
        const RestLibraryCatalogContext& context) {
    m_pTableModel->setCacheLoadCapabilitiesEnabled(
            context.configured &&
            context.capabilities.testFlag(
                    RestLibraryCatalogCapability::ResolveAudio));
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
