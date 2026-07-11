#pragma once

#include <QHash>
#include <QList>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QString>

#include "library/rest/restlibrarysettings.h"
#include "library/rest/restlibrarymixman.h"
#include "library/rest/restlibrarytrack.h"

class QFile;
class QFileInfo;
class QNetworkAccessManager;
class QNetworkRequest;

namespace mixxx::library::rest {

struct RestLibraryCacheResult {
    QString remoteId;
    RestLibraryCacheState cacheState = RestLibraryCacheState::Missing;
    QString cachedFilePath;
    QString errorText;
};

class RestLibraryCacheManager final : public QObject {
    Q_OBJECT

  public:
    explicit RestLibraryCacheManager(
            QNetworkAccessManager* pNetworkAccessManager,
            QObject* parent = nullptr);
    ~RestLibraryCacheManager() override;

    void reconcileTracks(
            const QList<RestLibraryTrack>& tracks,
            const RestLibrarySettings& settings);
    void cacheTracks(
            const QList<RestLibraryTrack>& tracks,
            const RestLibrarySettings& settings);
    void abortAll();

    static QString cacheFileStemForTesting(const QString& remoteId);
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
    struct ActiveDownload {
        RestLibraryTrack track;
        RestLibrarySettings settings;
        QPointer<QNetworkReply> reply;
        QFile* pFile = nullptr;
        QString tempFilePath;
        qint64 bytesWritten = 0;
    };

    QNetworkRequest newDownloadRequest(const RestLibraryTrack& track) const;
    void startNextDownloads();
    void startDownload(const RestLibraryTrack& track);
    void finishDownload(QNetworkReply* pReply);
    ActiveDownload* activeDownloadForReply(QNetworkReply* pReply);
    void pruneExpiredCachedFiles(const QList<RestLibraryTrack>& tracks);
    void pruneCacheSize(const QString& preservedFilePath = {});
    QList<QFileInfo> cachedFileInfos() const;
    QString existingCachedFilePath(const QString& remoteId) const;
    QString finalCachedFilePath(
            const RestLibrarySettings& settings,
            const RestLibraryTrack& track,
            const QNetworkReply& reply) const;
    void emitState(
            const QString& remoteId,
            RestLibraryCacheState cacheState,
            const QString& cachedFilePath = {},
            const QString& errorText = {});
    void cleanupActiveDownload(ActiveDownload* pDownload);
    void removeActiveDownload(const QString& remoteId);

    static QString cacheFileStem(const QString& remoteId);
    static bool isCacheFileName(const QString& fileName);
    static QString normalizedExtension(QString extension);
    static QString extensionFromContentDisposition(const QByteArray& contentDisposition);
    static QString extensionFromContentType(const QByteArray& contentType);
    static QUrl urlWithPathTemplate(
            const RestLibrarySettings& settings,
            const QString& pathTemplate,
            const QString& remoteId);

    QPointer<QNetworkAccessManager> m_pNetworkAccessManager;
    RestLibrarySettings m_settings;
    QQueue<RestLibraryTrack> m_downloadQueue;
    QList<ActiveDownload> m_activeDownloads;
    QHash<QString, bool> m_knownPendingRemoteIds;
};

} // namespace mixxx::library::rest

Q_DECLARE_METATYPE(mixxx::library::rest::RestLibraryCacheResult)
