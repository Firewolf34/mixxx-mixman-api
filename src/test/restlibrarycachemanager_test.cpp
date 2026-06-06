#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "library/rest/restlibrarycachemanager.h"
#include "test/mock_networkaccessmanager.h"

namespace {

using mixxx::library::rest::RestLibraryCacheManager;
using mixxx::library::rest::RestLibraryCacheResult;
using mixxx::library::rest::RestLibraryCacheState;
using mixxx::library::rest::RestLibrarySettings;
using mixxx::library::rest::RestLibraryTrack;

RestLibraryTrack newTrack(
        const QString& remoteId,
        const QString& extension = QStringLiteral("mp3")) {
    RestLibraryTrack track;
    track.remoteId = remoteId;
    track.audioFileExtension = extension;
    return track;
}

RestLibrarySettings newSettings(const QString& cachePath) {
    RestLibrarySettings settings;
    settings.enabled = true;
    settings.cacheEnabled = true;
    settings.baseUrl = QUrl(QStringLiteral("http://example.invalid"));
    settings.trackListPath = QStringLiteral("/configured-list");
    settings.audioDownloadPathTemplate = QStringLiteral("/configured-audio/%1");
    settings.cacheDirectoryPath = cachePath;
    settings.maxConcurrentDownloads = 2;
    return settings;
}

RestLibraryCacheResult lastResult(const QSignalSpy& spy) {
    return qvariant_cast<RestLibraryCacheResult>(spy.at(spy.count() - 1).at(0));
}

} // namespace

TEST(RestLibraryCacheManagerTest, BuildsStableCacheStemWithoutRemoteIdLeak) {
    const QString first = RestLibraryCacheManager::cacheFileStemForTesting(
            QStringLiteral("remote-track-1"));
    const QString second = RestLibraryCacheManager::cacheFileStemForTesting(
            QStringLiteral("remote-track-1"));
    const QString other = RestLibraryCacheManager::cacheFileStemForTesting(
            QStringLiteral("remote-track-2"));

    EXPECT_EQ(first, second);
    EXPECT_NE(first, other);
    EXPECT_FALSE(first.contains(QStringLiteral("remote")));
}

TEST(RestLibraryCacheManagerTest, MapsCommonAudioContentTypesToExtensions) {
    EXPECT_EQ(
            RestLibraryCacheManager::extensionFromContentTypeForTesting(
                    QByteArrayLiteral("audio/mpeg")),
            QStringLiteral("mp3"));
    EXPECT_EQ(
            RestLibraryCacheManager::extensionFromContentTypeForTesting(
                    QByteArrayLiteral("audio/flac")),
            QStringLiteral("flac"));
    EXPECT_EQ(
            RestLibraryCacheManager::extensionFromContentTypeForTesting(
                    QByteArrayLiteral("application/json")),
            QString());
}

TEST(RestLibraryCacheManagerTest, ReconcilesExistingCachedFile) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString remoteId = QStringLiteral("42");
    const QString filePath = QDir(tempDir.path())
                                     .filePath(RestLibraryCacheManager::cacheFileStemForTesting(
                                                       remoteId) +
                                             QStringLiteral(".mp3"));
    QFile file(filePath);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("audio");
    file.close();

    RestLibraryCacheManager manager(nullptr);
    QSignalSpy spy(
            &manager,
            &RestLibraryCacheManager::trackCacheStateChanged);

    manager.reconcileTracks({newTrack(remoteId)}, newSettings(tempDir.path()));

    ASSERT_EQ(spy.count(), 1);
    const auto result = qvariant_cast<RestLibraryCacheResult>(spy.takeFirst().at(0));
    EXPECT_EQ(result.remoteId, remoteId);
    EXPECT_EQ(result.cacheState, RestLibraryCacheState::Ready);
    EXPECT_EQ(result.cachedFilePath, QDir::fromNativeSeparators(filePath));
}

TEST(RestLibraryCacheManagerTest, DownloadsAudioToFinalCacheFile) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    MockNetworkAccessManager network;
    RestLibraryCacheManager manager(&network);
    QSignalSpy spy(
            &manager,
            &RestLibraryCacheManager::trackCacheStateChanged);
    MockNetworkReply* pReply = network.ExpectGet(
            QStringLiteral("/configured-audio/42"),
            {},
            200,
            QByteArrayLiteral("audio bytes"));

    manager.cacheTracks({newTrack(QStringLiteral("42"))}, newSettings(tempDir.path()));
    pReply->Done();

    ASSERT_GE(spy.count(), 3);
    const RestLibraryCacheResult result = lastResult(spy);
    EXPECT_EQ(result.remoteId, QStringLiteral("42"));
    EXPECT_EQ(result.cacheState, RestLibraryCacheState::Ready);
    EXPECT_TRUE(QFile::exists(result.cachedFilePath));
    EXPECT_TRUE(result.cachedFilePath.endsWith(QStringLiteral(".mp3")));
}

TEST(RestLibraryCacheManagerTest, ReportsFailedForHttpError) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    MockNetworkAccessManager network;
    RestLibraryCacheManager manager(&network);
    QSignalSpy spy(
            &manager,
            &RestLibraryCacheManager::trackCacheStateChanged);
    MockNetworkReply* pReply = network.ExpectGet(
            QStringLiteral("/configured-audio/42"),
            {},
            401,
            QByteArrayLiteral("nope"));

    manager.cacheTracks({newTrack(QStringLiteral("42"))}, newSettings(tempDir.path()));
    pReply->Done();

    ASSERT_GE(spy.count(), 3);
    const RestLibraryCacheResult result = lastResult(spy);
    EXPECT_EQ(result.cacheState, RestLibraryCacheState::Failed);
    EXPECT_FALSE(result.errorText.isEmpty());
}

TEST(RestLibraryCacheManagerTest, ReportsFailedForEmptyBody) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    MockNetworkAccessManager network;
    RestLibraryCacheManager manager(&network);
    QSignalSpy spy(
            &manager,
            &RestLibraryCacheManager::trackCacheStateChanged);
    MockNetworkReply* pReply = network.ExpectGet(
            QStringLiteral("/configured-audio/42"),
            {},
            200,
            QByteArray());

    manager.cacheTracks({newTrack(QStringLiteral("42"))}, newSettings(tempDir.path()));
    pReply->Done();

    ASSERT_GE(spy.count(), 3);
    EXPECT_EQ(lastResult(spy).cacheState, RestLibraryCacheState::Failed);
}

TEST(RestLibraryCacheManagerTest, ReportsFailedForUnknownAudioType) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    MockNetworkAccessManager network;
    RestLibraryCacheManager manager(&network);
    QSignalSpy spy(
            &manager,
            &RestLibraryCacheManager::trackCacheStateChanged);
    MockNetworkReply* pReply = network.ExpectGet(
            QStringLiteral("/configured-audio/42"),
            {},
            200,
            QByteArrayLiteral("audio bytes"));

    manager.cacheTracks(
            {newTrack(QStringLiteral("42"), QString())},
            newSettings(tempDir.path()));
    pReply->Done();

    ASSERT_GE(spy.count(), 3);
    EXPECT_EQ(lastResult(spy).cacheState, RestLibraryCacheState::Failed);
}

TEST(RestLibraryCacheManagerTest, AbortRemovesTemporaryFile) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    MockNetworkAccessManager network;
    RestLibraryCacheManager manager(&network);
    QSignalSpy spy(
            &manager,
            &RestLibraryCacheManager::trackCacheStateChanged);
    network.ExpectGet(
            QStringLiteral("/configured-audio/42"),
            {},
            200,
            QByteArrayLiteral("audio bytes"));

    manager.cacheTracks({newTrack(QStringLiteral("42"))}, newSettings(tempDir.path()));
    manager.abortAll();

    EXPECT_GE(spy.count(), 2);
    const QStringList downloads = QDir(tempDir.path()).entryList(
            QStringList{QStringLiteral("*.download")},
            QDir::Files);
    EXPECT_TRUE(downloads.isEmpty());
}
