#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QDateTime>

#include "library/rest/restlibrarycachemanager.h"
#include "test/mock_networkaccessmanager.h"

namespace {

using mixxx::library::rest::RestLibraryCacheManager;
using mixxx::library::rest::RestLibraryCacheResult;
using mixxx::library::rest::RestLibraryCacheState;
using mixxx::library::rest::RestLibraryRequestDiagnostic;
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

QString cacheFilePath(const QString& cachePath, const QString& remoteId) {
    return QDir(cachePath).filePath(
            RestLibraryCacheManager::cacheFileStemForTesting(remoteId) + QStringLiteral(".mp3"));
}

void writeCacheFile(
        const QString& cachePath,
        const QString& remoteId,
        qint64 size,
        const QDateTime& modified = QDateTime::currentDateTimeUtc()) {
    QFile file(cacheFilePath(cachePath, remoteId));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_TRUE(file.resize(size));
    ASSERT_TRUE(file.setFileTime(modified, QFileDevice::FileModificationTime));
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

TEST(RestLibraryCacheManagerTest, ExpiresOldCachedFileAndReportsStale) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString remoteId = QStringLiteral("42");
    writeCacheFile(
            tempDir.path(),
            remoteId,
            4,
            QDateTime::currentDateTimeUtc().addDays(-2));
    RestLibrarySettings settings = newSettings(tempDir.path());
    settings.cacheMaxAgeDays = 1;
    RestLibraryCacheManager manager(nullptr);
    QSignalSpy spy(
            &manager,
            &RestLibraryCacheManager::trackCacheStateChanged);

    manager.reconcileTracks({newTrack(remoteId)}, settings);

    ASSERT_EQ(spy.count(), 1);
    const auto result = qvariant_cast<RestLibraryCacheResult>(spy.takeFirst().at(0));
    EXPECT_EQ(result.remoteId, remoteId);
    EXPECT_EQ(result.cacheState, RestLibraryCacheState::Stale);
    EXPECT_FALSE(QFile::exists(cacheFilePath(tempDir.path(), remoteId)));
}

TEST(RestLibraryCacheManagerTest, PrunesOldestCachedFilesToFitSizeLimit) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    writeCacheFile(
            tempDir.path(),
            QStringLiteral("1"),
            700 * 1024,
            QDateTime::currentDateTimeUtc().addSecs(-20));
    writeCacheFile(
            tempDir.path(),
            QStringLiteral("2"),
            700 * 1024,
            QDateTime::currentDateTimeUtc().addSecs(-10));
    RestLibrarySettings settings = newSettings(tempDir.path());
    settings.cacheMaxMegabytes = 1;
    RestLibraryCacheManager manager(nullptr);

    manager.reconcileTracks({newTrack(QStringLiteral("2"))}, settings);

    EXPECT_FALSE(QFile::exists(cacheFilePath(tempDir.path(), QStringLiteral("1"))));
    EXPECT_TRUE(QFile::exists(cacheFilePath(tempDir.path(), QStringLiteral("2"))));
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

TEST(RestLibraryCacheManagerTest, DownloadPreservesBaseUrlPath) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    MockNetworkAccessManager network;
    RestLibraryCacheManager manager(&network);
    MockNetworkReply* pReply = network.ExpectGet(
            QStringLiteral("/api/configured-audio/42"),
            {},
            200,
            QByteArrayLiteral("audio bytes"));
    RestLibrarySettings settings = newSettings(tempDir.path());
    settings.baseUrl = QUrl(QStringLiteral("http://example.invalid/api"));

    manager.cacheTracks({newTrack(QStringLiteral("42"))}, settings);
    pReply->Done();
}

TEST(RestLibraryCacheManagerTest, DownloadPrunesOlderFilesButKeepsNewFile) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    writeCacheFile(
            tempDir.path(),
            QStringLiteral("1"),
            700 * 1024,
            QDateTime::currentDateTimeUtc().addSecs(-20));
    MockNetworkAccessManager network;
    RestLibraryCacheManager manager(&network);
    QSignalSpy spy(
            &manager,
            &RestLibraryCacheManager::trackCacheStateChanged);
    MockNetworkReply* pReply = network.ExpectGet(
            QStringLiteral("/configured-audio/2"),
            {},
            200,
            QByteArray(700 * 1024, 'a'));
    RestLibrarySettings settings = newSettings(tempDir.path());
    settings.cacheMaxMegabytes = 1;

    manager.cacheTracks({newTrack(QStringLiteral("2"))}, settings);
    pReply->Done();

    const RestLibraryCacheResult result = lastResult(spy);
    EXPECT_EQ(result.cacheState, RestLibraryCacheState::Ready);
    EXPECT_TRUE(QFile::exists(result.cachedFilePath));
    EXPECT_FALSE(QFile::exists(cacheFilePath(tempDir.path(), QStringLiteral("1"))));
}

TEST(RestLibraryCacheManagerTest, OversizedDownloadFailsAndIsNotFinalized) {
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
            QByteArray(2 * 1024 * 1024, 'a'));
    RestLibrarySettings settings = newSettings(tempDir.path());
    settings.cacheMaxMegabytes = 1;

    manager.cacheTracks({newTrack(QStringLiteral("42"))}, settings);
    pReply->Done();

    ASSERT_GE(spy.count(), 3);
    const RestLibraryCacheResult result = lastResult(spy);
    EXPECT_EQ(result.cacheState, RestLibraryCacheState::Failed);
    EXPECT_TRUE(QDir(tempDir.path()).entryList(QStringList{QStringLiteral("*.mp3")}).isEmpty());
}

TEST(RestLibraryCacheManagerTest, IgnoresTemporaryAndUnrelatedFilesDuringPruning) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    QFile unrelated(QDir(tempDir.path()).filePath(QStringLiteral("notes.txt")));
    ASSERT_TRUE(unrelated.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(unrelated.write("keep"), 4);
    unrelated.close();
    QFile download(QDir(tempDir.path()).filePath(
            RestLibraryCacheManager::cacheFileStemForTesting(QStringLiteral("1")) +
            QStringLiteral(".download")));
    ASSERT_TRUE(download.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(download.write("keep"), 4);
    download.close();
    RestLibrarySettings settings = newSettings(tempDir.path());
    settings.cacheMaxMegabytes = 1;
    RestLibraryCacheManager manager(nullptr);

    manager.reconcileTracks({}, settings);

    EXPECT_TRUE(QFile::exists(unrelated.fileName()));
    EXPECT_TRUE(QFile::exists(download.fileName()));
}

TEST(RestLibraryCacheManagerTest, ReportsFailedForHttpError) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    MockNetworkAccessManager network;
    RestLibraryCacheManager manager(&network);
    QSignalSpy spy(
            &manager,
            &RestLibraryCacheManager::trackCacheStateChanged);
    QSignalSpy diagnosticSpy(
            &manager,
            &RestLibraryCacheManager::requestDiagnosticUpdated);
    MockNetworkReply* pReply = network.ExpectGet(
            QStringLiteral("/configured-audio/42"),
            {},
            401,
            R"json({"detail":"bad token"})json");

    manager.cacheTracks({newTrack(QStringLiteral("42"))}, newSettings(tempDir.path()));
    pReply->Done();

    ASSERT_GE(spy.count(), 3);
    const RestLibraryCacheResult result = lastResult(spy);
    EXPECT_EQ(result.cacheState, RestLibraryCacheState::Failed);
    EXPECT_FALSE(result.errorText.isEmpty());
    EXPECT_EQ(result.statusCode, 401);
    EXPECT_EQ(result.networkError, static_cast<int>(QNetworkReply::NoError));
    ASSERT_EQ(diagnosticSpy.count(), 1);
    const auto diagnostic =
            qvariant_cast<RestLibraryRequestDiagnostic>(diagnosticSpy.takeFirst().at(0));
    EXPECT_EQ(diagnostic.statusCode, 401);
    EXPECT_FALSE(diagnostic.success);
    EXPECT_EQ(diagnostic.errorText, QStringLiteral("bad token"));
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
