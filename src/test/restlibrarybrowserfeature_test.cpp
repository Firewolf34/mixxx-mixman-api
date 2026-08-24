#include <gtest/gtest.h>

#include <memory>

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "control/controlobject.h"
#include "library/dao/trackschema.h"
#include "library/libraryfeature.h"
#include "library/rest/restlibrarybackend.h"
#include "library/rest/restlibrarybrowserfeature.h"
#include "library/rest/restlibrarysettings.h"
#include "library/rest/restlibrarytablemodel.h"
#include "preferences/interface.h"
#include "test/librarytest.h"
#include "test/mock_networkaccessmanager.h"
#include "track/track.h"

namespace {

namespace restConfig = mixxx::library::rest::config;
using mixxx::library::rest::RestLibraryBackend;
using mixxx::library::rest::RestLibraryBrowserFeature;
using mixxx::library::rest::RestLibraryCacheRequestOwner;
using mixxx::library::rest::RestLibraryCatalogContext;
using mixxx::library::rest::RestLibraryCatalogPage;
using mixxx::library::rest::RestLibraryCatalogProvider;
using mixxx::library::rest::RestLibraryLoudnessManager;
using mixxx::library::rest::RestLibraryLoudnessResult;
using mixxx::library::rest::RestLibraryLoudnessState;
using mixxx::library::rest::RestLibrarySettings;
using mixxx::library::rest::RestLibraryTableModel;
using mixxx::library::rest::RestLibraryTrack;

QByteArray catalogPage(
        const QString& items,
        const QString& nextCursor = QString()) {
    return QStringLiteral(R"json({"items":[%1],"next_cursor":%2})json")
            .arg(items,
                    nextCursor.isEmpty()
                            ? QStringLiteral("null")
                            : QStringLiteral("\"%1\"").arg(nextCursor))
            .toUtf8();
}

class FakeLoudnessManager final : public RestLibraryLoudnessManager {
  public:
    using RestLibraryLoudnessManager::RestLibraryLoudnessManager;

    RestLibraryLoudnessResult prepareTrack(const TrackPointer& pTrack) override {
        if (!pTrack) {
            return {{}, RestLibraryLoudnessState::Failed, QStringLiteral("missing track")};
        }
        if (!defer || m_readyTrackIds.contains(pTrack->getId())) {
            return {pTrack->getId(), RestLibraryLoudnessState::Ready, {}};
        }
        m_pendingTracks.insert(pTrack->getId(), pTrack);
        return {pTrack->getId(), RestLibraryLoudnessState::Analyzing, {}};
    }

    void complete(const TrackPointer& pTrack, bool success) {
        ASSERT_TRUE(pTrack);
        m_pendingTracks.remove(pTrack->getId());
        RestLibraryLoudnessResult result;
        result.trackId = pTrack->getId();
        if (success) {
            mixxx::ReplayGain replayGain = pTrack->getReplayGain();
            replayGain.setRatio(2.0);
            pTrack->setReplayGain(replayGain);
            m_readyTrackIds.insert(pTrack->getId());
            result.state = RestLibraryLoudnessState::Ready;
        } else {
            result.state = RestLibraryLoudnessState::Failed;
            result.errorText = QStringLiteral("ReplayGain analysis failed");
        }
        emit trackLoudnessPrepared(result);
    }

    bool defer = false;

  private:
    QHash<TrackId, TrackPointer> m_pendingTracks;
    QSet<TrackId> m_readyTrackIds;
};

class FakeCatalogProvider final : public RestLibraryCatalogProvider {
  public:
    FakeCatalogProvider() {
        currentContext.providerId = QStringLiteral("fake");
        currentContext.scopeIdentity = QStringLiteral("fake:scope-a");
        currentContext.cacheIdentity = QStringLiteral("fake-cache-a");
        currentContext.displayName = QStringLiteral("Fake Catalog");
        currentContext.maxPages = 4;
        currentContext.maxTracks = 20;
        currentContext.configured = true;
    }

    RestLibraryCatalogContext context() const override {
        return currentContext;
    }
    void fetchPage(
            const RestLibraryCatalogContext& context,
            const QString& cursor) override {
        requestedScopes.append(context.scopeIdentity);
        requestedCursors.append(cursor);
    }
    void cancelPageFetch() override {
        ++pageCancelCount;
    }
    void reconcileTracks(
            const RestLibraryCatalogContext& context,
            const QList<RestLibraryTrack>& tracks) override {
        reconciledScope = context.scopeIdentity;
        reconciledTracks = tracks;
    }
    void resolveAudio(
            const RestLibraryCatalogContext&,
            const QList<RestLibraryTrack>& tracks,
            RestLibraryCacheRequestOwner owner) override {
        resolvedTracks = tracks;
        lastResolveOwner = owner;
    }
    void cancelAudio(RestLibraryCacheRequestOwner owner) override {
        canceledAudioOwners.insert(owner);
    }

    void completePage(
            const QString& scopeIdentity,
            const RestLibraryCatalogPage& page) {
        emit pageFetched(scopeIdentity, page);
    }
    void failPage(const QString& scopeIdentity, const QString& message) {
        emit pageFetchFailed(scopeIdentity, message);
    }

    RestLibraryCatalogContext currentContext;
    QStringList requestedScopes;
    QStringList requestedCursors;
    QString reconciledScope;
    QList<RestLibraryTrack> reconciledTracks;
    QList<RestLibraryTrack> resolvedTracks;
    RestLibraryCacheRequestOwner lastResolveOwner =
            RestLibraryCacheRequestOwner::BrowserLoad;
    QSet<RestLibraryCacheRequestOwner> canceledAudioOwners;
    int pageCancelCount = 0;
};

} // namespace

class RestLibraryBrowserFeatureTest : public LibraryTest {
  public:
    RestLibraryBrowserFeatureTest()
            : m_backend(nullptr, &m_network) {
        config()->setValue(restConfig::kEnabledKey, true);
        config()->setValue(
                restConfig::kBaseUrlKey,
                QStringLiteral("http://127.0.0.1:8765"));
        config()->setValue(restConfig::kUseMixManDefaultsKey, true);
        config()->setValue(restConfig::kCacheEnabledKey, true);
        config()->setValue(restConfig::kCacheDirectoryKey, m_cacheDir.path());
        config()->setValue(restConfig::kPageSizeKey, 2);
        config()->setValue(
                ConfigKey(QStringLiteral("[Controls]"),
                        QStringLiteral("LoadWhenDeckPlaying")),
                static_cast<int>(LoadWhenDeckPlaying::Reject));
        PlaylistDAO& playlistDao = internalCollection()->getPlaylistDAO();
        if (playlistDao.getPlaylistIdFromName(AUTODJ_TABLE) < 0) {
            playlistDao.createPlaylist(AUTODJ_TABLE, PlaylistDAO::PLHT_AUTO_DJ);
        }
        m_pFeature.reset(new RestLibraryBrowserFeature(
                nullptr,
                config(),
                &m_backend,
                trackCollectionManager(),
                &m_loudness));
    }

  protected:
    RestLibraryTableModel* model() {
        return m_pFeature->m_pTableModel.get();
    }

    void activate() {
        m_pFeature->activate();
    }

    void refresh() {
        m_pFeature->slotRefresh();
    }

    RestLibraryCatalogContext providerContext() const {
        return m_pFeature->m_pCatalogProvider->context();
    }

    bool resetForCurrentContext() {
        return m_pFeature->resetIfContextChanged(providerContext());
    }

    FakeCatalogProvider* useFakeProvider() {
        m_pFeature.reset();
        m_pFakeProvider = std::make_unique<FakeCatalogProvider>();
        m_pFeature.reset(new RestLibraryBrowserFeature(
                nullptr,
                config(),
                &m_backend,
                trackCollectionManager(),
                &m_loudness,
                m_pFakeProvider.get()));
        return m_pFakeProvider.get();
    }

    void requestDefaultLoad(const QModelIndex& index) {
        m_pFeature->slotUnresolvedTrackLoad(index);
    }

    void requestPlayerLoad(
            const QModelIndex& index,
            const QString& group,
            bool play) {
#ifdef __STEM__
        m_pFeature->slotUnresolvedTrackLoadToPlayer(
                index,
                group,
                mixxx::StemChannel::All,
                play);
#else
        m_pFeature->slotUnresolvedTrackLoadToPlayer(index, group, play);
#endif
    }

    void requestAutoDJ(
            const QModelIndexList& indices,
            PlaylistDAO::AutoDJSendLoc location) {
        m_pFeature->slotUnresolvedTracksAddToAutoDJ(indices, location);
    }

    MockNetworkAccessManager m_network;
    QTemporaryDir m_cacheDir;
    RestLibraryBackend m_backend;
    FakeLoudnessManager m_loudness;
    std::unique_ptr<FakeCatalogProvider> m_pFakeProvider;
    std::unique_ptr<RestLibraryBrowserFeature> m_pFeature;
};

TEST_F(RestLibraryBrowserFeatureTest, FakeProviderProvesPaginationScopeAndAtomicRefresh) {
    FakeCatalogProvider* pProvider = useFakeProvider();
    RestLibraryTrack first;
    first.remoteId = QStringLiteral("1");
    first.title = QStringLiteral("One");
    RestLibraryTrack second;
    second.remoteId = QStringLiteral("2");
    second.title = QStringLiteral("Two");
    RestLibraryTrack duplicate = second;
    duplicate.title = QStringLiteral("Duplicate");
    RestLibraryTrack third;
    third.remoteId = QStringLiteral("3");
    third.title = QStringLiteral("Three");

    activate();
    ASSERT_EQ(pProvider->requestedCursors, QStringList{QString()});
    pProvider->completePage(
            pProvider->currentContext.scopeIdentity,
            {{first, second}, QStringLiteral("next")});
    ASSERT_EQ(pProvider->requestedCursors,
            (QStringList{QString(), QStringLiteral("next")}));
    pProvider->completePage(
            pProvider->currentContext.scopeIdentity,
            {{duplicate, third}, {}});

    ASSERT_EQ(model()->trackCount(), 3);
    EXPECT_EQ(model()->trackForRemoteId(QStringLiteral("2")).title,
            QStringLiteral("Two"));
    EXPECT_EQ(pProvider->reconciledTracks.size(), 3);
    EXPECT_EQ(pProvider->reconciledScope, pProvider->currentContext.scopeIdentity);

    refresh();
    pProvider->completePage(QStringLiteral("fake:stale"), {{third}, {}});
    EXPECT_EQ(model()->trackCount(), 3);
    pProvider->failPage(
            pProvider->currentContext.scopeIdentity,
            QStringLiteral("temporary failure"));
    EXPECT_EQ(model()->trackCount(), 3);

    pProvider->currentContext.scopeIdentity = QStringLiteral("fake:scope-b");
    pProvider->currentContext.cacheIdentity = QStringLiteral("fake-cache-b");
    refresh();
    EXPECT_EQ(model()->trackCount(), 0);
    EXPECT_TRUE(pProvider->canceledAudioOwners.contains(
            RestLibraryCacheRequestOwner::BrowserLoad));
    EXPECT_TRUE(pProvider->canceledAudioOwners.contains(
            RestLibraryCacheRequestOwner::BrowserAutoDJ));
}

TEST_F(RestLibraryBrowserFeatureTest, LoadsAndDeduplicatesMultipleCatalogPages) {
    MockNetworkReply* pFirst = m_network.ExpectGet(
            QStringLiteral("/tracks"),
            {{QStringLiteral("include_details"), QStringLiteral("true")},
                    {QStringLiteral("limit"), QStringLiteral("2")}},
            200,
            catalogPage(
                    QStringLiteral(
                            R"json({"id":3,"title":"Three"},{"id":2,"title":"Two"})json"),
                    QStringLiteral("cursor-2")));
    MockNetworkReply* pSecond = m_network.ExpectGet(
            QStringLiteral("/tracks"),
            {{QStringLiteral("cursor"), QStringLiteral("cursor-2")}},
            200,
            catalogPage(QStringLiteral(
                    R"json({"id":2,"title":"Duplicate"},{"id":1,"title":"One"})json")));

    activate();
    pFirst->Done(true);
    EXPECT_EQ(model()->trackCount(), 0);
    pSecond->Done(true);

    EXPECT_EQ(model()->trackCount(), 3);
    EXPECT_EQ(model()->trackForRemoteId(QStringLiteral("2")).title,
            QStringLiteral("Two"));
}

TEST_F(RestLibraryBrowserFeatureTest, FailedRefreshRetainsCompletedCatalog) {
    MockNetworkReply* pInitial = m_network.ExpectGet(
            QStringLiteral("/tracks"),
            {},
            200,
            catalogPage(QStringLiteral(R"json({"id":1,"title":"Keep Me"})json")));
    activate();
    pInitial->Done(true);
    ASSERT_EQ(model()->trackCount(), 1);

    MockNetworkReply* pFailure = m_network.ExpectGet(
            QStringLiteral("/tracks"),
            {},
            503,
            QByteArrayLiteral(R"json({"detail":"temporarily unavailable"})json"));
    refresh();
    pFailure->Done(true);

    ASSERT_EQ(model()->trackCount(), 1);
    EXPECT_EQ(model()->trackForRemoteId(QStringLiteral("1")).title,
            QStringLiteral("Keep Me"));
}

TEST_F(RestLibraryBrowserFeatureTest, SettingsChangeRestartsCatalogOnNewOrigin) {
    MockNetworkReply* pOldOrigin = m_network.ExpectGet(
            QStringLiteral("127.0.0.1:8765/tracks"),
            {},
            200,
            catalogPage(QStringLiteral(R"json({"id":1,"title":"Stale"})json")));
    activate();

    config()->setValue(
            restConfig::kBaseUrlKey,
            QStringLiteral("http://127.0.0.1:8766"));
    MockNetworkReply* pNewOrigin = m_network.ExpectGet(
            QStringLiteral("127.0.0.1:8766/tracks"),
            {},
            200,
            catalogPage(QStringLiteral(R"json({"id":2,"title":"Current"})json")));
    pOldOrigin->Done(true);
    EXPECT_EQ(model()->trackCount(), 0);

    pNewOrigin->Done(true);

    ASSERT_EQ(model()->trackCount(), 1);
    EXPECT_EQ(model()->trackForRemoteId(QStringLiteral("2")).title,
            QStringLiteral("Current"));
    EXPECT_TRUE(model()->trackForRemoteId(QStringLiteral("1")).remoteId.isEmpty());
}

TEST_F(RestLibraryBrowserFeatureTest, ProviderScopeChangeAndLogoutClearCatalogImmediately) {
    MockNetworkReply* pInitial = m_network.ExpectGet(
            QStringLiteral("/tracks"),
            {},
            200,
            catalogPage(QStringLiteral(R"json({"id":1,"title":"Account A"})json")));
    activate();
    pInitial->Done(true);
    ASSERT_EQ(model()->trackCount(), 1);

    MockNetworkReply* pPending = m_network.ExpectGet(
            QStringLiteral("/tracks"),
            {},
            200,
            catalogPage(QStringLiteral(R"json({"id":2,"title":"Stale"})json")));
    refresh();
    config()->setValue(
            restConfig::kBaseUrlKey,
            QStringLiteral("http://127.0.0.1:8766"));
    EXPECT_TRUE(resetForCurrentContext());
    EXPECT_TRUE(pPending->WasAborted());
    EXPECT_EQ(model()->trackCount(), 0);

    RestLibrarySettings accountA = RestLibrarySettings::fromConfig(config());
    RestLibraryTrack accountATrack;
    accountATrack.remoteId = QStringLiteral("7");
    accountATrack.title = QStringLiteral("Account A cached catalog");
    accountATrack.audioFileExtension = QStringLiteral("mp3");
    model()->setTracks({accountATrack});
    ASSERT_EQ(model()->trackCount(), 1);
    MockNetworkReply* pAccountAAudio = m_network.ExpectGet(
            QStringLiteral("127.0.0.1:8766/download"),
            {{QStringLiteral("track_id"), QStringLiteral("7")}},
            200,
            QByteArrayLiteral("account a audio"));
    m_backend.cacheManager()->cacheTracks({accountATrack}, accountA);

    config()->setValue(
            restConfig::kBaseUrlKey,
            QStringLiteral("http://127.0.0.1:8767"));
    EXPECT_TRUE(resetForCurrentContext());
    EXPECT_TRUE(pAccountAAudio->WasAborted());
    EXPECT_EQ(model()->trackCount(), 0);

    model()->setTracks({accountATrack});
    config()->setValue(restConfig::kEnabledKey, false);
    EXPECT_TRUE(resetForCurrentContext());
    EXPECT_EQ(model()->trackCount(), 0);
}

TEST_F(RestLibraryBrowserFeatureTest, ExplicitLoadWaitsForCacheCompletion) {
    MockNetworkReply* pCatalog = m_network.ExpectGet(
            QStringLiteral("/tracks"),
            {},
            200,
            catalogPage(QStringLiteral(
                    R"json({"id":7,"title":"Load Me","artist":"Ada","download_file_extension":"mp3"})json")));
    activate();
    pCatalog->Done(true);
    ASSERT_EQ(model()->trackCount(), 1);

    MockNetworkReply* pAudio = m_network.ExpectGet(
            QStringLiteral("/download"),
            {{QStringLiteral("track_id"), QStringLiteral("7")}},
            200,
            QByteArrayLiteral("audio bytes"));
    QSignalSpy loadSpy(m_pFeature.get(), &LibraryFeature::loadTrack);
    QSignalSpy statusSpy(
            m_pFeature.get(),
            &RestLibraryBrowserFeature::statusTextChanged);
    requestDefaultLoad(model()->index(0, 0));
    EXPECT_EQ(loadSpy.count(), 0);
    ASSERT_GT(statusSpy.count(), 0);
    EXPECT_TRUE(statusSpy.last().at(0).toString().contains(QStringLiteral("Downloading")));

    pAudio->Done(true);

    ASSERT_EQ(loadSpy.count(), 1);
    const TrackPointer pTrack = qvariant_cast<TrackPointer>(loadSpy.takeFirst().at(0));
    ASSERT_TRUE(pTrack);
    EXPECT_TRUE(QFile::exists(pTrack->getLocation()));
    ASSERT_GT(statusSpy.count(), 1);
    const QString completedStatus = statusSpy.last().at(0).toString();
    EXPECT_FALSE(completedStatus.contains(QStringLiteral("Downloading")));
    EXPECT_TRUE(completedStatus.contains(QStringLiteral("1 REST Library track")));
    EXPECT_TRUE(completedStatus.contains(QStringLiteral("1 cached")));
}

TEST_F(RestLibraryBrowserFeatureTest, ExplicitLoadWaitsForReplayGainPreparation) {
    m_loudness.defer = true;
    MockNetworkReply* pCatalog = m_network.ExpectGet(
            QStringLiteral("/tracks"),
            {},
            200,
            catalogPage(QStringLiteral(
                    R"json({"id":17,"title":"Quiet Master","artist":"Ada","download_file_extension":"wav"})json")));
    activate();
    pCatalog->Done(true);
    MockNetworkReply* pAudio = m_network.ExpectGet(
            QStringLiteral("/download"),
            {{QStringLiteral("track_id"), QStringLiteral("17")}},
            200,
            QByteArrayLiteral("audio bytes"));
    QSignalSpy loadSpy(m_pFeature.get(), &LibraryFeature::loadTrack);

    requestDefaultLoad(model()->index(0, 0));
    pAudio->Done(true);

    EXPECT_EQ(loadSpy.count(), 0);
    const TrackPointer pTrack = model()->materializeTrack(QStringLiteral("17"));
    ASSERT_TRUE(pTrack);
    EXPECT_FALSE(pTrack->getReplayGain().hasRatio());

    m_loudness.complete(pTrack, true);

    ASSERT_EQ(loadSpy.count(), 1);
    EXPECT_TRUE(pTrack->getReplayGain().hasRatio());
    EXPECT_DOUBLE_EQ(pTrack->getReplayGain().getRatio(), 2.0);

    loadSpy.clear();
    requestDefaultLoad(model()->index(0, 0));
    EXPECT_EQ(loadSpy.count(), 1);
}

TEST_F(RestLibraryBrowserFeatureTest, ReplayGainFailureKeepsManualTrackUnloaded) {
    m_loudness.defer = true;
    MockNetworkReply* pCatalog = m_network.ExpectGet(
            QStringLiteral("/tracks"),
            {},
            200,
            catalogPage(QStringLiteral(
                    R"json({"id":18,"title":"Broken","download_file_extension":"wav"})json")));
    activate();
    pCatalog->Done(true);
    MockNetworkReply* pAudio = m_network.ExpectGet(
            QStringLiteral("/download"),
            {{QStringLiteral("track_id"), QStringLiteral("18")}},
            200,
            QByteArrayLiteral("audio bytes"));
    QSignalSpy loadSpy(m_pFeature.get(), &LibraryFeature::loadTrack);
    QSignalSpy statusSpy(
            m_pFeature.get(),
            &RestLibraryBrowserFeature::statusTextChanged);

    requestDefaultLoad(model()->index(0, 0));
    pAudio->Done(true);
    const TrackPointer pTrack = model()->materializeTrack(QStringLiteral("18"));
    ASSERT_TRUE(pTrack);
    m_loudness.complete(pTrack, false);

    EXPECT_EQ(loadSpy.count(), 0);
    ASSERT_GT(statusSpy.count(), 0);
    EXPECT_TRUE(statusSpy.last().at(0).toString().contains(
            QStringLiteral("ReplayGain analysis failed")));
}

TEST_F(RestLibraryBrowserFeatureTest, CachedTrackDoesNotLoadIntoBusyDeck) {
    MockNetworkReply* pCatalog = m_network.ExpectGet(
            QStringLiteral("/tracks"),
            {},
            200,
            catalogPage(QStringLiteral(
                    R"json({"id":8,"title":"Wait","artist":"Ada","download_file_extension":"mp3"})json")));
    activate();
    pCatalog->Done(true);
    MockNetworkReply* pAudio = m_network.ExpectGet(
            QStringLiteral("/download"),
            {{QStringLiteral("track_id"), QStringLiteral("8")}},
            200,
            QByteArrayLiteral("audio bytes"));
    requestDefaultLoad(model()->index(0, 0));
    pAudio->Done(true);

    const QString group = QStringLiteral("[Channel1]");
    ControlObject playControl(ConfigKey(group, QStringLiteral("play")));
    playControl.set(1.0);
    QSignalSpy loadSpy(m_pFeature.get(), &LibraryFeature::loadTrackToPlayer);
    QSignalSpy statusSpy(
            m_pFeature.get(),
            &RestLibraryBrowserFeature::statusTextChanged);

    requestPlayerLoad(model()->index(0, 0), group, true);

    EXPECT_EQ(loadSpy.count(), 0);
    ASSERT_GT(statusSpy.count(), 0);
    EXPECT_TRUE(statusSpy.last().at(0).toString().contains(
            QStringLiteral("destination deck became busy")));
}

TEST_F(RestLibraryBrowserFeatureTest, AutoDJBatchPreservesSelectionOrder) {
    MockNetworkReply* pCatalog = m_network.ExpectGet(
            QStringLiteral("/tracks"),
            {},
            200,
            catalogPage(QStringLiteral(
                    R"json({"id":12,"title":"First","download_file_extension":"mp3"},{"id":11,"title":"Second","download_file_extension":"mp3"})json")));
    activate();
    pCatalog->Done(true);
    ASSERT_EQ(model()->trackCount(), 2);

    const QModelIndex firstIndex = model()->index(0, 0);
    const QModelIndex secondIndex = model()->index(1, 0);
    const QString firstRemoteId = model()->remoteIdForIndex(firstIndex);
    const QString secondRemoteId = model()->remoteIdForIndex(secondIndex);
    MockNetworkReply* pFirstAudio = m_network.ExpectGet(
            QStringLiteral("/download"),
            {{QStringLiteral("track_id"), firstRemoteId}},
            200,
            QByteArrayLiteral("first audio"));
    MockNetworkReply* pSecondAudio = m_network.ExpectGet(
            QStringLiteral("/download"),
            {{QStringLiteral("track_id"), secondRemoteId}},
            200,
            QByteArrayLiteral("second audio"));
    requestAutoDJ(
            {firstIndex, secondIndex},
            PlaylistDAO::AutoDJSendLoc::BOTTOM);

    PlaylistDAO& playlistDao = internalCollection()->getPlaylistDAO();
    const int autoDJPlaylistId =
            playlistDao.getPlaylistIdFromName(AUTODJ_TABLE);
    pSecondAudio->Done(true);
    EXPECT_TRUE(playlistDao.getTrackIdsInPlaylistOrder(autoDJPlaylistId).isEmpty());
    pFirstAudio->Done(true);

    const TrackPointer pFirst = model()->materializeTrack(firstRemoteId);
    const TrackPointer pSecond = model()->materializeTrack(secondRemoteId);
    ASSERT_TRUE(pFirst);
    ASSERT_TRUE(pSecond);
    EXPECT_EQ(playlistDao.getTrackIdsInPlaylistOrder(autoDJPlaylistId),
            (QList<TrackId>{pFirst->getId(), pSecond->getId()}));
}

TEST_F(RestLibraryBrowserFeatureTest, AutoDJWaitsForAllReplayGainPreparation) {
    m_loudness.defer = true;
    MockNetworkReply* pCatalog = m_network.ExpectGet(
            QStringLiteral("/tracks"),
            {},
            200,
            catalogPage(QStringLiteral(
                    R"json({"id":32,"title":"First","download_file_extension":"wav"},{"id":31,"title":"Second","download_file_extension":"wav"})json")));
    activate();
    pCatalog->Done(true);
    const QModelIndex firstIndex = model()->index(0, 0);
    const QModelIndex secondIndex = model()->index(1, 0);
    const QString firstRemoteId = model()->remoteIdForIndex(firstIndex);
    const QString secondRemoteId = model()->remoteIdForIndex(secondIndex);
    MockNetworkReply* pFirstAudio = m_network.ExpectGet(
            QStringLiteral("/download"),
            {{QStringLiteral("track_id"), firstRemoteId}},
            200,
            QByteArrayLiteral("first audio"));
    MockNetworkReply* pSecondAudio = m_network.ExpectGet(
            QStringLiteral("/download"),
            {{QStringLiteral("track_id"), secondRemoteId}},
            200,
            QByteArrayLiteral("second audio"));

    requestAutoDJ(
            {firstIndex, secondIndex},
            PlaylistDAO::AutoDJSendLoc::BOTTOM);
    pFirstAudio->Done(true);
    pSecondAudio->Done(true);

    PlaylistDAO& playlistDao = internalCollection()->getPlaylistDAO();
    const int autoDJPlaylistId =
            playlistDao.getPlaylistIdFromName(AUTODJ_TABLE);
    EXPECT_TRUE(playlistDao.getTrackIdsInPlaylistOrder(autoDJPlaylistId).isEmpty());
    const TrackPointer pFirst = model()->materializeTrack(firstRemoteId);
    const TrackPointer pSecond = model()->materializeTrack(secondRemoteId);
    ASSERT_TRUE(pFirst);
    ASSERT_TRUE(pSecond);

    m_loudness.complete(pSecond, true);
    EXPECT_TRUE(playlistDao.getTrackIdsInPlaylistOrder(autoDJPlaylistId).isEmpty());
    m_loudness.complete(pFirst, true);

    EXPECT_EQ(playlistDao.getTrackIdsInPlaylistOrder(autoDJPlaylistId),
            (QList<TrackId>{pFirst->getId(), pSecond->getId()}));
}
