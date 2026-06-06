#include <gtest/gtest.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

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
