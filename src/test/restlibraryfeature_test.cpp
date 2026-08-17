#include <gtest/gtest.h>

#include <memory>

#include <QSignalSpy>
#include <QTemporaryDir>

#include "library/dao/playlistdao.h"
#include "library/rest/restlibrarybackend.h"
#include "library/rest/restlibraryfeature.h"
#include "library/rest/restlibrarysettings.h"
#include "library/rest/restlibrarytrack.h"
#include "test/librarytest.h"
#include "test/mock_networkaccessmanager.h"
#include "track/track.h"

namespace {

namespace restConfig = mixxx::library::rest::config;
using mixxx::library::rest::RestLibraryBackend;
using mixxx::library::rest::RestLibraryFeature;
using mixxx::library::rest::RestLibraryTrack;

RestLibraryTrack recommendation(const QString& remoteId, const QString& title) {
    RestLibraryTrack track;
    track.remoteId = remoteId;
    track.title = title;
    track.audioFileExtension = QStringLiteral("mp3");
    return track;
}

} // namespace

class RestLibraryFeatureTest : public LibraryTest {
  public:
    RestLibraryFeatureTest()
            : m_backend(nullptr, &m_network) {
        config()->setValue(restConfig::kEnabledKey, true);
        config()->setValue(
                restConfig::kBaseUrlKey,
                QStringLiteral("http://127.0.0.1:8765"));
        config()->setValue(restConfig::kUseMixManDefaultsKey, true);
        config()->setValue(restConfig::kCacheEnabledKey, true);
        config()->setValue(restConfig::kCacheDirectoryKey, m_cacheDir.path());

        PlaylistDAO& playlistDao = internalCollection()->getPlaylistDAO();
        if (playlistDao.getPlaylistIdFromName(AUTODJ_TABLE) < 0) {
            playlistDao.createPlaylist(AUTODJ_TABLE, PlaylistDAO::PLHT_AUTO_DJ);
        }
        m_pFeature.reset(new RestLibraryFeature(
                nullptr,
                config(),
                &m_backend,
                nullptr,
                trackCollectionManager()));
    }

  protected:
    void setRecommendations(const QList<RestLibraryTrack>& tracks) {
        m_pFeature->setRecommendationTracks(tracks);
    }

    void queueRecommendations() {
        m_pFeature->queueRecommendationsForAutoDJ();
    }

    QList<TrackId> autoDJTrackIds() {
        PlaylistDAO& playlistDao = internalCollection()->getPlaylistDAO();
        return playlistDao.getTrackIdsInPlaylistOrder(
                playlistDao.getPlaylistIdFromName(AUTODJ_TABLE));
    }

    MockNetworkAccessManager m_network;
    QTemporaryDir m_cacheDir;
    RestLibraryBackend m_backend;
    std::unique_ptr<RestLibraryFeature> m_pFeature;
};

TEST_F(RestLibraryFeatureTest, AutoDJBatchWaitsForAllDownloadsAndPreservesOrder) {
    MockNetworkReply* pFirstAudio = m_network.ExpectGet(
            QStringLiteral("/download"),
            {{QStringLiteral("track_id"), QStringLiteral("12")}},
            200,
            QByteArrayLiteral("first audio"));
    MockNetworkReply* pSecondAudio = m_network.ExpectGet(
            QStringLiteral("/download"),
            {{QStringLiteral("track_id"), QStringLiteral("11")}},
            200,
            QByteArrayLiteral("second audio"));

    setRecommendations({recommendation(QStringLiteral("12"), QStringLiteral("First")),
            recommendation(QStringLiteral("11"), QStringLiteral("Second"))});
    queueRecommendations();

    pSecondAudio->Done(true);
    EXPECT_TRUE(autoDJTrackIds().isEmpty());
    pFirstAudio->Done(true);

    const TrackPointer pFirst =
            m_pFeature->m_pTableModel->materializeTrack(QStringLiteral("12"));
    const TrackPointer pSecond =
            m_pFeature->m_pTableModel->materializeTrack(QStringLiteral("11"));
    ASSERT_TRUE(pFirst);
    ASSERT_TRUE(pSecond);
    EXPECT_EQ(autoDJTrackIds(),
            (QList<TrackId>{pFirst->getId(), pSecond->getId()}));
    EXPECT_TRUE(m_pFeature->m_autoDJRemoteIds.isEmpty());
}

TEST_F(RestLibraryFeatureTest, AutoDJBatchQueuesSuccessfulTracksAfterPartialFailure) {
    MockNetworkReply* pFirstAudio = m_network.ExpectGet(
            QStringLiteral("/download"),
            {{QStringLiteral("track_id"), QStringLiteral("21")}},
            200,
            QByteArrayLiteral("good audio"));
    MockNetworkReply* pSecondAudio = m_network.ExpectGet(
            QStringLiteral("/download"),
            {{QStringLiteral("track_id"), QStringLiteral("22")}},
            503,
            QByteArrayLiteral("temporarily unavailable"));
    QSignalSpy statusSpy(m_pFeature.get(), &RestLibraryFeature::statusTextChanged);

    setRecommendations({recommendation(QStringLiteral("21"), QStringLiteral("Good")),
            recommendation(QStringLiteral("22"), QStringLiteral("Unavailable"))});
    queueRecommendations();
    pFirstAudio->Done(true);
    EXPECT_TRUE(autoDJTrackIds().isEmpty());
    pSecondAudio->Done(true);

    const TrackPointer pGood =
            m_pFeature->m_pTableModel->materializeTrack(QStringLiteral("21"));
    ASSERT_TRUE(pGood);
    EXPECT_EQ(autoDJTrackIds(), (QList<TrackId>{pGood->getId()}));
    ASSERT_GT(statusSpy.count(), 0);
    EXPECT_TRUE(statusSpy.last().at(0).toString().contains(QStringLiteral("1 failed")));
}

TEST_F(RestLibraryFeatureTest, UnchangedCandidatesDoNotCancelActiveAutoDJBatch) {
    MockNetworkReply* pFirstAudio = m_network.ExpectGet(
            QStringLiteral("/download"),
            {{QStringLiteral("track_id"), QStringLiteral("31")}},
            200,
            QByteArrayLiteral("first audio"));
    MockNetworkReply* pSecondAudio = m_network.ExpectGet(
            QStringLiteral("/download"),
            {{QStringLiteral("track_id"), QStringLiteral("32")}},
            200,
            QByteArrayLiteral("second audio"));
    const QList<RestLibraryTrack> tracks{
            recommendation(QStringLiteral("31"), QStringLiteral("First")),
            recommendation(QStringLiteral("32"), QStringLiteral("Second"))};

    setRecommendations(tracks);
    queueRecommendations();
    ASSERT_EQ(m_pFeature->m_autoDJRemoteIds,
            (QStringList{QStringLiteral("31"), QStringLiteral("32")}));

    QList<RestLibraryTrack> updatedTracks = tracks;
    updatedTracks[0].title = QStringLiteral("Updated metadata");
    setRecommendations(updatedTracks);

    EXPECT_EQ(m_pFeature->m_autoDJRemoteIds,
            (QStringList{QStringLiteral("31"), QStringLiteral("32")}));
    EXPECT_EQ(m_pFeature->m_autoDJPendingIds.size(), 2);
    pFirstAudio->Done(true);
    pSecondAudio->Done(true);
}

TEST_F(RestLibraryFeatureTest, ChangedCandidatesCancelActiveAutoDJBatch) {
    MockNetworkReply* pOldAudio = m_network.ExpectGet(
            QStringLiteral("/download"),
            {{QStringLiteral("track_id"), QStringLiteral("41")}},
            200,
            QByteArrayLiteral("old audio"));
    setRecommendations(
            {recommendation(QStringLiteral("41"), QStringLiteral("Old"))});
    queueRecommendations();
    ASSERT_FALSE(m_pFeature->m_autoDJRemoteIds.isEmpty());

    MockNetworkReply* pNewAudio = m_network.ExpectGet(
            QStringLiteral("/download"),
            {{QStringLiteral("track_id"), QStringLiteral("42")}},
            200,
            QByteArrayLiteral("new audio"));
    setRecommendations(
            {recommendation(QStringLiteral("42"), QStringLiteral("New"))});

    EXPECT_TRUE(m_pFeature->m_autoDJRemoteIds.isEmpty());
    EXPECT_TRUE(m_pFeature->m_autoDJPendingIds.isEmpty());
    EXPECT_TRUE(pOldAudio->WasAborted());
    pNewAudio->Done(true);
}
