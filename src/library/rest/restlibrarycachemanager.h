#pragma once

#include <functional>

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>

#include "library/rest/restlibrarymixman.h"
#include "library/rest/restlibrarysettings.h"
#include "library/rest/restlibrarytrack.h"

class QFile;
class QFileInfo;
class QNetworkAccessManager;
class QNetworkRequest;

namespace mixxx::library::rest {

enum class RestLibraryCacheRequestOwner {
    RecommendationPrefetch,
    BrowserAutoDJ,
    BrowserLoad,
};

struct RestLibraryCacheResult {
    QString remoteId;
    RestLibraryCacheState cacheState = RestLibraryCacheState::Missing;
    QString cachedFilePath;
    QString errorText;
    int statusCode = 0;
    int networkError = 0;
    QString cacheIdentity;
};

class RestLibraryCacheManager final : public QObject {
    Q_OBJECT

  public:
    using CacheFileFactory =
            std::function<QFile*(const QString& filePath, QObject* parent)>;

    explicit RestLibraryCacheManager(
            QNetworkAccessManager* pNetworkAccessManager,
            QObject* parent = nullptr,
            CacheFileFactory cacheFileFactory = {});
    ~RestLibraryCacheManager() override;

    void reconcileTracks(
            const QList<RestLibraryTrack>& tracks,
            const RestLibrarySettings& settings);
    void cacheTracks(
            const QList<RestLibraryTrack>& tracks,
            const RestLibrarySettings& settings,
            RestLibraryCacheRequestOwner owner =
                    RestLibraryCacheRequestOwner::RecommendationPrefetch);
    void cancelRequests(RestLibraryCacheRequestOwner owner);
    void abortAll();

    static QString serverIdentity(const RestLibrarySettings& settings);
    static QString cacheIdentity(const RestLibrarySettings& settings);

    static QString cacheFileStemForTesting(const QString& remoteId);
    static QString cacheFileStemForTesting(
            const QUrl& baseUrl,
            const QString& remoteId,
            const QString& bearerToken = {});
    static QString extensionFromContentTypeForTesting(const QByteArray& contentType);
    static QString existingCachedFilePathForTesting(
            const QString& cacheDirectoryPath,
            const QString& remoteId);

  signals:
    void trackCacheStateChanged(
            const mixxx::library::rest::RestLibraryCacheResult& result);
    void requestDiagnosticUpdated(
            const mixxx::library::rest::RestLibraryRequestDiagnostic& diagnostic);

  private slots:
    void slotReadyRead();
    void slotDownloadFinished();

  private:
    struct PendingDownload {
        RestLibraryTrack track;
        RestLibrarySettings settings;
        QString requestKey;
        QSet<RestLibraryCacheRequestOwner> owners;
    };

    struct ActiveDownload {
        RestLibraryTrack track;
        RestLibrarySettings settings;
        QString requestKey;
        QSet<RestLibraryCacheRequestOwner> owners;
        QPointer<QNetworkReply> reply;
        QFile* pFile = nullptr;
        QString tempFilePath;
        qint64 bytesWritten = 0;
        qint64 expectedBytes = 0;
        QByteArray responsePrefix;
        QString fileError;
        bool writeFailed = false;
        bool sizeLimitExceeded = false;
        bool quotaLimitExceeded = false;
    };

    QNetworkRequest newDownloadRequest(
            const RestLibraryTrack& track,
            const RestLibrarySettings& settings) const;
    void startNextDownloads();
    void startDownload(PendingDownload pendingDownload);
    void enqueuePendingDownload(PendingDownload pendingDownload);
    ActiveDownload* activeDownloadForKey(const QString& requestKey);
    void consumeReplyBytes(QNetworkReply* pReply);
    void finishDownload(QNetworkReply* pReply);
    ActiveDownload* activeDownloadForReply(QNetworkReply* pReply);
    qint64 reservedDownloadBytes(
            const RestLibrarySettings& settings,
            const ActiveDownload* pExcludedDownload) const;
    bool isActiveTempFilePath(const QString& filePath) const;
    void rememberTrackCacheStems(
            const QList<RestLibraryTrack>& tracks,
            const RestLibrarySettings& settings);
    void pruneExpiredCachedFiles(
            const QList<RestLibraryTrack>& tracks,
            const RestLibrarySettings& settings);
    void pruneCacheSize(
            const RestLibrarySettings& settings,
            const QString& preservedFilePath = {});
    QList<QFileInfo> cachedFileInfos(const RestLibrarySettings& settings) const;
    QString existingCachedFilePath(
            const RestLibrarySettings& settings,
            const QString& remoteId) const;
    QString finalCachedFilePath(
            const RestLibrarySettings& settings,
            const RestLibraryTrack& track,
            const QNetworkReply& reply) const;
    void emitState(
            const RestLibrarySettings& settings,
            const QString& remoteId,
            RestLibraryCacheState cacheState,
            const QString& cachedFilePath = {},
            const QString& errorText = {},
            int statusCode = 0,
            int networkError = 0);
    void cleanupActiveDownload(ActiveDownload* pDownload);
    void removeActiveDownload(const QString& requestKey);

    static QString legacyCacheFileStem(const QString& remoteId);
    static QString cacheFileStem(
            const RestLibrarySettings& settings,
            const QString& remoteId);
    static bool isCacheFileName(const QString& fileName);
    static QString normalizedExtension(QString extension);
    static QString extensionFromContentDisposition(const QByteArray& contentDisposition);
    static QString extensionFromContentType(const QByteArray& contentType);
    static QUrl urlWithPathTemplate(
            const RestLibrarySettings& settings,
            const QString& pathTemplate,
            const QString& remoteId);

    QPointer<QNetworkAccessManager> m_pNetworkAccessManager;
    CacheFileFactory m_cacheFileFactory;
    QList<PendingDownload> m_downloadQueue;
    QList<ActiveDownload> m_activeDownloads;
    QHash<QString, QString> m_remoteIdByCacheStem;
    QHash<QString, QString> m_cacheIdentityByCacheStem;
};

} // namespace mixxx::library::rest

Q_DECLARE_METATYPE(mixxx::library::rest::RestLibraryCacheResult)
