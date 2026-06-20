#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStringList>

#include "library/rest/restlibraryclient.h"
#include "test/mock_networkaccessmanager.h"
#include "track/track.h"

namespace {

using mixxx::library::rest::RestLibraryClient;
using mixxx::library::rest::RestLibrarySettings;

RestLibrarySettings newSettings() {
    RestLibrarySettings settings;
    settings.enabled = true;
    settings.baseUrl = QUrl(QStringLiteral("http://example.invalid"));
    settings.trackListPath = QStringLiteral("/configured-list");
    settings.pageSize = 5;
    return settings;
}

RestLibrarySettings newRecommendationSettings() {
    RestLibrarySettings settings = newSettings();
    settings.trackDetailPathTemplate = QStringLiteral("/configured-detail/%1");
    settings.recommendationPathTemplate = QStringLiteral("/configured-related/%1");
    settings.recommendationLimit = 5;
    return settings;
}

RestLibrarySettings newMixManSettings() {
    RestLibrarySettings settings = newSettings();
    settings.useMixManDefaults = true;
    settings.trackLookupPathTemplate = QStringLiteral("/tracks?artists=%artist&titles=%title");
    settings.recommendationPathTemplate =
            QStringLiteral("/recommendations/policy-console/path/%1");
    settings.recommendationLimit = 10;
    settings.mixManPathDepth = 5;
    settings.mixManPolicyPreset = QStringLiteral("build_energy");
    settings.mixManTargetEnergyEnabled = true;
    settings.mixManTargetEnergy = 4;
    settings.mixManTargetColorEnabled = true;
    settings.mixManTargetColor = QStringLiteral("#ff6600");
    settings.mixManTargetBpmEnabled = true;
    settings.mixManTargetBpm = 132;
    settings.mixManAdminApprovedOnly = true;
    return settings;
}

} // namespace

TEST(RestLibraryClientTest, ParsesObjectWrappedTrackList) {
    const QJsonDocument document = QJsonDocument::fromJson(R"json(
        {
          "results": [
            {
              "id": 42,
              "title": "Night Train",
              "artist": "Ada",
              "album": "Signals",
              "genre": "House",
              "bpm": 124.5,
              "key": "8A",
              "duration": 366.0,
              "rating": 4
            }
          ]
        }
    )json");

    const auto tracks = RestLibraryClient::parseTrackListDocumentForTesting(document);

    ASSERT_EQ(tracks.size(), 1);
    EXPECT_EQ(tracks.at(0).remoteId, QStringLiteral("42"));
    EXPECT_EQ(tracks.at(0).title, QStringLiteral("Night Train"));
    EXPECT_EQ(tracks.at(0).artist, QStringLiteral("Ada"));
    EXPECT_EQ(tracks.at(0).genre, QStringLiteral("House"));
    EXPECT_DOUBLE_EQ(tracks.at(0).bpm, 124.5);
    EXPECT_EQ(tracks.at(0).rating, 4);
}

TEST(RestLibraryClientTest, ParsesTrackObjectFallbackFields) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), QStringLiteral("abc"));
    object.insert(QStringLiteral("name"), QStringLiteral("Fallback Title"));
    object.insert(QStringLiteral("duration_seconds"), QStringLiteral("120"));
    object.insert(QStringLiteral("rating"), 5);

    const auto track = RestLibraryClient::parseTrackObjectForTesting(object);

    EXPECT_EQ(track.remoteId, QStringLiteral("abc"));
    EXPECT_EQ(track.title, QStringLiteral("Fallback Title"));
    EXPECT_DOUBLE_EQ(track.durationSeconds, 120.0);
    EXPECT_EQ(track.rating, 5);
}

TEST(RestLibraryClientTest, ParsesMixManIndexStatus) {
    const QJsonDocument document = QJsonDocument::fromJson(R"json(
        {"ready": true, "count": 123, "dim": 512}
    )json");

    const auto diagnostics = RestLibraryClient::parseIndexStatusDocumentForTesting(document);

    EXPECT_TRUE(diagnostics.indexKnown);
    EXPECT_TRUE(diagnostics.indexReady);
    EXPECT_EQ(diagnostics.indexCount, 123);
    EXPECT_EQ(diagnostics.indexDimension, 512);
}

TEST(RestLibraryClientTest, ParsesMixManPolicyPresets) {
    const QJsonDocument document = QJsonDocument::fromJson(R"json(
        [
          {"key": "party_safe", "label": "Party Safe", "description": "Safer choices"},
          {"key": "explore", "label": "Explore", "description": "Broader choices"}
        ]
    )json");

    const auto presets = RestLibraryClient::parsePolicyPresetsDocumentForTesting(document);

    ASSERT_EQ(presets.size(), 2);
    EXPECT_EQ(presets.at(0).key, QStringLiteral("party_safe"));
    EXPECT_EQ(presets.at(0).label, QStringLiteral("Party Safe"));
    EXPECT_EQ(presets.at(1).key, QStringLiteral("explore"));
}

TEST(RestLibraryClientTest, ParsesMixManPolicyPath) {
    const QJsonDocument document = QJsonDocument::fromJson(R"json(
        {
          "policy_preset": "build_energy",
          "resolved_move_type": "bridge",
          "recommendation_event_id": 77,
          "tracks_by_id": {
            "8": {
              "track_id": 8,
              "title": "First",
              "artist": "Ada",
              "metadata": {"bpm": 124, "quality_score": 0.91}
            },
            "9": {
              "track_id": 9,
              "title": "Second",
              "artist": "Ben",
              "metadata": {"bpm": 126}
            }
          },
          "plan": {
            "alternatives": [
              {
                "track_id": 8,
                "score": 0.88,
                "position": 1,
                "recommendation_event_id": 77,
                "recommendation_item_id": 701,
                "reason_codes": ["energy_match"],
                "candidate_features": {
                  "transition_fit": 0.72,
                  "target_distance": 0.15
                }
              }
            ]
          },
          "path": {
            "selected_branch_score": 0.77,
            "steps": [
              {"track_id": 8, "score": 0.88, "position": 1},
              {"track_id": 9, "score": 0.80, "position": 2}
            ]
          }
        }
    )json");

    const auto path = RestLibraryClient::parsePolicyPathDocumentForTesting(document);

    EXPECT_EQ(path.policyPreset, QStringLiteral("build_energy"));
    EXPECT_EQ(path.resolvedMoveType, QStringLiteral("bridge"));
    EXPECT_EQ(path.recommendationEventId, 77);
    EXPECT_DOUBLE_EQ(path.selectedBranchScore, 0.77);
    ASSERT_EQ(path.candidates.size(), 1);
    EXPECT_EQ(path.candidates.at(0).remoteId, QStringLiteral("8"));
    EXPECT_EQ(path.candidates.at(0).title, QStringLiteral("First"));
    EXPECT_EQ(path.candidates.at(0).quality, 0.88);
    EXPECT_EQ(path.candidates.at(0).reasonCodes, QStringList({QStringLiteral("energy_match")}));
    EXPECT_DOUBLE_EQ(path.candidates.at(0).transitionFit, 0.72);
    ASSERT_EQ(path.path.size(), 2);
    EXPECT_EQ(path.path.at(1).remoteId, QStringLiteral("9"));
    EXPECT_EQ(path.path.at(1).position, 2);
}

TEST(RestLibraryClientTest, ParsesMixManSessionDetail) {
    const QJsonDocument document = QJsonDocument::fromJson(R"json(
        {
          "session": {
            "id": "session-1",
            "display_name": "Kitchen Party",
            "status": "active"
          },
          "snapshot": {
            "current_remote_id": "8",
            "playback": {"state": "playing"}
          },
          "intent": {
            "status": "active",
            "policy_preset": "build_energy"
          },
          "policy_event": {
            "id": 77
          },
          "clients": [{"client_id": "client-1"}],
          "recent_events": [{"event_type": "snapshot"}]
        }
    )json");

    const auto session = RestLibraryClient::parseSessionDocumentForTesting(document);

    EXPECT_EQ(session.id, QStringLiteral("session-1"));
    EXPECT_EQ(session.displayName, QStringLiteral("Kitchen Party"));
    EXPECT_EQ(session.status, QStringLiteral("active"));
    EXPECT_EQ(session.snapshot.value(QStringLiteral("current_remote_id")).toString(),
            QStringLiteral("8"));
    EXPECT_EQ(session.snapshot.value(QStringLiteral("playback"))
                    .toObject()
                    .value(QStringLiteral("state"))
                    .toString(),
            QStringLiteral("playing"));
    EXPECT_EQ(session.intent.value(QStringLiteral("policy_preset")).toString(),
            QStringLiteral("build_energy"));
    EXPECT_EQ(session.policyEvent.value(QStringLiteral("id")).toInt(), 77);
    ASSERT_EQ(session.clients.size(), 1);
    EXPECT_EQ(session.clients.at(0).toObject().value(QStringLiteral("client_id")).toString(),
            QStringLiteral("client-1"));
    ASSERT_EQ(session.recentEvents.size(), 1);
    EXPECT_EQ(session.recentEvents.at(0).toObject().value(QStringLiteral("event_type")).toString(),
            QStringLiteral("snapshot"));
}

TEST(RestLibraryClientTest, ParsesMixManAuthoritativeSessionState) {
    const QJsonDocument document = QJsonDocument::fromJson(R"json(
        {
          "session": {"id": "session-1", "status": "active"},
          "authoritative": {
            "session_id": "session-1",
            "revision": 42,
            "playback": {"revision": 7, "current_track_id": 8},
            "pressure_revision": 3,
            "pressure_state": {"explore": 0.25},
            "controller": {"client_id": "mixxx-1", "role": "dj"},
            "blocked": {"sector_change": {"reason": "beacon"}},
            "intents": [{"intent_type": "beacon"}],
            "queue": [{"track_id": 10}],
            "candidates": [
              {
                "track_id": 9,
                "title": "Next Track",
                "artist": "Ada",
                "score": 0.91,
                "reason_codes": ["energy_match"]
              }
            ],
            "path": {
              "steps": [
                {"track_id": 9, "title": "Next Track", "position": 1}
              ]
            }
          }
        }
    )json");

    const auto session = RestLibraryClient::parseSessionDocumentForTesting(document);

    EXPECT_EQ(session.authoritative.sessionId, QStringLiteral("session-1"));
    EXPECT_EQ(session.authoritative.revision, 42);
    EXPECT_EQ(session.authoritative.playbackRevision, 7);
    EXPECT_EQ(session.authoritative.pressureRevision, 3);
    EXPECT_EQ(session.authoritative.intents.size(), 1);
    EXPECT_EQ(session.authoritative.queue.size(), 1);
    EXPECT_FALSE(session.authoritative.blocked.isEmpty());
    ASSERT_EQ(session.authoritative.policyPath.candidates.size(), 1);
    EXPECT_EQ(session.authoritative.policyPath.candidates.at(0).remoteId, QStringLiteral("9"));
    EXPECT_EQ(session.authoritative.policyPath.candidates.at(0).sourceLabel,
            QStringLiteral("MixMan Authoritative"));
    EXPECT_EQ(session.authoritative.policyPath.candidates.at(0).reasonCodes,
            QStringList({QStringLiteral("energy_match")}));
    ASSERT_EQ(session.authoritative.policyPath.path.size(), 1);
    EXPECT_EQ(session.authoritative.policyPath.path.at(0).remoteId, QStringLiteral("9"));
}

TEST(RestLibraryClientTest, FetchesTrackListWithMockNetworkAccessManager) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy fetchedSpy(&client, &RestLibraryClient::tracksFetched);
    QSignalSpy failedSpy(&client, &RestLibraryClient::fetchFailed);
    MockNetworkReply* pReply = network.ExpectGet(
            QStringLiteral("/configured-list"),
            {{"limit", "5"}},
            200,
            R"json([{"id": 7, "title": "Mock Track"}])json");

    client.fetchTracks(newSettings());
    pReply->Done();

    ASSERT_EQ(fetchedSpy.count(), 1);
    EXPECT_EQ(failedSpy.count(), 0);
    const auto tracks = qvariant_cast<QList<mixxx::library::rest::RestLibraryTrack>>(
            fetchedSpy.takeFirst().at(0));
    ASSERT_EQ(tracks.size(), 1);
    EXPECT_EQ(tracks.at(0).remoteId, QStringLiteral("7"));
}

TEST(RestLibraryClientTest, IgnoresStaleTrackListReplyAfterNewerRequest) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy fetchedSpy(&client, &RestLibraryClient::tracksFetched);
    QSignalSpy failedSpy(&client, &RestLibraryClient::fetchFailed);
    RestLibrarySettings oldSettings = newSettings();
    oldSettings.trackListPath = QStringLiteral("/configured-list-old");
    RestLibrarySettings currentSettings = newSettings();
    currentSettings.trackListPath = QStringLiteral("/configured-list-current");
    MockNetworkReply* pOldReply = network.ExpectGet(
            QStringLiteral("/configured-list-old"),
            {{"limit", "5"}},
            200,
            R"json([{"id": 1, "title": "Old Track"}])json");
    MockNetworkReply* pCurrentReply = network.ExpectGet(
            QStringLiteral("/configured-list-current"),
            {{"limit", "5"}},
            200,
            R"json([{"id": 2, "title": "Current Track"}])json");

    client.fetchTracks(oldSettings);
    client.fetchTracks(currentSettings);
    pCurrentReply->Done();
    pOldReply->Done();

    ASSERT_EQ(fetchedSpy.count(), 1);
    EXPECT_EQ(failedSpy.count(), 0);
    const auto tracks = qvariant_cast<QList<mixxx::library::rest::RestLibraryTrack>>(
            fetchedSpy.takeFirst().at(0));
    ASSERT_EQ(tracks.size(), 1);
    EXPECT_EQ(tracks.at(0).remoteId, QStringLiteral("2"));
}

TEST(RestLibraryClientTest, ReportsAuthOrValidationFailure) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy fetchedSpy(&client, &RestLibraryClient::tracksFetched);
    QSignalSpy failedSpy(&client, &RestLibraryClient::fetchFailed);
    MockNetworkReply* pReply = network.ExpectGet(
            QStringLiteral("/configured-list"),
            {{"limit", "5"}},
            401,
            R"json({"detail":"failed"})json");

    client.fetchTracks(newSettings());
    pReply->Done();

    EXPECT_EQ(fetchedSpy.count(), 0);
    EXPECT_EQ(failedSpy.count(), 1);
}

TEST(RestLibraryClientTest, ReportsMalformedOrEmptyPayload) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy fetchedSpy(&client, &RestLibraryClient::tracksFetched);
    QSignalSpy failedSpy(&client, &RestLibraryClient::fetchFailed);
    MockNetworkReply* pReply = network.ExpectGet(
            QStringLiteral("/configured-list"),
            {{"limit", "5"}},
            200,
            QByteArrayLiteral("{"));

    client.fetchTracks(newSettings());
    pReply->Done();

    EXPECT_EQ(fetchedSpy.count(), 0);
    EXPECT_EQ(failedSpy.count(), 1);
}

TEST(RestLibraryClientTest, LooksUpCurrentTrackWithConfiguredTemplate) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy lookupSpy(&client, &RestLibraryClient::trackLookupSucceeded);
    QSignalSpy missedSpy(&client, &RestLibraryClient::trackLookupMissed);
    RestLibrarySettings settings = newSettings();
    settings.trackLookupPathTemplate =
            QStringLiteral("/configured-lookup?artist=%artist&title=%title&duration=%duration");

    TrackPointer pTrack = Track::newTemporary();
    pTrack->setArtist(QStringLiteral("Ada"));
    pTrack->setTitle(QStringLiteral("Night Train"));
    pTrack->setDuration(366.2);

    MockNetworkReply* pReply = network.ExpectGet(
            QStringLiteral("/configured-lookup"),
            {{"artist", "Ada"}, {"title", "Night Train"}, {"duration", "366"}, {"limit", "1"}},
            200,
            R"json([{"id": "remote-7"}])json");

    client.lookupTrack(settings, pTrack);
    pReply->Done();

    ASSERT_EQ(lookupSpy.count(), 1);
    EXPECT_EQ(missedSpy.count(), 0);
    EXPECT_EQ(lookupSpy.takeFirst().at(0).toString(), QStringLiteral("remote-7"));
}

TEST(RestLibraryClientTest, FetchesRecommendationsWithConfiguredTemplate) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy fetchedSpy(&client, &RestLibraryClient::recommendationsFetched);
    QSignalSpy failedSpy(&client, &RestLibraryClient::fetchFailed);
    MockNetworkReply* pListReply = network.ExpectGet(
            QStringLiteral("/configured-related/source-1"),
            {{"limit", "5"}},
            200,
            R"json(["remote-8", "remote-9"])json");
    MockNetworkReply* pDetailReply1 = network.ExpectGet(
            QStringLiteral("/configured-detail/remote-8"),
            {},
            200,
            R"json({"id":"remote-8","title":"First Related"})json");
    MockNetworkReply* pDetailReply2 = network.ExpectGet(
            QStringLiteral("/configured-detail/remote-9"),
            {},
            200,
            R"json({"id":"remote-9","title":"Second Related"})json");

    client.fetchRecommendations(newRecommendationSettings(), QStringLiteral("source-1"));
    pListReply->Done();
    pDetailReply1->Done();
    pDetailReply2->Done();

    ASSERT_EQ(fetchedSpy.count(), 1);
    EXPECT_EQ(failedSpy.count(), 0);
    const auto tracks = qvariant_cast<QList<mixxx::library::rest::RestLibraryTrack>>(
            fetchedSpy.takeFirst().at(0));
    ASSERT_EQ(tracks.size(), 2);
    EXPECT_EQ(tracks.at(0).remoteId, QStringLiteral("remote-8"));
    EXPECT_EQ(tracks.at(1).remoteId, QStringLiteral("remote-9"));
}

TEST(RestLibraryClientTest, FetchesMixManPolicyPathWithConfiguredTargets) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy fetchedSpy(&client, &RestLibraryClient::mixManPolicyPathFetched);
    QSignalSpy failedSpy(&client, &RestLibraryClient::fetchFailed);
    MockNetworkReply* pReply = network.ExpectGet(
            QStringLiteral("/recommendations/policy-console/path/1"),
            {{"candidate_limit", "10"},
                    {"planning_depth", "5"},
                    {"admin_approved_only", "true"},
                    {"policy_preset", "build_energy"},
                    {"target_energy", "0.80"},
                    {"target_color", "#ff6600"},
                    {"target_bpm", "132"},
                    {"session_id", "session-1"},
                    {"previous_track_id", "7"},
                    {"recent_track_ids", "7,6"}},
            200,
            R"json({
              "tracks_by_id": {"8": {"track_id": 8, "title": "Policy Track"}},
              "plan": {"alternatives": [{"track_id": 8, "score": 0.9}]},
              "path": {"steps": [{"track_id": 8, "score": 0.9, "position": 1}]}
            })json");

    client.fetchMixManPolicyPath(
            newMixManSettings(),
            QStringLiteral("1"),
            QStringLiteral("session-1"),
            QStringLiteral("7"),
            {QStringLiteral("7"), QStringLiteral("6")});
    pReply->Done();

    ASSERT_EQ(fetchedSpy.count(), 1);
    EXPECT_EQ(failedSpy.count(), 0);
    const auto path = qvariant_cast<mixxx::library::rest::RestLibraryPolicyPath>(
            fetchedSpy.takeFirst().at(0));
    ASSERT_EQ(path.candidates.size(), 1);
    EXPECT_EQ(path.candidates.at(0).remoteId, QStringLiteral("8"));
}

TEST(RestLibraryClientTest, RejectsInvalidMixManPolicyPathCurrentTrackId) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy fetchedSpy(&client, &RestLibraryClient::mixManPolicyPathFetched);
    QSignalSpy failedSpy(&client, &RestLibraryClient::fetchFailed);

    client.fetchMixManPolicyPath(newMixManSettings(), QStringLiteral("source-1"));

    EXPECT_EQ(fetchedSpy.count(), 0);
    ASSERT_EQ(failedSpy.count(), 1);
    EXPECT_FALSE(failedSpy.takeFirst().at(0).toString().isEmpty());
}

TEST(RestLibraryClientTest, OmitsInvalidMixManPolicyPathHistoryIds) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy fetchedSpy(&client, &RestLibraryClient::mixManPolicyPathFetched);
    MockNetworkReply* pReply = network.ExpectGet(
            QStringLiteral("/recommendations/policy-console/path/1"),
            {{"candidate_limit", "10"},
                    {"previous_track_id", "7"},
                    {"recent_track_ids", "7,6"}},
            200,
            R"json({
              "tracks_by_id": {"8": {"track_id": 8, "title": "Policy Track"}},
              "plan": {"alternatives": [{"track_id": 8, "score": 0.9}]}
            })json");

    client.fetchMixManPolicyPath(
            newMixManSettings(),
            QStringLiteral("1"),
            {},
            QStringLiteral("7"),
            {QStringLiteral("bad"), QStringLiteral("7"), QStringLiteral("6"), QStringLiteral("7")});
    pReply->Done();

    ASSERT_EQ(fetchedSpy.count(), 1);
}

TEST(RestLibraryClientTest, IgnoresStaleMixManPolicyPathReplyAfterNewerRequest) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy fetchedSpy(&client, &RestLibraryClient::mixManPolicyPathFetched);
    QSignalSpy failedSpy(&client, &RestLibraryClient::fetchFailed);
    MockNetworkReply* pOldReply = network.ExpectGet(
            QStringLiteral("/recommendations/policy-console/path/1"),
            {{"candidate_limit", "10"}},
            200,
            R"json({
              "tracks_by_id": {"1": {"track_id": 1, "title": "Old Policy Track"}},
              "plan": {"alternatives": [{"track_id": 1, "score": 0.1}]}
            })json");
    MockNetworkReply* pCurrentReply = network.ExpectGet(
            QStringLiteral("/recommendations/policy-console/path/2"),
            {{"candidate_limit", "10"}},
            200,
            R"json({
              "tracks_by_id": {"2": {"track_id": 2, "title": "Current Policy Track"}},
              "plan": {"alternatives": [{"track_id": 2, "score": 0.9}]}
            })json");

    client.fetchMixManPolicyPath(newMixManSettings(), QStringLiteral("1"));
    client.fetchMixManPolicyPath(newMixManSettings(), QStringLiteral("2"));
    pCurrentReply->Done();
    pOldReply->Done();

    ASSERT_EQ(fetchedSpy.count(), 1);
    EXPECT_EQ(failedSpy.count(), 0);
    const auto path = qvariant_cast<mixxx::library::rest::RestLibraryPolicyPath>(
            fetchedSpy.takeFirst().at(0));
    ASSERT_EQ(path.candidates.size(), 1);
    EXPECT_EQ(path.candidates.at(0).remoteId, QStringLiteral("2"));
}

TEST(RestLibraryClientTest, IgnoresStaleMixManDiagnosticsReplies) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy diagnosticsSpy(&client, &RestLibraryClient::diagnosticsUpdated);
    ::testing::InSequence sequence;
    MockNetworkReply* pOldHealthReply = network.ExpectGet(
            QStringLiteral("/health"),
            {},
            200,
            R"json({"ok":true})json");
    MockNetworkReply* pOldIndexReply = network.ExpectGet(
            QStringLiteral("/recommendations/index_status"),
            {},
            200,
            R"json({"ready":true,"count":1,"dim":2})json");
    MockNetworkReply* pCurrentHealthReply = network.ExpectGet(
            QStringLiteral("/health"),
            {},
            200,
            R"json({"ok":true})json");
    MockNetworkReply* pCurrentIndexReply = network.ExpectGet(
            QStringLiteral("/recommendations/index_status"),
            {},
            200,
            R"json({"ready":true,"count":3,"dim":4})json");

    client.fetchMixManDiagnostics(newMixManSettings());
    client.fetchMixManDiagnostics(newMixManSettings());
    pCurrentHealthReply->Done();
    pCurrentIndexReply->Done();
    pOldHealthReply->Done();
    pOldIndexReply->Done();

    EXPECT_EQ(diagnosticsSpy.count(), 2);
}

TEST(RestLibraryClientTest, IgnoresInvalidatedMixManPolicyPresetReply) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy presetsSpy(&client, &RestLibraryClient::policyPresetsFetched);
    MockNetworkReply* pReply = network.ExpectGet(
            QStringLiteral("/recommendations/policy-presets"),
            {},
            200,
            R"json([{"key":"old","label":"Old"}])json");

    client.fetchMixManPolicyPresets(newMixManSettings());
    client.invalidateMixManRequests();
    pReply->Done();

    EXPECT_EQ(presetsSpy.count(), 0);
}

TEST(RestLibraryClientTest, IgnoresStaleMixManSessionCreateReply) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy createdSpy(&client, &RestLibraryClient::mixManSessionCreated);
    QSignalSpy statusSpy(&client, &RestLibraryClient::mixManSessionWriteStatusUpdated);
    MockNetworkReply* pOldReply = network.ExpectPost(
            QStringLiteral("/sessions"),
            {},
            {QStringLiteral("\"client_id\":\"old-client\"")},
            201,
            R"json({"session":{"id":"old-session"},"clients":[],"recent_events":[]})json");
    MockNetworkReply* pCurrentReply = network.ExpectPost(
            QStringLiteral("/sessions"),
            {},
            {QStringLiteral("\"client_id\":\"current-client\"")},
            201,
            R"json({"session":{"id":"current-session"},"clients":[],"recent_events":[]})json");

    client.createMixManSession(newMixManSettings(), QStringLiteral("old-client"));
    client.createMixManSession(newMixManSettings(), QStringLiteral("current-client"));
    pCurrentReply->Done();
    pOldReply->Done();

    ASSERT_EQ(createdSpy.count(), 1);
    const auto session = qvariant_cast<mixxx::library::rest::RestLibrarySession>(
            createdSpy.takeFirst().at(0));
    EXPECT_EQ(session.id, QStringLiteral("current-session"));
    EXPECT_EQ(statusSpy.count(), 1);
}

TEST(RestLibraryClientTest, IgnoresInvalidatedMixManSessionWriteReply) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy statusSpy(&client, &RestLibraryClient::mixManSessionWriteStatusUpdated);
    MockNetworkReply* pReply = network.ExpectPut(
            QStringLiteral("/sessions/session-1/snapshot"),
            {},
            {QStringLiteral("\"client_id\":\"client-1\"")},
            200,
            R"json({"session_id":"session-1","snapshot":{}})json");
    mixxx::library::rest::RestLibrarySessionSnapshot snapshot;
    snapshot.clientId = QStringLiteral("client-1");

    client.publishMixManSessionSnapshot(
            newMixManSettings(),
            QStringLiteral("session-1"),
            snapshot);
    client.invalidateMixManRequests();
    pReply->Done();

    EXPECT_EQ(statusSpy.count(), 0);
}

TEST(RestLibraryClientTest, CreatesMixManSession) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy createdSpy(&client, &RestLibraryClient::mixManSessionCreated);
    QSignalSpy statusSpy(&client, &RestLibraryClient::mixManSessionWriteStatusUpdated);
    MockNetworkReply* pReply = network.ExpectPost(
            QStringLiteral("/sessions"),
            {},
            {QStringLiteral("\"client_id\":\"client-1\""),
                    QStringLiteral("\"role\":\"dj\""),
                    QStringLiteral("\"source\":\"mixxx\""),
                    QStringLiteral("\"surface\":\"rest_library\"")},
            201,
            R"json({
              "session": {
                "id": "session-1",
                "display_name": "Kitchen Party",
                "status": "active"
              },
              "snapshot": null,
              "clients": [],
              "recent_events": []
            })json");

    client.createMixManSession(newMixManSettings(), QStringLiteral("client-1"));
    pReply->Done();

    ASSERT_EQ(createdSpy.count(), 1);
    const auto session = qvariant_cast<mixxx::library::rest::RestLibrarySession>(
            createdSpy.takeFirst().at(0));
    EXPECT_EQ(session.id, QStringLiteral("session-1"));
    ASSERT_EQ(statusSpy.count(), 1);
    const auto status =
            qvariant_cast<mixxx::library::rest::RestLibrarySessionWriteStatus>(
                    statusSpy.takeFirst().at(0));
    EXPECT_TRUE(status.success);
    EXPECT_EQ(status.operation, QStringLiteral("session_create"));
}

TEST(RestLibraryClientTest, PublishesMixManSessionSnapshot) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy statusSpy(&client, &RestLibraryClient::mixManSessionWriteStatusUpdated);
    MockNetworkReply* pReply = network.ExpectPut(
            QStringLiteral("/sessions/session-1/snapshot"),
            {},
            {QStringLiteral("\"client_id\":\"client-1\""),
                    QStringLiteral("\"current_track_id\":8"),
                    QStringLiteral("\"playback_state\":\"playing\""),
                    QStringLiteral("\"snapshot\""),
                    QStringLiteral("\"title\":\"Night Train\""),
                    QStringLiteral("\"previous_track_id\":\"7\""),
                    QStringLiteral("\"recent_track_ids\":[\"7\",\"6\"]"),
                    QStringLiteral("\"policy\""),
                    QStringLiteral("\"playback\"")},
            200,
            R"json({"session_id":"session-1","snapshot":{}})json");

    mixxx::library::rest::RestLibrarySessionSnapshot snapshot;
    snapshot.clientId = QStringLiteral("client-1");
    snapshot.source = QStringLiteral("mixxx");
    snapshot.surface = QStringLiteral("rest_library");
    snapshot.currentTrackId = QStringLiteral("8");
    snapshot.playbackState = QStringLiteral("playing");
    snapshot.snapshot.insert(QStringLiteral("title"), QStringLiteral("Night Train"));
    snapshot.snapshot.insert(QStringLiteral("previous_track_id"), QStringLiteral("7"));
    snapshot.snapshot.insert(
            QStringLiteral("recent_track_ids"),
            QJsonArray{QStringLiteral("7"), QStringLiteral("6")});
    snapshot.snapshot.insert(
            QStringLiteral("policy"),
            QJsonObject{{QStringLiteral("policy_preset"), QStringLiteral("build_energy")}});
    snapshot.snapshot.insert(
            QStringLiteral("playback"),
            QJsonObject{{QStringLiteral("state"), QStringLiteral("playing")}});

    client.publishMixManSessionSnapshot(
            newMixManSettings(),
            QStringLiteral("session-1"),
            snapshot);
    pReply->Done();

    ASSERT_EQ(statusSpy.count(), 1);
    const auto status =
            qvariant_cast<mixxx::library::rest::RestLibrarySessionWriteStatus>(
                    statusSpy.takeFirst().at(0));
    EXPECT_TRUE(status.success);
    EXPECT_EQ(status.operation, QStringLiteral("session_snapshot"));
}

TEST(RestLibraryClientTest, UpdatesMixManSessionIntent) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy statusSpy(&client, &RestLibraryClient::mixManSessionWriteStatusUpdated);
    MockNetworkReply* pReply = network.ExpectPut(
            QStringLiteral("/sessions/session-1/intent"),
            {},
            {QStringLiteral("\"client_id\":\"client-1\""),
                    QStringLiteral("\"status\":\"active\""),
                    QStringLiteral("\"policy_preset\":\"build_energy\""),
                    QStringLiteral("\"target_energy\":0.8"),
                    QStringLiteral("\"target_color\":\"#ff6600\"")},
            200,
            R"json({"session_id":"session-1","revision":1})json");

    mixxx::library::rest::RestLibrarySessionIntent intent;
    intent.clientId = QStringLiteral("client-1");
    intent.source = QStringLiteral("mixxx");
    intent.surface = QStringLiteral("rest_library");
    intent.policyPreset = QStringLiteral("build_energy");
    intent.targetEnergyEnabled = true;
    intent.targetEnergy = 0.8;
    intent.targetColorEnabled = true;
    intent.targetColor = QStringLiteral("#ff6600");

    client.updateMixManSessionIntent(
            newMixManSettings(),
            QStringLiteral("session-1"),
            intent);
    pReply->Done();

    ASSERT_EQ(statusSpy.count(), 1);
    const auto status =
            qvariant_cast<mixxx::library::rest::RestLibrarySessionWriteStatus>(
                    statusSpy.takeFirst().at(0));
    EXPECT_TRUE(status.success);
    EXPECT_EQ(status.operation, QStringLiteral("session_intent"));
}

TEST(RestLibraryClientTest, SendsMixManSessionHeartbeat) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy statusSpy(&client, &RestLibraryClient::mixManSessionWriteStatusUpdated);
    MockNetworkReply* pReply = network.ExpectPost(
            QStringLiteral("/sessions/session-1/heartbeat"),
            {},
            {QStringLiteral("\"client_id\":\"client-1\""),
                    QStringLiteral("\"status\":\"active\""),
                    QStringLiteral("\"role\":\"dj\"")},
            200,
            R"json({"session_id":"session-1","client_id":"client-1"})json");

    client.sendMixManSessionHeartbeat(
            newMixManSettings(),
            QStringLiteral("session-1"),
            QStringLiteral("client-1"));
    pReply->Done();

    ASSERT_EQ(statusSpy.count(), 1);
    const auto status =
            qvariant_cast<mixxx::library::rest::RestLibrarySessionWriteStatus>(
                    statusSpy.takeFirst().at(0));
    EXPECT_TRUE(status.success);
    EXPECT_EQ(status.operation, QStringLiteral("session_heartbeat"));
}

TEST(RestLibraryClientTest, FetchesMixManAuthoritativeSession) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy fetchedSpy(&client, &RestLibraryClient::mixManSessionFetched);
    MockNetworkReply* pReply = network.ExpectGet(
            QStringLiteral("/sessions/session-1"),
            {},
            200,
            R"json({
              "session": {"id": "session-1"},
              "authoritative": {"session_id": "session-1", "revision": 2}
            })json");

    client.fetchMixManSession(newMixManSettings(), QStringLiteral("session-1"));
    pReply->Done();

    ASSERT_EQ(fetchedSpy.count(), 1);
    const auto session = qvariant_cast<mixxx::library::rest::RestLibrarySession>(
            fetchedSpy.takeFirst().at(0));
    EXPECT_EQ(session.id, QStringLiteral("session-1"));
    EXPECT_EQ(session.authoritative.revision, 2);
}

TEST(RestLibraryClientTest, PublishesMixManSessionPlayback) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy fetchedSpy(&client, &RestLibraryClient::mixManSessionFetched);
    QSignalSpy statusSpy(&client, &RestLibraryClient::mixManSessionWriteStatusUpdated);
    MockNetworkReply* pReply = network.ExpectPost(
            QStringLiteral("/sessions/session-1/playback"),
            {},
            {QStringLiteral("\"client_id\":\"client-1\""),
                    QStringLiteral("\"role\":\"dj\""),
                    QStringLiteral("\"current_track_id\":8"),
                    QStringLiteral("\"previous_track_id\":7"),
                    QStringLiteral("\"playback_state\":\"playing\""),
                    QStringLiteral("\"current_track\""),
                    QStringLiteral("\"title\":\"Night Train\"")},
            200,
            R"json({
              "session": {"id": "session-1"},
              "authoritative": {
                "session_id": "session-1",
                "revision": 3,
                "candidates": [{"track_id": 9, "title": "Next"}]
              }
            })json");

    mixxx::library::rest::RestLibrarySessionPlayback playback;
    playback.clientId = QStringLiteral("client-1");
    playback.source = QStringLiteral("mixxx");
    playback.surface = QStringLiteral("rest_library");
    playback.currentTrackId = QStringLiteral("8");
    playback.previousTrackId = QStringLiteral("7");
    playback.playbackState = QStringLiteral("playing");
    playback.currentTrack.insert(QStringLiteral("title"), QStringLiteral("Night Train"));

    client.publishMixManSessionPlayback(
            newMixManSettings(),
            QStringLiteral("session-1"),
            playback);
    pReply->Done();

    ASSERT_EQ(statusSpy.count(), 1);
    const auto status =
            qvariant_cast<mixxx::library::rest::RestLibrarySessionWriteStatus>(
                    statusSpy.takeFirst().at(0));
    EXPECT_TRUE(status.success);
    EXPECT_EQ(status.operation, QStringLiteral("session_playback"));
    ASSERT_EQ(fetchedSpy.count(), 1);
    const auto session = qvariant_cast<mixxx::library::rest::RestLibrarySession>(
            fetchedSpy.takeFirst().at(0));
    EXPECT_EQ(session.authoritative.revision, 3);
    ASSERT_EQ(session.authoritative.policyPath.candidates.size(), 1);
    EXPECT_EQ(session.authoritative.policyPath.candidates.at(0).remoteId, QStringLiteral("9"));
}

TEST(RestLibraryClientTest, ClaimsMixManSessionControlAsDj) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy statusSpy(&client, &RestLibraryClient::mixManSessionWriteStatusUpdated);
    MockNetworkReply* pReply = network.ExpectPost(
            QStringLiteral("/sessions/session-1/control/claim"),
            {},
            {QStringLiteral("\"client_id\":\"client-1\""),
                    QStringLiteral("\"role\":\"dj\""),
                    QStringLiteral("\"ttl_seconds\":45")},
            200,
            R"json({"session_id":"session-1"})json");

    client.claimMixManSessionControl(
            newMixManSettings(),
            QStringLiteral("session-1"),
            QStringLiteral("client-1"));
    pReply->Done();

    ASSERT_EQ(statusSpy.count(), 1);
    const auto status =
            qvariant_cast<mixxx::library::rest::RestLibrarySessionWriteStatus>(
                    statusSpy.takeFirst().at(0));
    EXPECT_TRUE(status.success);
    EXPECT_EQ(status.operation, QStringLiteral("session_control_claim"));
}

TEST(RestLibraryClientTest, SelectsMixManSessionCandidateAndReportsConflict) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy statusSpy(&client, &RestLibraryClient::mixManSessionWriteStatusUpdated);
    MockNetworkReply* pReply = network.ExpectPost(
            QStringLiteral("/sessions/session-1/candidates/9/select"),
            {},
            {QStringLiteral("\"client_id\":\"client-1\""),
                    QStringLiteral("\"role\":\"dj\""),
                    QStringLiteral("\"selection_origin\":\"recommendation_reroll\""),
                    QStringLiteral("\"allow_external_candidate\":true")},
            409,
            R"json({"detail":{"reason":"candidate_not_authoritative"}})json");

    client.selectMixManSessionCandidate(
            newMixManSettings(),
            QStringLiteral("session-1"),
            QStringLiteral("9"),
            QStringLiteral("client-1"),
            QStringLiteral("recommendation_reroll"),
            true);
    pReply->Done();

    ASSERT_EQ(statusSpy.count(), 1);
    const auto status =
            qvariant_cast<mixxx::library::rest::RestLibrarySessionWriteStatus>(
                    statusSpy.takeFirst().at(0));
    EXPECT_FALSE(status.success);
    EXPECT_EQ(status.statusCode, 409);
    EXPECT_EQ(status.operation, QStringLiteral("session_candidate_select"));
}

TEST(RestLibraryClientTest, PublishesMixManPolicyRefreshAction) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy fetchedSpy(&client, &RestLibraryClient::mixManSessionFetched);
    QSignalSpy statusSpy(&client, &RestLibraryClient::mixManSessionWriteStatusUpdated);
    MockNetworkReply* pReply = network.ExpectPost(
            QStringLiteral("/sessions/session-1/actions"),
            {},
            {QStringLiteral("\"client_id\":\"client-1\""),
                    QStringLiteral("\"role\":\"dj\""),
                    QStringLiteral("\"action_type\":\"policy_refresh\""),
                    QStringLiteral("\"policy_preset\":\"build_energy\""),
                    QStringLiteral("\"target_energy\":0.8"),
                    QStringLiteral("\"target_color\":\"#ff6600\""),
                    QStringLiteral("\"target_bpm\":132"),
                    QStringLiteral("\"reroll_constraints\""),
                    QStringLiteral("\"mode\":\"fuzzy\""),
                    QStringLiteral("\"limit\":10")},
            200,
            R"json({
              "session": {"id": "session-1"},
              "authoritative": {
                "session_id": "session-1",
                "revision": 4,
                "candidates": [{"track_id": 12, "title": "Reroll"}]
              }
            })json");

    client.publishMixManPolicyRefreshAction(
            newMixManSettings(),
            QStringLiteral("session-1"),
            QStringLiteral("client-1"));
    pReply->Done();

    ASSERT_EQ(statusSpy.count(), 1);
    const auto status =
            qvariant_cast<mixxx::library::rest::RestLibrarySessionWriteStatus>(
                    statusSpy.takeFirst().at(0));
    EXPECT_TRUE(status.success);
    EXPECT_EQ(status.operation, QStringLiteral("session_policy_refresh"));
    ASSERT_EQ(fetchedSpy.count(), 1);
    const auto session = qvariant_cast<mixxx::library::rest::RestLibrarySession>(
            fetchedSpy.takeFirst().at(0));
    EXPECT_EQ(session.authoritative.revision, 4);
    ASSERT_EQ(session.authoritative.policyPath.candidates.size(), 1);
    EXPECT_EQ(session.authoritative.policyPath.candidates.at(0).remoteId, QStringLiteral("12"));
}
