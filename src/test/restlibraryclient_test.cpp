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
using mixxx::library::rest::RestLibraryRequestDiagnostic;
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

QByteArray mixManSessionContractV3() {
    return QByteArrayLiteral(R"json({
      "session_contract": {
        "version": 3,
        "base_path": "/api/v3",
        "surfaces": [{
          "application": "mixxx",
          "surface": "rest_library",
          "required_scope": "session:playback:dj",
          "capabilities": [
            "session.read",
            "session.recommendations.read",
            "session.intent.write",
            "session.intent.ack",
            "session.plan.write",
            "session.action.write",
            "playback.claim",
            "playback.publish"
          ]
        }],
        "lease_ttl_seconds": 30,
        "renew_interval_seconds": 10,
        "pause_grace_seconds": 15,
        "heartbeat_interval_seconds": 30,
        "active_timeout_seconds": 90
      }
    })json");
}

QByteArray mixManRegistrationV3(const QString& sessionId = QStringLiteral("session-1")) {
    return QStringLiteral(R"json({
      "session_id":"%1",
      "instance":{
        "instance_id":"inst-test",
        "status":"active",
        "active":true,
        "capabilities":[
          "session.read","session.recommendations.read","session.intent.write",
          "session.intent.ack","session.plan.write","session.action.write",
          "playback.claim","playback.publish"
        ]
      },
      "resume_token":"resume-token-123456789012345678901",
      "heartbeat_interval_seconds":30,
      "active_timeout_seconds":90
    })json")
            .arg(sessionId)
            .toUtf8();
}

QByteArray mixManLeaseV3() {
    return QByteArrayLiteral(R"json({
      "session_id":"session-1",
      "revision":1,
      "playback_controller":{
        "instance_id":"inst-test","lease_id":"lease-test",
        "generation":1,"active":true
      }
    })json");
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

TEST(RestLibraryClientTest, ParsesMixManSessionContractV3) {
    const auto contract = RestLibraryClient::parseSessionContractDocumentForTesting(
            QJsonDocument::fromJson(mixManSessionContractV3()));

    EXPECT_TRUE(contract.valid);
    EXPECT_EQ(contract.version, 3);
    EXPECT_EQ(contract.basePath, QStringLiteral("/api/v3"));
    EXPECT_EQ(contract.leaseTtlSeconds, 30);
    EXPECT_EQ(contract.leaseRenewIntervalSeconds, 10);
    EXPECT_EQ(contract.pauseGraceSeconds, 15);
}

TEST(RestLibraryClientTest, RejectsLegacyMixManSessionContract) {
    const auto contract = RestLibraryClient::parseSessionContractDocumentForTesting(
            QJsonDocument::fromJson(R"json({
              "session_contract": {
                "version": 1,
                "client_kinds": ["mixxx"],
                "playback_capable_client_kinds": ["mixxx"]
              }
            })json"));

    EXPECT_FALSE(contract.valid);
    EXPECT_TRUE(contract.errorText.contains(QStringLiteral("v3")));
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
            "playback_controller": {"instance_id": "inst-1", "lease_id": "lease-1", "generation": 4, "active": true},
            "blocked": {"sector_change": {"reason": "beacon"}},
            "intents": [{"intent_type": "beacon"}],
            "queue": [{"track_id": 10}],
            "candidates": {
              "id": 7,
              "candidates": [{
                  "id": 70,
                  "track_id": 9,
                  "title": "Next Track",
                  "artist": "Ada",
                  "score": 0.91
              }]
            },
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
    EXPECT_TRUE(session.authoritative.playbackLease.isValidFor(QStringLiteral("inst-1")));
    ASSERT_EQ(session.authoritative.policyPath.candidates.size(), 1);
    EXPECT_EQ(session.authoritative.policyPath.candidates.at(0).remoteId, QStringLiteral("9"));
    EXPECT_EQ(session.authoritative.policyPath.candidates.at(0).sourceLabel,
            QStringLiteral("MixMan Authoritative"));
    EXPECT_TRUE(session.authoritative.policyPath.candidates.at(0).reasonCodes.isEmpty());
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

TEST(RestLibraryClientTest, EmitsDiagnosticForAuthFailure) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy diagnosticsSpy(&client, &RestLibraryClient::requestDiagnosticUpdated);
    QSignalSpy failedSpy(&client, &RestLibraryClient::fetchFailed);
    RestLibrarySettings settings = newSettings();
    settings.baseUrl = QUrl(QStringLiteral("http://user:secret@example.invalid"));
    MockNetworkReply* pReply = network.ExpectGet(
            QStringLiteral("/configured-list"),
            {{"limit", "5"}},
            401,
            R"json({"detail":"bad token"})json");

    client.fetchTracks(settings);
    pReply->Done();

    ASSERT_EQ(diagnosticsSpy.count(), 1);
    const auto diagnostic =
            qvariant_cast<RestLibraryRequestDiagnostic>(diagnosticsSpy.takeFirst().at(0));
    EXPECT_FALSE(diagnostic.success);
    EXPECT_EQ(diagnostic.statusCode, 401);
    EXPECT_EQ(diagnostic.stage, QStringLiteral("Track list"));
    EXPECT_TRUE(diagnostic.url.contains(QStringLiteral("/configured-list")));
    EXPECT_FALSE(diagnostic.url.contains(QStringLiteral("secret")));
    EXPECT_TRUE(diagnostic.responseSnippet.contains(QStringLiteral("bad token")));
    EXPECT_TRUE(diagnostic.summary.contains(QStringLiteral("Authentication failed")));
    ASSERT_EQ(failedSpy.count(), 1);
    const QString failureMessage = failedSpy.takeFirst().at(0).toString();
    EXPECT_TRUE(failureMessage.contains(QStringLiteral("Authentication failed")));
    EXPECT_TRUE(failureMessage.contains(QStringLiteral("401")));
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

TEST(RestLibraryClientTest, TestsMixManConnectionSuccessfully) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy diagnosticsSpy(&client, &RestLibraryClient::requestDiagnosticUpdated);
    QSignalSpy finishedSpy(&client, &RestLibraryClient::connectionTestFinished);
    MockNetworkReply* pHealthReply = network.ExpectGet(
            QStringLiteral("/health"),
            {},
            200,
            R"json({"ok":true})json");
    MockNetworkReply* pConfigReply = network.ExpectGet(
            QStringLiteral("/config"), {}, 200, mixManSessionContractV3());
    MockNetworkReply* pIndexReply = network.ExpectGet(
            QStringLiteral("/recommendations/index_status"),
            {},
            200,
            R"json({"ready":true,"count":3,"dim":4})json");
    MockNetworkReply* pTracksReply = network.ExpectGet(
            QStringLiteral("/configured-list"),
            {{"limit", "1"}},
            200,
            R"json([{"id":1,"title":"One"}])json");
    MockNetworkReply* pSessionReply = network.ExpectPost(
            QStringLiteral("/api/v3/sessions"),
            {},
            {QStringLiteral("\"session_id\":"),
                    QStringLiteral("\"connection_test\":true")},
            {QStringLiteral("\"role\"")},
            201,
            R"json({"id":"session-1","status":"active"})json");
    MockNetworkReply* pRegisterReply = network.ExpectPost(
            QStringLiteral("/api/v3/sessions/session-1/instances"),
            {},
            {QStringLiteral("\"application\":\"mixxx\""),
                    QStringLiteral("\"surface\":\"rest_library\"")},
            201,
            mixManRegistrationV3());
    MockNetworkReply* pStateReply = network.ExpectGet(
            QStringLiteral("/api/v3/sessions/session-1/state"),
            {{QStringLiteral("instance_id"), QStringLiteral("inst-test")}},
            200,
            R"json({"session":{"id":"session-1"},"self":{"instance_id":"inst-test"},"authoritative":{"session_id":"session-1","revision":0}})json");
    MockNetworkReply* pClaimReply = network.ExpectPost(
            QStringLiteral("/api/v3/sessions/session-1/playback-control/claim"),
            {},
            {QStringLiteral("\"instance_id\":\"inst-test\"")},
            200,
            mixManLeaseV3());
    MockNetworkReply* pReleaseReply = network.ExpectPost(
            QStringLiteral("/api/v3/sessions/session-1/playback-control/release"),
            {},
            {QStringLiteral("\"lease_id\":\"lease-test\""),
                    QStringLiteral("\"generation\":1")},
            200,
            R"json({"session_id":"session-1","playback_controller":null})json");
    MockNetworkReply* pDisconnectReply = network.ExpectPost(
            QStringLiteral("/api/v3/sessions/session-1/instances/inst-test/disconnect"),
            {},
            {},
            204,
            {});

    client.testMixManConnection(newMixManSettings(), {}, true);
    pHealthReply->Done();
    pConfigReply->Done();
    pIndexReply->Done();
    pTracksReply->Done();
    pSessionReply->Done();
    pRegisterReply->Done();
    pStateReply->Done();
    pClaimReply->Done();
    pReleaseReply->Done();
    pDisconnectReply->Done();

    EXPECT_EQ(diagnosticsSpy.count(), 10);
    ASSERT_EQ(finishedSpy.count(), 1);
    EXPECT_TRUE(finishedSpy.takeFirst().at(0).toBool());
}

TEST(RestLibraryClientTest, TestMixManConnectionReportsInvalidJson) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy diagnosticsSpy(&client, &RestLibraryClient::requestDiagnosticUpdated);
    QSignalSpy finishedSpy(&client, &RestLibraryClient::connectionTestFinished);
    MockNetworkReply* pHealthReply = network.ExpectGet(
            QStringLiteral("/health"),
            {},
            200,
            R"json({"ok":true})json");
    MockNetworkReply* pConfigReply = network.ExpectGet(
            QStringLiteral("/config"), {}, 200, mixManSessionContractV3());
    MockNetworkReply* pIndexReply = network.ExpectGet(
            QStringLiteral("/recommendations/index_status"),
            {},
            200,
            QByteArrayLiteral("{"));

    client.testMixManConnection(newMixManSettings());
    pHealthReply->Done();
    pConfigReply->Done();
    pIndexReply->Done();

    ASSERT_EQ(diagnosticsSpy.count(), 3);
    const auto diagnostic =
            qvariant_cast<RestLibraryRequestDiagnostic>(diagnosticsSpy.takeLast().at(0));
    EXPECT_FALSE(diagnostic.success);
    EXPECT_TRUE(diagnostic.summary.contains(QStringLiteral("not valid JSON")));
    ASSERT_EQ(finishedSpy.count(), 1);
    EXPECT_FALSE(finishedSpy.takeFirst().at(0).toBool());
}

TEST(RestLibraryClientTest, TestMixManConnectionReportsIndexNotReady) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy diagnosticsSpy(&client, &RestLibraryClient::requestDiagnosticUpdated);
    QSignalSpy finishedSpy(&client, &RestLibraryClient::connectionTestFinished);
    MockNetworkReply* pHealthReply = network.ExpectGet(
            QStringLiteral("/health"),
            {},
            200,
            R"json({"ok":true})json");
    MockNetworkReply* pConfigReply = network.ExpectGet(
            QStringLiteral("/config"), {}, 200, mixManSessionContractV3());
    MockNetworkReply* pIndexReply = network.ExpectGet(
            QStringLiteral("/recommendations/index_status"),
            {},
            200,
            R"json({"ready":false,"count":3,"dim":4})json");

    client.testMixManConnection(newMixManSettings());
    pHealthReply->Done();
    pConfigReply->Done();
    pIndexReply->Done();

    ASSERT_EQ(diagnosticsSpy.count(), 3);
    const auto diagnostic =
            qvariant_cast<RestLibraryRequestDiagnostic>(diagnosticsSpy.takeLast().at(0));
    EXPECT_FALSE(diagnostic.success);
    EXPECT_TRUE(diagnostic.summary.contains(QStringLiteral("not ready")));
    ASSERT_EQ(finishedSpy.count(), 1);
    EXPECT_FALSE(finishedSpy.takeFirst().at(0).toBool());
}

TEST(RestLibraryClientTest, TestMixManConnectionRejectsLegacySessionContract) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy diagnosticsSpy(&client, &RestLibraryClient::requestDiagnosticUpdated);
    QSignalSpy finishedSpy(&client, &RestLibraryClient::connectionTestFinished);
    MockNetworkReply* pHealthReply = network.ExpectGet(
            QStringLiteral("/health"), {}, 200, R"json({"ok":true})json");
    MockNetworkReply* pConfigReply = network.ExpectGet(
            QStringLiteral("/config"),
            {},
            200,
            R"json({"session_contract":{"version":1,"client_kinds":["mixxx"]}})json");

    client.testMixManConnection(newMixManSettings(), {}, true);
    pHealthReply->Done();
    pConfigReply->Done();

    ASSERT_EQ(diagnosticsSpy.count(), 2);
    const auto diagnostic =
            qvariant_cast<RestLibraryRequestDiagnostic>(diagnosticsSpy.takeLast().at(0));
    EXPECT_FALSE(diagnostic.success);
    EXPECT_TRUE(diagnostic.summary.contains(QStringLiteral("v3")));
    ASSERT_EQ(finishedSpy.count(), 1);
    EXPECT_FALSE(finishedSpy.takeFirst().at(0).toBool());
}

TEST(RestLibraryClientTest, TestMixManConnectionReportsNetworkError) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy diagnosticsSpy(&client, &RestLibraryClient::requestDiagnosticUpdated);
    QSignalSpy finishedSpy(&client, &RestLibraryClient::connectionTestFinished);
    MockNetworkReply* pHealthReply = network.ExpectGet(
            QStringLiteral("/health"),
            {},
            0,
            QByteArray());

    client.testMixManConnection(newMixManSettings());
    static_cast<QNetworkReply*>(pHealthReply)->abort();

    ASSERT_EQ(diagnosticsSpy.count(), 1);
    const auto diagnostic =
            qvariant_cast<RestLibraryRequestDiagnostic>(diagnosticsSpy.takeFirst().at(0));
    EXPECT_FALSE(diagnostic.success);
    EXPECT_NE(diagnostic.networkError, static_cast<int>(QNetworkReply::NoError));
    ASSERT_EQ(finishedSpy.count(), 1);
    EXPECT_FALSE(finishedSpy.takeFirst().at(0).toBool());
}

TEST(RestLibraryClientTest, CancelMixManConnectionTestIgnoresActiveReply) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy diagnosticsSpy(&client, &RestLibraryClient::requestDiagnosticUpdated);
    QSignalSpy finishedSpy(&client, &RestLibraryClient::connectionTestFinished);
    network.ExpectGet(
            QStringLiteral("/health"),
            {},
            200,
            R"json({"ok":true})json");

    client.testMixManConnection(newMixManSettings());
    client.cancelMixManConnectionTest();

    EXPECT_EQ(diagnosticsSpy.count(), 0);
    EXPECT_EQ(finishedSpy.count(), 0);
}

TEST(RestLibraryClientTest, IgnoresStaleMixManConnectionTestReply) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy diagnosticsSpy(&client, &RestLibraryClient::requestDiagnosticUpdated);
    QSignalSpy finishedSpy(&client, &RestLibraryClient::connectionTestFinished);
    ::testing::InSequence sequence;
    MockNetworkReply* pOldHealthReply = network.ExpectGet(
            QStringLiteral("/health"),
            {},
            500,
            R"json({"detail":"old"})json");

    client.testMixManConnection(newMixManSettings(), {}, true);

    MockNetworkReply* pCurrentHealthReply = network.ExpectGet(
            QStringLiteral("/health"),
            {},
            200,
            R"json({"ok":true})json");
    MockNetworkReply* pCurrentConfigReply = network.ExpectGet(
            QStringLiteral("/config"), {}, 200, mixManSessionContractV3());
    MockNetworkReply* pCurrentIndexReply = network.ExpectGet(
            QStringLiteral("/recommendations/index_status"),
            {},
            200,
            R"json({"ready":true,"count":3,"dim":4})json");
    MockNetworkReply* pCurrentTracksReply = network.ExpectGet(
            QStringLiteral("/configured-list"),
            {{"limit", "1"}},
            200,
            R"json([{"id":1,"title":"One"}])json");
    client.testMixManConnection(newMixManSettings(), {}, false);
    pCurrentHealthReply->Done();
    pCurrentConfigReply->Done();
    pCurrentIndexReply->Done();
    pCurrentTracksReply->Done();
    pOldHealthReply->Done();

    EXPECT_EQ(diagnosticsSpy.count(), 4);
    ASSERT_EQ(finishedSpy.count(), 1);
    EXPECT_TRUE(finishedSpy.takeFirst().at(0).toBool());
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

TEST(RestLibraryClientTest, RecommendationDetailsUseOriginalSettingsAfterDiagnosticsRequest) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy fetchedSpy(&client, &RestLibraryClient::recommendationsFetched);
    MockNetworkReply* pListReply = network.ExpectGet(
            QStringLiteral("/configured-related/source-1"),
            {{"limit", "5"}},
            200,
            R"json(["remote-8"])json");
    MockNetworkReply* pHealthReply = network.ExpectGet(
            QStringLiteral("/health"),
            {},
            200,
            R"json({"ok":true})json");
    MockNetworkReply* pIndexReply = network.ExpectGet(
            QStringLiteral("/recommendations/index_status"),
            {},
            200,
            R"json({"ready":true,"count":3,"dim":4})json");
    MockNetworkReply* pDetailReply = network.ExpectGet(
            QStringLiteral("/configured-detail/remote-8"),
            {},
            200,
            R"json({"id":"remote-8","title":"Original Detail Path"})json");

    client.fetchRecommendations(newRecommendationSettings(), QStringLiteral("source-1"));
    client.fetchMixManDiagnostics(newMixManSettings());
    pHealthReply->Done();
    pIndexReply->Done();
    pListReply->Done();
    pDetailReply->Done();

    ASSERT_EQ(fetchedSpy.count(), 1);
    const auto tracks = qvariant_cast<QList<mixxx::library::rest::RestLibraryTrack>>(
            fetchedSpy.takeFirst().at(0));
    ASSERT_EQ(tracks.size(), 1);
    EXPECT_EQ(tracks.at(0).title, QStringLiteral("Original Detail Path"));
}

TEST(RestLibraryClientTest, ConnectionTestUsesOriginalSettingsAfterDiagnosticsRequest) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy finishedSpy(&client, &RestLibraryClient::connectionTestFinished);
    ::testing::InSequence sequence;
    MockNetworkReply* pConnectionHealthReply = network.ExpectGet(
            QStringLiteral("/health"),
            {},
            200,
            R"json({"ok":true})json");
    MockNetworkReply* pDiagnosticsHealthReply = network.ExpectGet(
            QStringLiteral("/health"),
            {},
            200,
            R"json({"ok":true})json");
    MockNetworkReply* pDiagnosticsIndexReply = network.ExpectGet(
            QStringLiteral("/recommendations/index_status"),
            {},
            200,
            R"json({"ready":true,"count":3,"dim":4})json");
    MockNetworkReply* pConnectionConfigReply = network.ExpectGet(
            QStringLiteral("/config"), {}, 200, mixManSessionContractV3());
    MockNetworkReply* pConnectionIndexReply = network.ExpectGet(
            QStringLiteral("/recommendations/index_status"),
            {},
            200,
            R"json({"ready":true,"count":3,"dim":4})json");
    MockNetworkReply* pTracksReply = network.ExpectGet(
            QStringLiteral("/connection-list"),
            {{"limit", "1"}},
            200,
            R"json([{"id":1,"title":"One"}])json");
    RestLibrarySettings connectionSettings = newSettings();
    connectionSettings.trackListPath = QStringLiteral("/connection-list");

    client.testMixManConnection(connectionSettings);
    client.fetchMixManDiagnostics(newMixManSettings());
    pConnectionHealthReply->Done();
    pDiagnosticsHealthReply->Done();
    pDiagnosticsIndexReply->Done();
    pConnectionConfigReply->Done();
    pConnectionIndexReply->Done();
    pTracksReply->Done();

    ASSERT_EQ(finishedSpy.count(), 1);
    EXPECT_TRUE(finishedSpy.takeFirst().at(0).toBool());
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

TEST(RestLibraryClientTest, IgnoresStaleMixManInstanceRegistrationReply) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy registeredSpy(&client, &RestLibraryClient::mixManSessionInstanceRegistered);
    QSignalSpy statusSpy(&client, &RestLibraryClient::mixManSessionWriteStatusUpdated);
    MockNetworkReply* pOldConfigReply = network.ExpectGet(
            QStringLiteral("/config"), {}, 200, mixManSessionContractV3());
    MockNetworkReply* pOldReply = network.ExpectPost(
            QStringLiteral("/api/v3/sessions/old-session/instances"),
            {},
            {QStringLiteral("\"application\":\"mixxx\"")},
            201,
            R"json({"session_id":"old-session","instance":{"instance_id":"inst-old","capabilities":[],"active":true},"resume_token":"resume-token-old-1234567890123456","heartbeat_interval_seconds":30,"active_timeout_seconds":90})json");
    client.createMixManSession(newMixManSettings(), QStringLiteral("old-session"));
    pOldConfigReply->Done();

    MockNetworkReply* pCurrentConfigReply = network.ExpectGet(
            QStringLiteral("/config"), {}, 200, mixManSessionContractV3());
    MockNetworkReply* pCurrentReply = network.ExpectPost(
            QStringLiteral("/api/v3/sessions/current-session/instances"),
            {},
            {QStringLiteral("\"application\":\"mixxx\"")},
            201,
            R"json({"session_id":"current-session","instance":{"instance_id":"inst-current","capabilities":[],"active":true},"resume_token":"resume-token-current-1234567890123","heartbeat_interval_seconds":30,"active_timeout_seconds":90})json");

    client.createMixManSession(newMixManSettings(), QStringLiteral("current-session"));
    pCurrentConfigReply->Done();
    pCurrentReply->Done();
    pOldReply->Done();

    ASSERT_EQ(registeredSpy.count(), 1);
    const auto registration =
            qvariant_cast<mixxx::library::rest::RestLibrarySessionRegistration>(
                    registeredSpy.takeFirst().at(0));
    EXPECT_EQ(registration.sessionId, QStringLiteral("current-session"));
    EXPECT_EQ(statusSpy.count(), 1);
}

TEST(RestLibraryClientTest, IgnoresInvalidatedMixManSessionWriteReply) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy statusSpy(&client, &RestLibraryClient::mixManSessionWriteStatusUpdated);
    MockNetworkReply* pReply = network.ExpectPut(
            QStringLiteral("/api/v3/sessions/session-1/snapshot"),
            {},
            {QStringLiteral("\"instance_id\":\"inst-1\"")},
            200,
            R"json({"session_id":"session-1","snapshot":{}})json");
    mixxx::library::rest::RestLibrarySessionSnapshot snapshot;
    snapshot.lease = {QStringLiteral("inst-1"), QStringLiteral("lease-1"), 3, true};

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
    QSignalSpy registeredSpy(&client, &RestLibraryClient::mixManSessionInstanceRegistered);
    MockNetworkReply* pConfigReply = network.ExpectGet(
            QStringLiteral("/config"), {}, 200, mixManSessionContractV3());
    MockNetworkReply* pInitialRegisterReply = network.ExpectPost(
            QStringLiteral("/api/v3/sessions/session-1/instances"),
            {},
            {QStringLiteral("\"application\":\"mixxx\""),
                    QStringLiteral("\"surface\":\"rest_library\"")},
            404,
            R"json({"detail":"Session not found"})json");
    MockNetworkReply* pCreateReply = network.ExpectPost(
            QStringLiteral("/api/v3/sessions"),
            {},
            {QStringLiteral("\"session_id\":\"session-1\"")},
            201,
            R"json({
              "id": "session-1",
              "display_name": "Kitchen Party",
              "status": "active"
            })json");
    client.createMixManSession(newMixManSettings(), QStringLiteral("session-1"));
    pConfigReply->Done();
    pInitialRegisterReply->Done();
    MockNetworkReply* pRegisterReply = network.ExpectPost(
            QStringLiteral("/api/v3/sessions/session-1/instances"),
            {},
            {QStringLiteral("\"application\":\"mixxx\"")},
            201,
            R"json({"session_id":"session-1","instance":{"instance_id":"inst-1","capabilities":[],"active":true},"resume_token":"resume-token-123456789012345678901","heartbeat_interval_seconds":30,"active_timeout_seconds":90})json");
    pCreateReply->Done();
    pRegisterReply->Done();

    ASSERT_EQ(createdSpy.count(), 1);
    const auto session = qvariant_cast<mixxx::library::rest::RestLibrarySession>(
            createdSpy.takeFirst().at(0));
    EXPECT_EQ(session.id, QStringLiteral("session-1"));
    ASSERT_EQ(registeredSpy.count(), 1);
    EXPECT_GE(statusSpy.count(), 2);
}

TEST(RestLibraryClientTest, DoesNotCreateSessionWithoutContractV3) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy createdSpy(&client, &RestLibraryClient::mixManSessionCreated);
    QSignalSpy contractSpy(&client, &RestLibraryClient::mixManSessionContractVerified);
    QSignalSpy statusSpy(&client, &RestLibraryClient::mixManSessionWriteStatusUpdated);
    MockNetworkReply* pConfigReply = network.ExpectGet(
            QStringLiteral("/config"),
            {},
            200,
            R"json({"session_contract":{"version":1,"client_kinds":["mixxx"]}})json");

    client.createMixManSession(newMixManSettings(), QStringLiteral("client-1"));
    pConfigReply->Done();

    EXPECT_EQ(createdSpy.count(), 0);
    ASSERT_EQ(contractSpy.count(), 1);
    const auto contract = qvariant_cast<mixxx::library::rest::RestLibrarySessionContract>(
            contractSpy.takeFirst().at(0));
    EXPECT_FALSE(contract.valid);
    ASSERT_EQ(statusSpy.count(), 1);
    const auto status =
            qvariant_cast<mixxx::library::rest::RestLibrarySessionWriteStatus>(
                    statusSpy.takeFirst().at(0));
    EXPECT_FALSE(status.success);
    EXPECT_TRUE(status.errorText.contains(QStringLiteral("v3")));
}

TEST(RestLibraryClientTest, PublishesMixManSessionSnapshot) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy statusSpy(&client, &RestLibraryClient::mixManSessionWriteStatusUpdated);
    MockNetworkReply* pReply = network.ExpectPut(
            QStringLiteral("/api/v3/sessions/session-1/snapshot"),
            {},
            {QStringLiteral("\"instance_id\":\"inst-1\""),
                    QStringLiteral("\"lease_id\":\"lease-1\""),
                    QStringLiteral("\"lease_generation\":3"),
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
    snapshot.lease = {QStringLiteral("inst-1"), QStringLiteral("lease-1"), 3, true};
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

TEST(RestLibraryClientTest, ReportsPutForFailedMixManSessionSnapshot) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy diagnosticsSpy(&client,
            &RestLibraryClient::requestDiagnosticUpdated);
    MockNetworkReply* pReply =
            network.ExpectPut(QStringLiteral("/api/v3/sessions/session-1/snapshot"),
                    {},
                    {QStringLiteral("\"instance_id\":\"inst-1\"")},
                    422,
                    R"json({"detail":"invalid snapshot"})json");
    mixxx::library::rest::RestLibrarySessionSnapshot snapshot;
    snapshot.lease = {QStringLiteral("inst-1"), QStringLiteral("lease-1"), 3, true};

    client.publishMixManSessionSnapshot(newMixManSettings(),
            QStringLiteral("session-1"),
            snapshot);
    pReply->Done();

    ASSERT_EQ(diagnosticsSpy.count(), 1);
    const auto diagnostic = qvariant_cast<RestLibraryRequestDiagnostic>(
            diagnosticsSpy.takeFirst().at(0));
    EXPECT_EQ(diagnostic.method, QStringLiteral("PUT"));
}

TEST(RestLibraryClientTest, UpdatesMixManSessionIntent) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy statusSpy(&client, &RestLibraryClient::mixManSessionWriteStatusUpdated);
    MockNetworkReply* pReply = network.ExpectPut(
            QStringLiteral("/api/v3/sessions/session-1/intent"),
            {},
            {QStringLiteral("\"instance_id\":\"inst-1\""),
                    QStringLiteral("\"status\":\"active\""),
                    QStringLiteral("\"policy_preset\":\"build_energy\""),
                    QStringLiteral("\"target_energy\":0.8"),
                    QStringLiteral("\"target_color\":\"#ff6600\"")},
            200,
            R"json({"session_id":"session-1","revision":1})json");

    mixxx::library::rest::RestLibrarySessionIntent intent;
    intent.instanceId = QStringLiteral("inst-1");
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

TEST(RestLibraryClientTest, ClearsMixManSessionIntentWithoutTargets) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy statusSpy(&client, &RestLibraryClient::mixManSessionWriteStatusUpdated);
    MockNetworkReply* pReply = network.ExpectPut(
            QStringLiteral("/api/v3/sessions/session-1/intent"),
            {},
            {QStringLiteral("\"instance_id\":\"inst-1\""),
                    QStringLiteral("\"status\":\"cleared\"")},
            200,
            R"json({"session_id":"session-1","revision":1,"status":"cleared"})json");

    mixxx::library::rest::RestLibrarySessionIntent intent;
    intent.instanceId = QStringLiteral("inst-1");
    intent.status = QStringLiteral("cleared");

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
}

TEST(RestLibraryClientTest, ReportsPutForFailedMixManSessionIntent) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy diagnosticsSpy(&client,
            &RestLibraryClient::requestDiagnosticUpdated);
    MockNetworkReply* pReply =
            network.ExpectPut(QStringLiteral("/api/v3/sessions/session-1/intent"),
                    {},
                    {QStringLiteral("\"instance_id\":\"inst-1\"")},
                    422,
                    R"json({"detail":"invalid intent"})json");
    mixxx::library::rest::RestLibrarySessionIntent intent;
    intent.instanceId = QStringLiteral("inst-1");

    client.updateMixManSessionIntent(newMixManSettings(),
            QStringLiteral("session-1"),
            intent);
    pReply->Done();

    ASSERT_EQ(diagnosticsSpy.count(), 1);
    const auto diagnostic = qvariant_cast<RestLibraryRequestDiagnostic>(
            diagnosticsSpy.takeFirst().at(0));
    EXPECT_EQ(diagnostic.method, QStringLiteral("PUT"));
}

TEST(RestLibraryClientTest, SendsMixManSessionHeartbeat) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy statusSpy(&client, &RestLibraryClient::mixManSessionWriteStatusUpdated);
    MockNetworkReply* pReply = network.ExpectPost(
            QStringLiteral("/api/v3/sessions/session-1/instances/inst-1/heartbeat"),
            {},
            {QStringLiteral("\"status\":\"active\"")},
            200,
            R"json({"session_id":"session-1","client_id":"client-1"})json");

    client.sendMixManSessionHeartbeat(
            newMixManSettings(),
            QStringLiteral("session-1"),
            QStringLiteral("inst-1"));
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
            QStringLiteral("/api/v3/sessions/session-1/state"),
            {{QStringLiteral("instance_id"), QStringLiteral("inst-1")}},
            200,
            R"json({
              "session": {"id": "session-1"},
              "authoritative": {"session_id": "session-1", "revision": 2}
            })json");

    client.fetchMixManSession(newMixManSettings(),
            QStringLiteral("session-1"),
            QStringLiteral("inst-1"));
    pReply->Done();

    ASSERT_EQ(fetchedSpy.count(), 1);
    const auto session = qvariant_cast<mixxx::library::rest::RestLibrarySession>(
            fetchedSpy.takeFirst().at(0));
    EXPECT_EQ(session.id, QStringLiteral("session-1"));
    EXPECT_EQ(session.authoritative.revision, 2);
}

TEST(RestLibraryClientTest, ReportsExpiredInstanceFromAuthoritativeSessionFetch) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy fetchedSpy(&client, &RestLibraryClient::mixManSessionFetched);
    QSignalSpy statusSpy(&client, &RestLibraryClient::mixManSessionWriteStatusUpdated);
    MockNetworkReply* pReply = network.ExpectGet(
            QStringLiteral("/api/v3/sessions/session-1/state"),
            {{QStringLiteral("instance_id"), QStringLiteral("inst-1")}},
            409,
            R"json({"detail":{"reason":"instance_expired"}})json");

    client.fetchMixManSession(newMixManSettings(),
            QStringLiteral("session-1"),
            QStringLiteral("inst-1"));
    pReply->Done();

    ASSERT_EQ(statusSpy.count(), 1);
    const auto status =
            qvariant_cast<mixxx::library::rest::RestLibrarySessionWriteStatus>(
                    statusSpy.takeFirst().at(0));
    EXPECT_FALSE(status.success);
    EXPECT_EQ(status.operation, QStringLiteral("session_fetch"));
    EXPECT_EQ(status.errorReason, QStringLiteral("instance_expired"));
    ASSERT_EQ(fetchedSpy.count(), 1);
    const auto session = qvariant_cast<mixxx::library::rest::RestLibrarySession>(
            fetchedSpy.takeFirst().at(0));
    EXPECT_TRUE(session.id.isEmpty());
}

TEST(RestLibraryClientTest, PublishesMixManSessionPlayback) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy fetchedSpy(&client, &RestLibraryClient::mixManSessionFetched);
    QSignalSpy statusSpy(&client, &RestLibraryClient::mixManSessionWriteStatusUpdated);
    MockNetworkReply* pReply = network.ExpectPost(
            QStringLiteral("/api/v3/sessions/session-1/playback"),
            {},
            {QStringLiteral("\"instance_id\":\"inst-1\""),
                    QStringLiteral("\"lease_id\":\"lease-1\""),
                    QStringLiteral("\"lease_generation\":3"),
                    QStringLiteral("\"current_track_id\":8"),
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
    playback.lease = {QStringLiteral("inst-1"), QStringLiteral("lease-1"), 3, true};
    playback.currentTrackId = QStringLiteral("8");
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

TEST(RestLibraryClientTest, ClaimsMixManPlaybackControlAsMixxx) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy statusSpy(&client, &RestLibraryClient::mixManSessionWriteStatusUpdated);
    MockNetworkReply* pReply = network.ExpectPost(
            QStringLiteral("/api/v3/sessions/session-1/playback-control/claim"),
            {},
            {QStringLiteral("\"instance_id\":\"inst-1\""),
                    QStringLiteral("\"ttl_seconds\":30")},
            200,
            R"json({"session_id":"session-1","playback_controller":{"instance_id":"inst-1","lease_id":"lease-1","generation":3,"active":true}})json");

    client.claimMixManPlaybackControl(
            newMixManSettings(),
            QStringLiteral("session-1"),
            QStringLiteral("inst-1"),
            30);
    pReply->Done();

    ASSERT_EQ(statusSpy.count(), 1);
    const auto status =
            qvariant_cast<mixxx::library::rest::RestLibrarySessionWriteStatus>(
                    statusSpy.takeFirst().at(0));
    EXPECT_TRUE(status.success);
    EXPECT_EQ(status.operation, QStringLiteral("session_playback_control_claim"));
}

TEST(RestLibraryClientTest, RenewsAndReleasesMixManPlaybackControl) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy statusSpy(&client, &RestLibraryClient::mixManSessionWriteStatusUpdated);
    MockNetworkReply* pRenewReply = network.ExpectPost(
            QStringLiteral("/api/v3/sessions/session-1/playback-control/renew"),
            {},
            {QStringLiteral("\"instance_id\":\"inst-1\""),
                    QStringLiteral("\"lease_id\":\"lease-1\""),
                    QStringLiteral("\"generation\":3")},
            200,
            R"json({"session_id":"session-1","playback_controller":{"active":true}})json");
    MockNetworkReply* pReleaseReply = network.ExpectPost(
            QStringLiteral("/api/v3/sessions/session-1/playback-control/release"),
            {},
            {QStringLiteral("\"instance_id\":\"inst-1\""),
                    QStringLiteral("\"lease_id\":\"lease-1\""),
                    QStringLiteral("\"generation\":3")},
            200,
            R"json({"session_id":"session-1","playback_controller":null})json");

    const mixxx::library::rest::RestLibraryPlaybackLease lease{
            QStringLiteral("inst-1"), QStringLiteral("lease-1"), 3, true};
    client.renewMixManPlaybackControl(
            newMixManSettings(), QStringLiteral("session-1"), lease);
    pRenewReply->Done();
    client.releaseMixManPlaybackControl(
            newMixManSettings(), QStringLiteral("session-1"), lease);
    pReleaseReply->Done();

    ASSERT_EQ(statusSpy.count(), 2);
    EXPECT_EQ(statusSpy.at(0).at(0)
                      .value<mixxx::library::rest::RestLibrarySessionWriteStatus>()
                      .operation,
            QStringLiteral("session_playback_control_renew"));
    EXPECT_EQ(statusSpy.at(1).at(0)
                      .value<mixxx::library::rest::RestLibrarySessionWriteStatus>()
                      .operation,
            QStringLiteral("session_playback_control_release"));
}

TEST(RestLibraryClientTest, SelectsMixManSessionCandidateAndReportsConflict) {
    MockNetworkAccessManager network;
    RestLibraryClient client(&network);
    QSignalSpy statusSpy(&client, &RestLibraryClient::mixManSessionWriteStatusUpdated);
    MockNetworkReply* pReply = network.ExpectPost(
            QStringLiteral("/api/v3/sessions/session-1/candidates/9/select"),
            {},
            {QStringLiteral("\"instance_id\":\"inst-1\""),
                    QStringLiteral("\"lease_id\":\"lease-1\""),
                    QStringLiteral("\"lease_generation\":3"),
                    QStringLiteral("\"selection_origin\":\"authoritative_candidate\""),
                    QStringLiteral("\"allow_external_candidate\":false")},
            409,
            R"json({"detail":{"reason":"candidate_not_authoritative"}})json");

    client.selectMixManSessionCandidate(
            newMixManSettings(),
            QStringLiteral("session-1"),
            QStringLiteral("9"),
            {QStringLiteral("inst-1"), QStringLiteral("lease-1"), 3, true});
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
            QStringLiteral("/api/v3/sessions/session-1/actions"),
            {},
            {QStringLiteral("\"instance_id\":\"inst-1\""),
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
            QStringLiteral("inst-1"));
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
