#include <gtest/gtest.h>

#include <memory>

#include <QDir>
#include <QFile>
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
using mixxx::library::rest::RestLibraryLoudnessManager;
using mixxx::library::rest::RestLibraryLoudnessResult;
using mixxx::library::rest::RestLibraryLoudnessState;
using mixxx::library::rest::RestLibraryTrack;

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
        return {pTrack->getId(), RestLibraryLoudnessState::Analyzing, {}};
    }

    void complete(const TrackPointer& pTrack, bool success) {
        ASSERT_TRUE(pTrack);
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
    QSet<TrackId> m_readyTrackIds;
};

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
                trackCollectionManager(),
                &m_loudness));
    }

  protected:
    void setRecommendations(const QList<RestLibraryTrack>& tracks) {
        m_pFeature->setRecommendationTracks(tracks);
    }

    void queueRecommendations() {
        m_pFeature->queueRecommendationsForAutoDJ();
    }

    void requestPlayerLoad(
            const TrackPointer& pTrack,
            const QString& group,
            bool play) {
        m_pFeature->slotLoadTrackToPlayerRequested(pTrack, group, play);
    }

    void failFetch(const QString& message) {
        m_pFeature->slotFetchFailed(message);
    }

    QList<TrackId> autoDJTrackIds() {
        PlaylistDAO& playlistDao = internalCollection()->getPlaylistDAO();
        return playlistDao.getTrackIdsInPlaylistOrder(
                playlistDao.getPlaylistIdFromName(AUTODJ_TABLE));
    }

    MockNetworkAccessManager m_network;
    QTemporaryDir m_cacheDir;
    RestLibraryBackend m_backend;
    FakeLoudnessManager m_loudness;
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

TEST_F(RestLibraryFeatureTest, AutoDJUsesExplicitlySortedDisplayOrder) {
    MockNetworkReply* pPrimaryAudio = m_network.ExpectGet(
            QStringLiteral("/download"),
            {{QStringLiteral("track_id"), QStringLiteral("82")}},
            200,
            QByteArrayLiteral("primary audio"));
    MockNetworkReply* pAlternateAudio = m_network.ExpectGet(
            QStringLiteral("/download"),
            {{QStringLiteral("track_id"), QStringLiteral("81")}},
            200,
            QByteArrayLiteral("alternate audio"));

    RestLibraryTrack primary =
            recommendation(QStringLiteral("82"), QStringLiteral("Primary"));
    primary.artist = QStringLiteral("Zulu");
    primary.recommendationPosition = 1;
    RestLibraryTrack alternate =
            recommendation(QStringLiteral("81"), QStringLiteral("Alternate"));
    alternate.artist = QStringLiteral("Alpha");
    alternate.recommendationPosition = 2;
    setRecommendations({primary, alternate});

    const int artistColumn =
            m_pFeature->m_pTableModel->fieldIndex(QStringLiteral("artist"));
    m_pFeature->m_pTableModel->sort(artistColumn, Qt::AscendingOrder);
    ASSERT_EQ(m_pFeature->m_pTableModel->remoteIdForIndex(
                      m_pFeature->m_pTableModel->index(0, 0)),
            QStringLiteral("81"));
    queueRecommendations();

    pPrimaryAudio->Done(true);
    pAlternateAudio->Done(true);

    const TrackPointer pAlternate =
            m_pFeature->m_pTableModel->materializeTrack(QStringLiteral("81"));
    const TrackPointer pPrimary =
            m_pFeature->m_pTableModel->materializeTrack(QStringLiteral("82"));
    ASSERT_TRUE(pAlternate);
    ASSERT_TRUE(pPrimary);
    EXPECT_EQ(autoDJTrackIds(),
            (QList<TrackId>{pAlternate->getId(), pPrimary->getId()}));
}

TEST_F(RestLibraryFeatureTest, AutoDJBatchWaitsForReplayGainPreparation) {
    m_loudness.defer = true;
    MockNetworkReply* pFirstAudio = m_network.ExpectGet(
            QStringLiteral("/download"),
            {{QStringLiteral("track_id"), QStringLiteral("52")}},
            200,
            QByteArrayLiteral("first audio"));
    MockNetworkReply* pSecondAudio = m_network.ExpectGet(
            QStringLiteral("/download"),
            {{QStringLiteral("track_id"), QStringLiteral("51")}},
            200,
            QByteArrayLiteral("second audio"));

    setRecommendations({recommendation(QStringLiteral("52"), QStringLiteral("First")),
            recommendation(QStringLiteral("51"), QStringLiteral("Second"))});
    queueRecommendations();
    pFirstAudio->Done(true);
    pSecondAudio->Done(true);

    EXPECT_TRUE(autoDJTrackIds().isEmpty());
    const TrackPointer pFirst =
            m_pFeature->m_pTableModel->materializeTrack(QStringLiteral("52"));
    const TrackPointer pSecond =
            m_pFeature->m_pTableModel->materializeTrack(QStringLiteral("51"));
    ASSERT_TRUE(pFirst);
    ASSERT_TRUE(pSecond);
    m_loudness.complete(pSecond, true);
    EXPECT_TRUE(autoDJTrackIds().isEmpty());
    m_loudness.complete(pFirst, true);

    EXPECT_EQ(autoDJTrackIds(),
            (QList<TrackId>{pFirst->getId(), pSecond->getId()}));
}

TEST_F(RestLibraryFeatureTest, ManualPlayerLoadWaitsForReplayGainPreparation) {
    MockNetworkReply* pAudio = m_network.ExpectGet(
            QStringLiteral("/download"),
            {{QStringLiteral("track_id"), QStringLiteral("61")}},
            200,
            QByteArrayLiteral("audio"));
    setRecommendations(
            {recommendation(QStringLiteral("61"), QStringLiteral("Quiet Master"))});
    pAudio->Done(true);
    const TrackPointer pTrack =
            m_pFeature->m_pTableModel->materializeTrack(QStringLiteral("61"));
    ASSERT_TRUE(pTrack);
    m_loudness.defer = true;
    QSignalSpy loadSpy(m_pFeature.get(), &LibraryFeature::loadTrackToPlayer);

    requestPlayerLoad(pTrack, QStringLiteral("[Channel1]"), true);

    EXPECT_EQ(loadSpy.count(), 0);
    EXPECT_FALSE(pTrack->getReplayGain().hasRatio());
    m_loudness.complete(pTrack, true);

    ASSERT_EQ(loadSpy.count(), 1);
    EXPECT_TRUE(pTrack->getReplayGain().hasRatio());
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

TEST_F(RestLibraryFeatureTest, ServerLookupMappingQueuesExistingLocalTrackWithoutDownload) {
    const QString localPath =
            QDir(m_cacheDir.path()).filePath(QStringLiteral("ordinary-local.mp3"));
    QFile localFile(localPath);
    ASSERT_TRUE(localFile.open(QIODevice::WriteOnly));
    localFile.write("local audio");
    localFile.close();
    const TrackPointer pLocalTrack = getOrAddTrackByLocation(localPath);
    ASSERT_TRUE(pLocalTrack);

    m_pFeature->m_pendingTrackLookup = pLocalTrack;
    m_pFeature->slotTrackLookupSucceeded(QStringLiteral("77"));
    setRecommendations({recommendation(QStringLiteral("77"), QStringLiteral("Mapped"))});
    queueRecommendations();

    EXPECT_EQ(autoDJTrackIds(), (QList<TrackId>{pLocalTrack->getId()}));
    EXPECT_FALSE(m_pFeature->m_pTableModel->isCacheArtifact(pLocalTrack->getId()));
    EXPECT_EQ(m_pFeature->m_pTableModel->remoteIdForTrack(pLocalTrack),
            QStringLiteral("77"));
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
    QList<RestLibraryTrack> tracks{
            recommendation(QStringLiteral("31"), QStringLiteral("First")),
            recommendation(QStringLiteral("32"), QStringLiteral("Second"))};
    tracks[0].recommendationPosition = 1;
    tracks[0].color = QStringLiteral("#112233");
    tracks[1].recommendationPosition = 2;
    tracks[1].color = QStringLiteral("#445566");

    setRecommendations(tracks);
    queueRecommendations();
    ASSERT_EQ(m_pFeature->m_autoDJRemoteIds,
            (QStringList{QStringLiteral("31"), QStringLiteral("32")}));

    QList<RestLibraryTrack> updatedTracks = tracks;
    updatedTracks[0].title = QStringLiteral("Updated metadata");
    updatedTracks[0].recommendationPosition = 2;
    updatedTracks[0].color = QStringLiteral("#abcdef");
    updatedTracks[1].recommendationPosition = 1;
    updatedTracks[1].color = QStringLiteral("#fedcba");
    setRecommendations(updatedTracks);

    EXPECT_EQ(m_pFeature->m_autoDJRemoteIds,
            (QStringList{QStringLiteral("31"), QStringLiteral("32")}));
    EXPECT_EQ(m_pFeature->m_autoDJPendingIds.size(), 2);
    const auto* pModel = m_pFeature->m_pTableModel.get();
    ASSERT_NE(pModel, nullptr);
    EXPECT_EQ(pModel->remoteIdForIndex(pModel->index(0, 0)), QStringLiteral("32"));
    const int rankColumn = pModel->fieldIndex(QStringLiteral("recommendation_rank"));
    EXPECT_EQ(pModel->data(pModel->index(0, rankColumn)).toString(),
            QStringLiteral("Top"));
    const int colorColumn = pModel->fieldIndex(QStringLiteral("color"));
    EXPECT_EQ(pModel->data(
                          pModel->index(0, colorColumn),
                          TrackModel::kDataExportRole)
                          .toString(),
            QStringLiteral("#fedcba"));
    EXPECT_EQ(pModel->trackForRemoteId(QStringLiteral("31")).title,
            QStringLiteral("Updated metadata"));
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

TEST_F(RestLibraryFeatureTest, FailedRefreshRetainsLastValidRecommendations) {
    config()->setValue(restConfig::kCacheEnabledKey, false);
    const QList<RestLibraryTrack> tracks{
            recommendation(QStringLiteral("71"), QStringLiteral("Keep Me"))};
    setRecommendations(tracks);
    ASSERT_EQ(m_pFeature->m_pTableModel->trackCount(), 1);

    failFetch(QStringLiteral("MixMan policy path response was not valid JSON."));

    ASSERT_EQ(m_pFeature->m_pTableModel->trackCount(), 1);
    EXPECT_EQ(m_pFeature->m_pTableModel->trackForRemoteId(QStringLiteral("71")).title,
            QStringLiteral("Keep Me"));
}
