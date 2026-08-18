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
                trackCollectionManager()));
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

    bool resetForSettings(const RestLibrarySettings& settings) {
        return m_pFeature->resetIfSettingsChanged(settings);
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
    std::unique_ptr<RestLibraryBrowserFeature> m_pFeature;
};

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

TEST_F(RestLibraryBrowserFeatureTest, CredentialRotationAndLogoutClearCatalogImmediately) {
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
    RestLibrarySettings accountA = RestLibrarySettings::fromConfig(config());
    accountA.bearerToken = QStringLiteral("account-a-secret-token");
    EXPECT_TRUE(resetForSettings(accountA));
    EXPECT_TRUE(pPending->WasAborted());
    EXPECT_EQ(model()->trackCount(), 0);

    RestLibraryTrack accountATrack;
    accountATrack.remoteId = QStringLiteral("7");
    accountATrack.title = QStringLiteral("Account A cached catalog");
    accountATrack.audioFileExtension = QStringLiteral("mp3");
    model()->setTracks({accountATrack});
    ASSERT_EQ(model()->trackCount(), 1);
    MockNetworkReply* pAccountAAudio = m_network.ExpectGet(
            QStringLiteral("/download"),
            {{QStringLiteral("track_id"), QStringLiteral("7")}},
            200,
            QByteArrayLiteral("account a audio"));
    m_backend.cacheManager()->cacheTracks({accountATrack}, accountA);

    RestLibrarySettings rotated = accountA;
    rotated.bearerToken = QStringLiteral("account-b-rotated-token");
    EXPECT_TRUE(resetForSettings(rotated));
    EXPECT_TRUE(pAccountAAudio->WasAborted());
    EXPECT_EQ(model()->trackCount(), 0);

    model()->setTracks({accountATrack});
    RestLibrarySettings loggedOut = rotated;
    loggedOut.bearerToken.clear();
    EXPECT_TRUE(resetForSettings(loggedOut));
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
    requestDefaultLoad(model()->index(0, 0));
    EXPECT_EQ(loadSpy.count(), 0);

    pAudio->Done(true);

    ASSERT_EQ(loadSpy.count(), 1);
    const TrackPointer pTrack = qvariant_cast<TrackPointer>(loadSpy.takeFirst().at(0));
    ASSERT_TRUE(pTrack);
    EXPECT_TRUE(QFile::exists(pTrack->getLocation()));
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
