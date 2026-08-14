#include <gtest/gtest.h>

#include <algorithm>

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
using mixxx::library::rest::RestLibraryCacheRequestOwner;
using mixxx::library::rest::RestLibraryCacheResult;
using mixxx::library::rest::RestLibraryCacheState;
using mixxx::library::rest::RestLibraryRequestDiagnostic;
using mixxx::library::rest::RestLibrarySettings;
using mixxx::library::rest::RestLibraryTrack;

class PartialWriteFile final : public QFile {
  public:
    using QFile::QFile;

  protected:
    qint64 writeData(const char* data, qint64 len) override {
        return QFile::writeData(data, std::max<qint64>(1, len / 2));
    }
};

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
            RestLibraryCacheManager::cacheFileStemForTesting(
                    QUrl(QStringLiteral("http://example.invalid")), remoteId) +
            QStringLiteral(".mp3"));
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
            QUrl(QStringLiteral("https://mixman.invalid")),
            QStringLiteral("remote-track-1"));
    const QString second = RestLibraryCacheManager::cacheFileStemForTesting(
            QUrl(QStringLiteral("https://mixman.invalid/")),
            QStringLiteral("remote-track-1"));
    const QString other = RestLibraryCacheManager::cacheFileStemForTesting(
            QUrl(QStringLiteral("https://mixman.invalid")),
            QStringLiteral("remote-track-2"));
    const QString otherServer = RestLibraryCacheManager::cacheFileStemForTesting(
            QUrl(QStringLiteral("https://other.invalid")),
            QStringLiteral("remote-track-1"));

    EXPECT_EQ(first, second);
    EXPECT_NE(first, other);
    EXPECT_NE(first, otherServer);
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
                                                       QUrl(QStringLiteral("http://example.invalid")),
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

TEST(RestLibraryCacheManagerTest, IgnoresAmbiguousLegacyCacheFile) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString remoteId = QStringLiteral("42");
    QFile legacyFile(QDir(tempDir.path())
                             .filePath(RestLibraryCacheManager::cacheFileStemForTesting(
                                               remoteId) +
                                     QStringLiteral(".mp3")));
    ASSERT_TRUE(legacyFile.open(QIODevice::WriteOnly));
    legacyFile.write("legacy-audio");
    legacyFile.close();

    RestLibraryCacheManager manager(nullptr);
    QSignalSpy spy(&manager, &RestLibraryCacheManager::trackCacheStateChanged);

    manager.reconcileTracks({newTrack(remoteId)}, newSettings(tempDir.path()));

    EXPECT_EQ(spy.count(), 0);
    EXPECT_TRUE(QFile::exists(legacyFile.fileName()));
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
    pReply->Done(true);

    ASSERT_GE(spy.count(), 3);
    const RestLibraryCacheResult result = lastResult(spy);
    EXPECT_EQ(result.remoteId, QStringLiteral("42"));
    EXPECT_EQ(result.cacheState, RestLibraryCacheState::Ready);
    EXPECT_TRUE(QFile::exists(result.cachedFilePath));
    EXPECT_TRUE(result.cachedFilePath.endsWith(QStringLiteral(".mp3")));
    EXPECT_EQ(result.serverIdentity, QStringLiteral("http://example.invalid"));
}

TEST(RestLibraryCacheManagerTest, ExplicitLoadPromotesQueuedRecommendation) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    MockNetworkAccessManager network;
    RestLibraryCacheManager manager(&network);
    RestLibrarySettings settings = newSettings(tempDir.path());
    settings.maxConcurrentDownloads = 1;
    MockNetworkReply* pFirst = network.ExpectGet(
            QStringLiteral("/configured-audio/1"),
            {},
            200,
            QByteArrayLiteral("first"));
    MockNetworkReply* pSecond = network.ExpectGet(
            QStringLiteral("/configured-audio/2"),
            {},
            200,
            QByteArrayLiteral("second"));
    MockNetworkReply* pExplicit = network.ExpectGet(
            QStringLiteral("/configured-audio/3"),
            {},
            200,
            QByteArrayLiteral("explicit"));

    manager.cacheTracks(
            {newTrack(QStringLiteral("1")),
                    newTrack(QStringLiteral("2")),
                    newTrack(QStringLiteral("3"))},
            settings,
            RestLibraryCacheRequestOwner::RecommendationPrefetch);
    manager.cacheTracks(
            {newTrack(QStringLiteral("3"))},
            settings,
            RestLibraryCacheRequestOwner::BrowserLoad);

    EXPECT_TRUE(pExplicit->request().url().isEmpty());
    pFirst->Done(true);
    EXPECT_FALSE(pExplicit->request().url().isEmpty());
    EXPECT_TRUE(pSecond->request().url().isEmpty());
    pExplicit->Done(true);
    EXPECT_FALSE(pSecond->request().url().isEmpty());
    pSecond->Done(true);
}

TEST(RestLibraryCacheManagerTest, CancellingRecommendationKeepsSharedBrowserLoad) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    MockNetworkAccessManager network;
    RestLibraryCacheManager manager(&network);
    RestLibrarySettings settings = newSettings(tempDir.path());
    settings.maxConcurrentDownloads = 1;
    MockNetworkReply* pObsolete = network.ExpectGet(
            QStringLiteral("/configured-audio/1"),
            {},
            200,
            QByteArrayLiteral("obsolete"));
    MockNetworkReply* pShared = network.ExpectGet(
            QStringLiteral("/configured-audio/2"),
            {},
            200,
            QByteArrayLiteral("shared"));

    manager.cacheTracks(
            {newTrack(QStringLiteral("1")), newTrack(QStringLiteral("2"))},
            settings,
            RestLibraryCacheRequestOwner::RecommendationPrefetch);
    manager.cacheTracks(
            {newTrack(QStringLiteral("2"))},
            settings,
            RestLibraryCacheRequestOwner::BrowserLoad);
    manager.cancelRequests(
            RestLibraryCacheRequestOwner::RecommendationPrefetch);

    EXPECT_TRUE(pObsolete->WasAborted());
    EXPECT_FALSE(pShared->request().url().isEmpty());
    pShared->Done(true);
    EXPECT_TRUE(QFile::exists(
            cacheFilePath(tempDir.path(), QStringLiteral("2"))));
}

TEST(RestLibraryCacheManagerTest, PriorityPreservesAutoDJFifoOrder) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    MockNetworkAccessManager network;
    RestLibraryCacheManager manager(&network);
    RestLibrarySettings settings = newSettings(tempDir.path());
    settings.maxConcurrentDownloads = 1;
    MockNetworkReply* pPrefetch = network.ExpectGet(
            QStringLiteral("/configured-audio/1"),
            {},
            200,
            QByteArrayLiteral("prefetch"));
    MockNetworkReply* pAutoDJFirst = network.ExpectGet(
            QStringLiteral("/configured-audio/2"),
            {},
            200,
            QByteArrayLiteral("autodj-first"));
    MockNetworkReply* pAutoDJSecond = network.ExpectGet(
            QStringLiteral("/configured-audio/3"),
            {},
            200,
            QByteArrayLiteral("autodj-second"));
    MockNetworkReply* pExplicit = network.ExpectGet(
            QStringLiteral("/configured-audio/4"),
            {},
            200,
            QByteArrayLiteral("explicit"));

    manager.cacheTracks(
            {newTrack(QStringLiteral("1"))},
            settings,
            RestLibraryCacheRequestOwner::RecommendationPrefetch);
    manager.cacheTracks(
            {newTrack(QStringLiteral("2")), newTrack(QStringLiteral("3"))},
            settings,
            RestLibraryCacheRequestOwner::BrowserAutoDJ);
    manager.cacheTracks(
            {newTrack(QStringLiteral("4"))},
            settings,
            RestLibraryCacheRequestOwner::BrowserLoad);

    pPrefetch->Done(true);
    EXPECT_FALSE(pExplicit->request().url().isEmpty());
    EXPECT_TRUE(pAutoDJFirst->request().url().isEmpty());
    pExplicit->Done(true);
    EXPECT_FALSE(pAutoDJFirst->request().url().isEmpty());
    EXPECT_TRUE(pAutoDJSecond->request().url().isEmpty());
    pAutoDJFirst->Done(true);
    EXPECT_FALSE(pAutoDJSecond->request().url().isEmpty());
    pAutoDJSecond->Done(true);
}

TEST(RestLibraryCacheManagerTest, TerminalSignalMayAbortManagerReentrantly) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    MockNetworkAccessManager network;
    RestLibraryCacheManager manager(&network);
    MockNetworkReply* pReply = network.ExpectGet(
            QStringLiteral("/configured-audio/42"),
            {},
            200,
            QByteArrayLiteral("audio bytes"));
    QObject::connect(&manager,
            &RestLibraryCacheManager::trackCacheStateChanged,
            &manager,
            [&manager](const RestLibraryCacheResult& result) {
                if (result.cacheState == RestLibraryCacheState::Ready) {
                    manager.abortAll();
                }
            });

    manager.cacheTracks({newTrack(QStringLiteral("42"))}, newSettings(tempDir.path()));
    pReply->Done(true);

    EXPECT_TRUE(QFile::exists(cacheFilePath(tempDir.path(), QStringLiteral("42"))));
}

TEST(RestLibraryCacheManagerTest, EvictionSignalMayAbortManagerReentrantly) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    writeCacheFile(
            tempDir.path(),
            QStringLiteral("1"),
            700 * 1024,
            QDateTime::currentDateTimeUtc().addSecs(-20));

    MockNetworkAccessManager network;
    RestLibraryCacheManager manager(&network);
    MockNetworkReply* pReply = network.ExpectGet(
            QStringLiteral("/configured-audio/2"),
            {},
            200,
            QByteArray(700 * 1024, 'a'));
    QObject::connect(&manager,
            &RestLibraryCacheManager::trackCacheStateChanged,
            &manager,
            [&manager](const RestLibraryCacheResult& result) {
                if (result.cacheState == RestLibraryCacheState::Stale) {
                    manager.abortAll();
                }
            });
    RestLibrarySettings settings = newSettings(tempDir.path());
    settings.cacheMaxMegabytes = 1;

    manager.cacheTracks(
            {newTrack(QStringLiteral("1")), newTrack(QStringLiteral("2"))},
            settings);
    pReply->Done(true);

    EXPECT_FALSE(QFile::exists(cacheFilePath(tempDir.path(), QStringLiteral("1"))));
    EXPECT_TRUE(QFile::exists(cacheFilePath(tempDir.path(), QStringLiteral("2"))));
}

TEST(RestLibraryCacheManagerTest, AudioDownloadUsesSecureSameOriginBearerRequest) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    MockNetworkAccessManager network;
    RestLibraryCacheManager manager(&network);
    MockNetworkReply* pReply = network.ExpectGet(
            QStringLiteral("https://example.test/configured-audio/42"),
            {},
            200,
            QByteArrayLiteral("audio bytes"));
    RestLibrarySettings settings = newSettings(tempDir.path());
    settings.baseUrl = QUrl(QStringLiteral("https://example.test"));
    settings.bearerToken = QStringLiteral("secret-token");

    manager.cacheTracks({newTrack(QStringLiteral("42"))}, settings);

    EXPECT_EQ(pReply->request().rawHeader("Authorization"),
            QByteArrayLiteral("Bearer secret-token"));
    EXPECT_EQ(pReply->request().attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
            static_cast<int>(QNetworkRequest::SameOriginRedirectPolicy));
    pReply->Done(true);
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
    pReply->Done(true);
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

    manager.cacheTracks(
            {newTrack(QStringLiteral("1")), newTrack(QStringLiteral("2"))},
            settings);
    pReply->Done(true);

    const RestLibraryCacheResult result = lastResult(spy);
    EXPECT_EQ(result.cacheState, RestLibraryCacheState::Ready);
    EXPECT_TRUE(QFile::exists(result.cachedFilePath));
    EXPECT_FALSE(QFile::exists(cacheFilePath(tempDir.path(), QStringLiteral("1"))));
    bool evictionReported = false;
    for (const auto& arguments : spy) {
        const auto cacheResult =
                qvariant_cast<RestLibraryCacheResult>(arguments.at(0));
        if (cacheResult.remoteId == QStringLiteral("1") &&
                cacheResult.cacheState == RestLibraryCacheState::Stale) {
            evictionReported = true;
            break;
        }
    }
    EXPECT_TRUE(evictionReported);
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
    pReply->Done(true);

    ASSERT_GE(spy.count(), 3);
    EXPECT_TRUE(pReply->WasAborted());
    const RestLibraryCacheResult result = lastResult(spy);
    EXPECT_EQ(result.cacheState, RestLibraryCacheState::Failed);
    EXPECT_TRUE(QDir(tempDir.path()).entryList(QStringList{QStringLiteral("*.mp3")}).isEmpty());
}

TEST(RestLibraryCacheManagerTest, DeclaredOversizedDownloadIsRejectedBeforeWriting) {
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
            QByteArrayLiteral("must not be written"));
    RestLibrarySettings settings = newSettings(tempDir.path());
    settings.cacheMaxMegabytes = 1;

    manager.cacheTracks({newTrack(QStringLiteral("42"))}, settings);
    pReply->SetHeader(QNetworkRequest::ContentLengthHeader, 2 * 1024 * 1024);
    pReply->EmitMetaDataChanged();

    EXPECT_TRUE(pReply->WasAborted());
    ASSERT_GE(spy.count(), 3);
    EXPECT_EQ(lastResult(spy).cacheState, RestLibraryCacheState::Failed);
    EXPECT_TRUE(QDir(tempDir.path())
                        .entryList(QDir::Files | QDir::NoDotAndDotDot)
                        .isEmpty());
}

TEST(RestLibraryCacheManagerTest, ChunkedOversizedDownloadIsRejectedAcrossReads) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    MockNetworkAccessManager network;
    RestLibraryCacheManager manager(&network);
    QSignalSpy spy(
            &manager,
            &RestLibraryCacheManager::trackCacheStateChanged);
    MockNetworkReply* pReply = network.ExpectGet(
            QStringLiteral("/configured-audio/42"), {}, 200, {});
    RestLibrarySettings settings = newSettings(tempDir.path());
    settings.cacheMaxMegabytes = 1;

    manager.cacheTracks({newTrack(QStringLiteral("42"))}, settings);
    pReply->SetData(QByteArray(700 * 1024, 'a'));
    pReply->EmitReadyRead();
    EXPECT_FALSE(pReply->WasAborted());
    pReply->SetData(QByteArray(700 * 1024, 'b'));
    pReply->EmitReadyRead();

    EXPECT_TRUE(pReply->WasAborted());
    ASSERT_GE(spy.count(), 3);
    EXPECT_EQ(lastResult(spy).cacheState, RestLibraryCacheState::Failed);
    EXPECT_TRUE(QDir(tempDir.path())
                        .entryList(QDir::Files | QDir::NoDotAndDotDot)
                        .isEmpty());
}

TEST(RestLibraryCacheManagerTest, PartialFileWriteFailsAndRemovesTemporaryFile) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    MockNetworkAccessManager network;
    RestLibraryCacheManager manager(
            &network,
            nullptr,
            [](const QString& filePath, QObject* parent) {
                return new PartialWriteFile(filePath, parent);
            });
    QSignalSpy spy(
            &manager,
            &RestLibraryCacheManager::trackCacheStateChanged);
    MockNetworkReply* pReply = network.ExpectGet(
            QStringLiteral("/configured-audio/42"),
            {},
            200,
            QByteArrayLiteral("audio bytes"));

    manager.cacheTracks(
            {newTrack(QStringLiteral("42"))}, newSettings(tempDir.path()));
    pReply->Done(true);

    EXPECT_TRUE(pReply->WasAborted());
    ASSERT_GE(spy.count(), 3);
    EXPECT_EQ(lastResult(spy).cacheState, RestLibraryCacheState::Failed);
    EXPECT_TRUE(QDir(tempDir.path())
                        .entryList(QDir::Files | QDir::NoDotAndDotDot)
                        .isEmpty());
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
    pReply->Done(true);

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
    pReply->Done(true);

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
    pReply->Done(true);

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
