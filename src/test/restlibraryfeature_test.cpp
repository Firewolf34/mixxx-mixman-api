#include <gtest/gtest.h>

#include <memory>
#include <utility>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "library/dao/playlistdao.h"
#include "library/dao/trackschema.h"
#include "library/rest/restlibrarybackend.h"
#include "library/rest/restlibraryfeature.h"
#include "library/rest/restlibrarysettings.h"
#include "library/rest/restlibrarytrack.h"
#include "test/mock_networkaccessmanager.h"
#include "test/restlibrarytest.h"
#include "track/track.h"

namespace {

namespace restConfig = mixxx::library::rest::config;
using mixxx::library::rest::RestLibraryBackend;
using mixxx::library::rest::RestLibraryFeature;
using mixxx::library::rest::RestLibraryLoudnessManager;
using mixxx::library::rest::RestLibraryLoudnessResult;
using mixxx::library::rest::RestLibraryLoudnessState;
using mixxx::library::rest::RestLibraryPathStep;
using mixxx::library::rest::RestLibraryPolicyPath;
using mixxx::library::rest::RestLibrarySettings;
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

class RestLibraryFeatureTest : public RestLibraryTest {
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

    void setAutoDJTracks(const QList<RestLibraryTrack>& tracks) {
        setPolicyPath(tracks, tracks);
    }

    void setPolicyPath(
            const QList<RestLibraryTrack>& candidates,
            const QList<RestLibraryTrack>& pathTracks) {
        RestLibraryPolicyPath policyPath;
        policyPath.candidates = candidates;
        for (int index = 0; index < pathTracks.size(); ++index) {
            RestLibraryPathStep step;
            step.track = pathTracks[index];
            step.remoteId = pathTracks[index].remoteId;
            step.position = index + 1;
            policyPath.path.append(std::move(step));
        }
        setRecommendations(candidates);
        m_pFeature->setAutoDJPath(policyPath);
    }

    void queueRecommendations() {
        m_pFeature->m_autoDJPathIntent = true;
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

    mixxx::library::rest::RestLibraryTableModel* tableModel() const {
        return m_pFeature->m_pTableModel.get();
    }

    QStringList autoDJRemoteIds() const {
        return m_pFeature->m_autoDJRemoteIds;
    }

    int autoDJPendingCount() const {
        return m_pFeature->m_autoDJPendingIds.size();
    }

    bool autoDJPendingIsEmpty() const {
        return m_pFeature->m_autoDJPendingIds.isEmpty();
    }

    void mapPendingTrack(const TrackPointer& pTrack, const QString& remoteId) {
        m_pFeature->m_pendingTrackLookup = pTrack;
        m_pFeature->slotTrackLookupSucceeded(remoteId);
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

TEST_F(RestLibraryFeatureTest, SteeringEditsStayLocalUntilOneCompleteUpdate) {
    m_pFeature->m_mixManSession.id = QStringLiteral("session-1");
    m_pFeature->m_mixManRegistration.instance.instanceId = QStringLiteral("inst-1");

    m_pFeature->slotPolicyPresetChanged(QStringLiteral("explore"));
    m_pFeature->slotRecommendationLensChanged(QStringLiteral("semantic"));
    m_pFeature->slotTargetEnergyChanged(true, 2);
    m_pFeature->slotTargetColorChanged(true, QStringLiteral("#00ffff"));
    m_pFeature->slotTargetBpmChanged(true, 124);

    EXPECT_FALSE(config()->getValue<bool>(
            restConfig::kMixManPolicyOverrideEnabledKey, false));
    EXPECT_FALSE(config()->exists(restConfig::kMixManRecommendationLensKey));
    EXPECT_FALSE(config()->getValue<bool>(
            restConfig::kMixManTargetEnergyEnabledKey, false));

    MockNetworkReply* pReply = m_network.ExpectPost(
            QStringLiteral("/api/v3/sessions/session-1/actions"),
            {},
            {QStringLiteral("\"action_type\":\"policy_refresh\""),
                    QStringLiteral("\"policy_preset\":\"explore\""),
                    QStringLiteral("\"recommendation_lens\":\"semantic\""),
                    QStringLiteral("\"candidate_limit\":5"),
                    QStringLiteral("\"target_energy\":0.4"),
                    QStringLiteral("\"target_color\":\"#00ffff\""),
                    QStringLiteral("\"target_bpm\":124")},
            200,
            R"json({"session":{"id":"session-1"},"authoritative":{"session_id":"session-1"}})json");

    m_pFeature->slotUpdateSuggestionsRequested();
    pReply->Done();

    EXPECT_TRUE(config()->getValue<bool>(
            restConfig::kMixManPolicyOverrideEnabledKey, false));
    EXPECT_EQ(config()->getValueString(restConfig::kMixManPolicyPresetKey),
            QStringLiteral("explore"));
    EXPECT_EQ(config()->getValueString(restConfig::kMixManRecommendationLensKey),
            QStringLiteral("semantic"));
    EXPECT_TRUE(config()->getValue<bool>(
            restConfig::kMixManTargetEnergyEnabledKey, false));
}

TEST_F(RestLibraryFeatureTest, ResetIsStagedAndFailedUpdateRetainsDraft) {
    m_pFeature->m_mixManAppliedSettings.mixManPolicyOverrideEnabled = true;
    m_pFeature->m_mixManAppliedSettings.mixManPolicyPreset = QStringLiteral("explore");
    m_pFeature->m_mixManAppliedSettings.mixManRecommendationLens =
            QStringLiteral("semantic");
    m_pFeature->m_mixManAppliedSettings.mixManTargetEnergyEnabled = true;
    m_pFeature->m_mixManAppliedSettings.mixManTargetColorEnabled = true;
    m_pFeature->m_mixManAppliedSettings.mixManTargetBpmEnabled = true;
    m_pFeature->m_mixManDraftSettings = m_pFeature->m_mixManAppliedSettings;

    m_pFeature->slotResetSteeringRequested();

    EXPECT_FALSE(m_pFeature->m_mixManDraftSettings.mixManPolicyOverrideEnabled);
    EXPECT_EQ(m_pFeature->m_mixManDraftSettings.mixManRecommendationLens,
            QStringLiteral("auto"));
    EXPECT_FALSE(m_pFeature->m_mixManDraftSettings.mixManTargetEnergyEnabled);
    EXPECT_FALSE(m_pFeature->m_mixManDraftSettings.mixManTargetColorEnabled);
    EXPECT_FALSE(m_pFeature->m_mixManDraftSettings.mixManTargetBpmEnabled);
    EXPECT_FALSE(config()->exists(restConfig::kMixManPolicyOverrideEnabledKey));

    m_pFeature->m_mixManPendingAppliedSettings =
            m_pFeature->m_mixManDraftSettings;
    m_pFeature->m_policyRefreshPersistsDraft = true;
    m_pFeature->m_mutationSequencer.queue(
            mixxx::library::rest::RestLibraryMutationSequencer::Kind::PolicyRefresh);
    const auto dispatch = m_pFeature->m_mutationSequencer.takeNext({});
    ASSERT_TRUE(dispatch);
    m_pFeature->m_policyRefreshPersistSequence = dispatch->sequence;

    mixxx::library::rest::RestLibrarySessionWriteStatus failure;
    failure.operation = QStringLiteral("session_policy_refresh");
    failure.mutationSequence = dispatch->sequence;
    failure.statusCode = 422;
    failure.errorText = QStringLiteral("invalid steering");
    m_pFeature->slotMixManSessionWriteStatusUpdated(failure);

    EXPECT_FALSE(m_pFeature->m_policyRefreshPersistsDraft);
    EXPECT_FALSE(m_pFeature->m_mixManDraftSettings.mixManPolicyOverrideEnabled);
    EXPECT_TRUE(m_pFeature->m_mixManAppliedSettings.mixManPolicyOverrideEnabled);
    EXPECT_FALSE(config()->exists(restConfig::kMixManPolicyOverrideEnabledKey));
}

TEST_F(RestLibraryFeatureTest, FailedPolicyRefreshStartsClaimForQueuedPlayback) {
    using MutationKind =
            mixxx::library::rest::RestLibraryMutationSequencer::Kind;
    m_pFeature->m_mixManSession.id = QStringLiteral("session-1");
    m_pFeature->m_mixManRegistration.instance.instanceId = QStringLiteral("inst-1");

    const quint64 policySequence =
            m_pFeature->m_mutationSequencer.queue(MutationKind::PolicyRefresh);
    ASSERT_TRUE(m_pFeature->m_mutationSequencer.takeNext({}));
    m_pFeature->m_mutationSequencer.queue(MutationKind::Playback);
    m_pFeature->m_mutationSequencer.queue(MutationKind::Snapshot);

    m_network.ExpectPost(
            QStringLiteral("/api/v3/sessions/session-1/playback-control/claim"),
            {},
            {QStringLiteral("\"instance_id\":\"inst-1\"")},
            200,
            R"json({"session_id":"session-1","playback_controller":{"instance_id":"inst-1","lease_id":"lease-1","generation":1,"active":true}})json");

    mixxx::library::rest::RestLibrarySessionWriteStatus failure;
    failure.operation = QStringLiteral("session_policy_refresh");
    failure.mutationSequence = policySequence;
    failure.statusCode = 422;
    failure.errorText = QStringLiteral("invalid steering");
    m_pFeature->slotMixManSessionWriteStatusUpdated(failure);

    EXPECT_TRUE(m_pFeature->m_playbackControlClaimPending);
    EXPECT_TRUE(m_pFeature->m_mutationSequencer.hasInFlight());
    EXPECT_TRUE(m_pFeature->m_mutationSequencer.hasQueued(MutationKind::Playback));
    EXPECT_TRUE(m_pFeature->m_mutationSequencer.hasQueued(MutationKind::Snapshot));
}

TEST_F(RestLibraryFeatureTest, FailedPolicyRefreshDrainsQueuedPlaybackWithLease) {
    using MutationKind =
            mixxx::library::rest::RestLibraryMutationSequencer::Kind;
    m_pFeature->m_mixManSession.id = QStringLiteral("session-1");
    m_pFeature->m_mixManRegistration.instance.instanceId = QStringLiteral("inst-1");
    m_pFeature->m_playbackLease = {
            QStringLiteral("inst-1"), QStringLiteral("lease-1"), 3, true};
    m_pFeature->m_playbackLeaseOwned = true;
    m_pFeature->m_pendingPlayback.currentTrackId = QStringLiteral("8");
    m_pFeature->m_pendingPlayback.playbackState = QStringLiteral("playing");

    const quint64 policySequence =
            m_pFeature->m_mutationSequencer.queue(MutationKind::PolicyRefresh);
    ASSERT_TRUE(m_pFeature->m_mutationSequencer.takeNext({}));
    m_pFeature->m_mutationSequencer.queue(MutationKind::Playback);
    m_pFeature->m_mutationSequencer.queue(MutationKind::Snapshot);

    m_network.ExpectPost(
            QStringLiteral("/api/v3/sessions/session-1/playback"),
            {},
            {QStringLiteral("\"lease_id\":\"lease-1\""),
                    QStringLiteral("\"lease_generation\":3"),
                    QStringLiteral("\"current_track_id\":8")},
            200,
            R"json({"session":{"id":"session-1"},"authoritative":{"session_id":"session-1"}})json");

    mixxx::library::rest::RestLibrarySessionWriteStatus failure;
    failure.operation = QStringLiteral("session_policy_refresh");
    failure.mutationSequence = policySequence;
    failure.statusCode = 503;
    failure.errorText = QStringLiteral("temporarily unavailable");
    m_pFeature->slotMixManSessionWriteStatusUpdated(failure);

    EXPECT_TRUE(m_pFeature->m_mutationSequencer.hasInFlight());
    EXPECT_TRUE(m_pFeature->m_mutationSequencer.hasQueued(MutationKind::Snapshot));
}

TEST_F(RestLibraryFeatureTest, FailedAutomaticPolicyRefreshDispatchesQueuedUserUpdate) {
    using MutationKind =
            mixxx::library::rest::RestLibraryMutationSequencer::Kind;
    m_pFeature->m_mixManSession.id = QStringLiteral("session-1");
    m_pFeature->m_mixManRegistration.instance.instanceId = QStringLiteral("inst-1");
    RestLibrarySettings automaticSettings = RestLibrarySettings::fromConfig(config());
    automaticSettings.mixManRecommendationLens = QStringLiteral("auto");
    m_pFeature->m_mixManAppliedSettings = automaticSettings;
    m_pFeature->m_mixManDraftSettings = automaticSettings;

    MockNetworkReply* pAutomaticReply = m_network.ExpectPost(
            QStringLiteral("/api/v3/sessions/session-1/actions"),
            {},
            {QStringLiteral("\"action_type\":\"policy_refresh\""),
                    QStringLiteral("\"recommendation_lens\":\"auto\"")},
            500,
            R"json({"detail":"temporary failure"})json");
    m_pFeature->requestMixManPolicyRefresh(automaticSettings);

    MockNetworkReply* pUserReply = m_network.ExpectPost(
            QStringLiteral("/api/v3/sessions/session-1/actions"),
            {},
            {QStringLiteral("\"action_type\":\"policy_refresh\""),
                    QStringLiteral("\"recommendation_lens\":\"semantic\"")},
            422,
            R"json({"detail":"invalid steering"})json");
    m_pFeature->slotRecommendationLensChanged(QStringLiteral("semantic"));
    m_pFeature->slotUpdateSuggestionsRequested();

    EXPECT_TRUE(m_pFeature->m_policyRefreshPersistsDraft);
    EXPECT_EQ(m_pFeature->m_policyRefreshPersistSequence, 0u);
    EXPECT_TRUE(m_pFeature->m_mutationSequencer.hasQueued(MutationKind::PolicyRefresh));

    pAutomaticReply->Done();

    EXPECT_TRUE(m_pFeature->m_policyRefreshPersistsDraft);
    EXPECT_GT(m_pFeature->m_policyRefreshPersistSequence, 0u);
    EXPECT_TRUE(m_pFeature->m_mutationSequencer.hasInFlight());
    EXPECT_FALSE(m_pFeature->m_mutationSequencer.hasQueued(MutationKind::PolicyRefresh));

    pUserReply->Done();

    EXPECT_FALSE(m_pFeature->m_policyRefreshPersistsDraft);
    EXPECT_EQ(m_pFeature->m_policyRefreshPersistSequence, 0u);
    EXPECT_FALSE(m_pFeature->m_mutationSequencer.hasInFlight());
    EXPECT_EQ(m_pFeature->m_mixManDraftSettings.mixManRecommendationLens,
            QStringLiteral("semantic"));
    EXPECT_EQ(m_pFeature->m_mixManAppliedSettings.mixManRecommendationLens,
            QStringLiteral("auto"));
    EXPECT_FALSE(config()->exists(restConfig::kMixManRecommendationLensKey));
}

TEST_F(RestLibraryFeatureTest, QueuedUserUpdatePersistsAfterAutomaticPolicyFailure) {
    m_pFeature->m_mixManSession.id = QStringLiteral("session-1");
    m_pFeature->m_mixManRegistration.instance.instanceId = QStringLiteral("inst-1");
    RestLibrarySettings automaticSettings = RestLibrarySettings::fromConfig(config());
    automaticSettings.mixManRecommendationLens = QStringLiteral("auto");
    m_pFeature->m_mixManAppliedSettings = automaticSettings;
    m_pFeature->m_mixManDraftSettings = automaticSettings;

    MockNetworkReply* pAutomaticReply = m_network.ExpectPost(
            QStringLiteral("/api/v3/sessions/session-1/actions"),
            {},
            {QStringLiteral("\"action_type\":\"policy_refresh\""),
                    QStringLiteral("\"recommendation_lens\":\"auto\"")},
            503,
            R"json({"detail":"temporary failure"})json");
    m_pFeature->requestMixManPolicyRefresh(automaticSettings);

    MockNetworkReply* pUserReply = m_network.ExpectPost(
            QStringLiteral("/api/v3/sessions/session-1/actions"),
            {},
            {QStringLiteral("\"action_type\":\"policy_refresh\""),
                    QStringLiteral("\"recommendation_lens\":\"semantic\"")},
            200,
            R"json({"session":{"id":"session-1"},"authoritative":{"session_id":"session-1"}})json");
    m_pFeature->slotRecommendationLensChanged(QStringLiteral("semantic"));
    m_pFeature->slotUpdateSuggestionsRequested();

    pAutomaticReply->Done();
    ASSERT_TRUE(m_pFeature->m_policyRefreshPersistsDraft);
    ASSERT_GT(m_pFeature->m_policyRefreshPersistSequence, 0u);

    pUserReply->Done();

    EXPECT_FALSE(m_pFeature->m_policyRefreshPersistsDraft);
    EXPECT_EQ(m_pFeature->m_policyRefreshPersistSequence, 0u);
    EXPECT_FALSE(m_pFeature->m_mutationSequencer.hasInFlight());
    EXPECT_EQ(m_pFeature->m_mixManDraftSettings.mixManRecommendationLens,
            QStringLiteral("semantic"));
    EXPECT_EQ(m_pFeature->m_mixManAppliedSettings.mixManRecommendationLens,
            QStringLiteral("semantic"));
    EXPECT_EQ(config()->getValueString(restConfig::kMixManRecommendationLensKey),
            QStringLiteral("semantic"));
}

TEST_F(RestLibraryFeatureTest, TrackChangeDiscardsUnappliedSteeringDraft) {
    config()->setValue(restConfig::kUseMixManDefaultsKey, false);
    m_pFeature->slotRecommendationLensChanged(QStringLiteral("semantic"));
    ASSERT_EQ(m_pFeature->m_mixManDraftSettings.mixManRecommendationLens,
            QStringLiteral("semantic"));

    m_pFeature->slotCurrentPlayingTrackChanged({});

    EXPECT_EQ(m_pFeature->m_mixManDraftSettings.mixManRecommendationLens,
            QStringLiteral("auto"));
    EXPECT_EQ(m_pFeature->m_mixManAppliedSettings.mixManRecommendationLens,
            QStringLiteral("auto"));
}

TEST_F(RestLibraryFeatureTest, AutoDJQueuesAuthoritativePathNotCandidateSiblings) {
    MockNetworkReply* pB = m_network.ExpectGet(
            QStringLiteral("/download"),
            {{QStringLiteral("track_id"), QStringLiteral("B")}},
            200,
            QByteArrayLiteral("audio B"));
    MockNetworkReply* pE = m_network.ExpectGet(
            QStringLiteral("/download"),
            {{QStringLiteral("track_id"), QStringLiteral("E")}},
            200,
            QByteArrayLiteral("audio E"));
    MockNetworkReply* pF = m_network.ExpectGet(
            QStringLiteral("/download"),
            {{QStringLiteral("track_id"), QStringLiteral("F")}},
            200,
            QByteArrayLiteral("audio F"));
    const QList<RestLibraryTrack> candidates{
            recommendation(QStringLiteral("B"), QStringLiteral("Candidate B")),
            recommendation(QStringLiteral("C"), QStringLiteral("Candidate C")),
            recommendation(QStringLiteral("D"), QStringLiteral("Candidate D"))};
    const QList<RestLibraryTrack> path{
            recommendation(QStringLiteral("B"), QStringLiteral("Path B")),
            recommendation(QStringLiteral("E"), QStringLiteral("Path E")),
            recommendation(QStringLiteral("E"), QStringLiteral("Path E duplicate")),
            recommendation(QStringLiteral("F"), QStringLiteral("Path F"))};
    setPolicyPath(candidates, path);

    ASSERT_EQ(tableModel()->rowCount(), 3);
    EXPECT_EQ(tableModel()->remoteIdForIndex(tableModel()->index(0, 0)),
            QStringLiteral("B"));
    EXPECT_EQ(tableModel()->remoteIdForIndex(tableModel()->index(1, 0)),
            QStringLiteral("C"));
    EXPECT_EQ(tableModel()->remoteIdForIndex(tableModel()->index(2, 0)),
            QStringLiteral("D"));
    queueRecommendations();
    pF->Done(true);
    pB->Done(true);
    pE->Done(true);

    QList<TrackId> expectedTrackIds;
    for (const RestLibraryTrack& track : std::as_const(m_pFeature->m_autoDJPathTracks)) {
        const TrackPointer pTrack = tableModel()->materializeTrack(track);
        ASSERT_TRUE(pTrack);
        expectedTrackIds.append(pTrack->getId());
    }
    EXPECT_EQ(autoDJTrackIds(), expectedTrackIds);
}

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

    setAutoDJTracks({recommendation(QStringLiteral("12"), QStringLiteral("First")),
            recommendation(QStringLiteral("11"), QStringLiteral("Second"))});
    queueRecommendations();

    pSecondAudio->Done(true);
    EXPECT_TRUE(autoDJTrackIds().isEmpty());
    pFirstAudio->Done(true);

    const TrackPointer pFirst =
            tableModel()->materializeTrack(QStringLiteral("12"));
    const TrackPointer pSecond =
            tableModel()->materializeTrack(QStringLiteral("11"));
    ASSERT_TRUE(pFirst);
    ASSERT_TRUE(pSecond);
    EXPECT_EQ(autoDJTrackIds(),
            (QList<TrackId>{pFirst->getId(), pSecond->getId()}));
    EXPECT_TRUE(autoDJRemoteIds().isEmpty());
}

TEST_F(RestLibraryFeatureTest, AutoDJUsesAuthoritativePathInsteadOfDisplayOrder) {
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
    setAutoDJTracks({primary, alternate});

    const int artistColumn =
            tableModel()->fieldIndex(QStringLiteral("artist"));
    tableModel()->sort(artistColumn, Qt::AscendingOrder);
    ASSERT_EQ(tableModel()->remoteIdForIndex(tableModel()->index(0, 0)),
            QStringLiteral("81"));
    queueRecommendations();

    pPrimaryAudio->Done(true);
    pAlternateAudio->Done(true);

    const TrackPointer pAlternate =
            tableModel()->materializeTrack(QStringLiteral("81"));
    const TrackPointer pPrimary =
            tableModel()->materializeTrack(QStringLiteral("82"));
    ASSERT_TRUE(pAlternate);
    ASSERT_TRUE(pPrimary);
    EXPECT_EQ(autoDJTrackIds(),
            (QList<TrackId>{pPrimary->getId(), pAlternate->getId()}));
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

    setAutoDJTracks({recommendation(QStringLiteral("52"), QStringLiteral("First")),
            recommendation(QStringLiteral("51"), QStringLiteral("Second"))});
    queueRecommendations();
    pFirstAudio->Done(true);
    pSecondAudio->Done(true);

    EXPECT_TRUE(autoDJTrackIds().isEmpty());
    const TrackPointer pFirst =
            tableModel()->materializeTrack(QStringLiteral("52"));
    const TrackPointer pSecond =
            tableModel()->materializeTrack(QStringLiteral("51"));
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
            tableModel()->materializeTrack(QStringLiteral("61"));
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

    setAutoDJTracks({recommendation(QStringLiteral("21"), QStringLiteral("Good")),
            recommendation(QStringLiteral("22"), QStringLiteral("Unavailable"))});
    queueRecommendations();
    pFirstAudio->Done(true);
    EXPECT_TRUE(autoDJTrackIds().isEmpty());
    m_pFeature->m_autoDJPathIntent = false;
    pSecondAudio->Done(true);

    const TrackPointer pGood =
            tableModel()->materializeTrack(QStringLiteral("21"));
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

    mapPendingTrack(pLocalTrack, QStringLiteral("77"));
    setAutoDJTracks({recommendation(QStringLiteral("77"), QStringLiteral("Mapped"))});
    queueRecommendations();

    EXPECT_EQ(autoDJTrackIds(), (QList<TrackId>{pLocalTrack->getId()}));
    EXPECT_FALSE(tableModel()->isCacheArtifact(pLocalTrack->getId()));
    EXPECT_EQ(tableModel()->remoteIdForTrack(pLocalTrack),
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

    setAutoDJTracks(tracks);
    queueRecommendations();
    ASSERT_EQ(autoDJRemoteIds(),
            (QStringList{QStringLiteral("31"), QStringLiteral("32")}));

    QList<RestLibraryTrack> updatedTracks = tracks;
    updatedTracks[0].title = QStringLiteral("Updated metadata");
    updatedTracks[0].recommendationPosition = 2;
    updatedTracks[0].color = QStringLiteral("#abcdef");
    updatedTracks[1].recommendationPosition = 1;
    updatedTracks[1].color = QStringLiteral("#fedcba");
    setRecommendations(updatedTracks);

    EXPECT_EQ(autoDJRemoteIds(),
            (QStringList{QStringLiteral("31"), QStringLiteral("32")}));
    EXPECT_EQ(autoDJPendingCount(), 2);
    const auto* pModel = tableModel();
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

TEST_F(RestLibraryFeatureTest, ChangedPathCancelsAndRestartsActiveAutoDJBatch) {
    MockNetworkReply* pOldAudio = m_network.ExpectGet(
            QStringLiteral("/download"),
            {{QStringLiteral("track_id"), QStringLiteral("41")}},
            200,
            QByteArrayLiteral("old audio"));
    setAutoDJTracks(
            {recommendation(QStringLiteral("41"), QStringLiteral("Old"))});
    queueRecommendations();
    ASSERT_FALSE(autoDJRemoteIds().isEmpty());

    MockNetworkReply* pNewAudio = m_network.ExpectGet(
            QStringLiteral("/download"),
            {{QStringLiteral("track_id"), QStringLiteral("42")}},
            200,
            QByteArrayLiteral("new audio"));
    setAutoDJTracks(
            {recommendation(QStringLiteral("42"), QStringLiteral("New"))});

    EXPECT_EQ(autoDJRemoteIds(), (QStringList{QStringLiteral("42")}));
    EXPECT_EQ(autoDJPendingCount(), 1);
    EXPECT_TRUE(pOldAudio->WasAborted());
    pNewAudio->Done(true);
}

TEST_F(RestLibraryFeatureTest, FailedRefreshRetainsLastValidRecommendations) {
    config()->setValue(restConfig::kCacheEnabledKey, false);
    const QList<RestLibraryTrack> tracks{
            recommendation(QStringLiteral("71"), QStringLiteral("Keep Me"))};
    setRecommendations(tracks);
    ASSERT_EQ(tableModel()->trackCount(), 1);

    failFetch(QStringLiteral("MixMan policy path response was not valid JSON."));

    ASSERT_EQ(tableModel()->trackCount(), 1);
    EXPECT_EQ(tableModel()->trackForRemoteId(QStringLiteral("71")).title,
            QStringLiteral("Keep Me"));
}
