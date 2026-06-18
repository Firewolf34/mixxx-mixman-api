#include "library/rest/restlibrarycachemanager.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QRegularExpression>

#include "moc_restlibrarycachemanager.cpp"
#include "util/logger.h"

namespace mixxx::library::rest {

namespace {

constexpr int kDownloadTimeoutMillis = 30000;
constexpr qsizetype kCacheHashLength = 24;
constexpr qint64 kBytesPerMegabyte = 1024 * 1024;

const Logger kLogger("RestLibraryCacheManager");

bool isSuccessStatus(int statusCode) {
    return statusCode >= 200 && statusCode < 300;
}

int statusCodeFromReply(const QNetworkReply& reply) {
    return reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
}

QString responseSnippet(const QByteArray& body) {
    constexpr qsizetype kMaxLoggedResponseBytes = 500;
    QString snippet = QString::fromUtf8(body.left(kMaxLoggedResponseBytes)).trimmed();
    snippet.replace(QChar('\n'), QChar(' '));
    snippet.replace(QChar('\r'), QChar(' '));
    return snippet;
}

} // namespace

RestLibraryCacheManager::RestLibraryCacheManager(
        QNetworkAccessManager* pNetworkAccessManager,
        QObject* parent)
        : QObject(parent),
          m_pNetworkAccessManager(pNetworkAccessManager) {
    qRegisterMetaType<RestLibraryCacheResult>(
            "mixxx::library::rest::RestLibraryCacheResult");
}

RestLibraryCacheManager::~RestLibraryCacheManager() {
    abortAll();
}

void RestLibraryCacheManager::reconcileTracks(
        const QList<RestLibraryTrack>& tracks,
        const RestLibrarySettings& settings) {
    m_settings = settings;
    if (!m_settings.hasAudioDownloadConfigured()) {
        return;
    }

    pruneExpiredCachedFiles(tracks);
    pruneCacheSize();

    for (const RestLibraryTrack& track : tracks) {
        if (track.remoteId.isEmpty()) {
            continue;
        }
        const QString cachedFilePath = existingCachedFilePath(track.remoteId);
        if (!cachedFilePath.isEmpty()) {
            emitState(track.remoteId, RestLibraryCacheState::Ready, cachedFilePath);
        }
    }
}

void RestLibraryCacheManager::cacheTracks(
        const QList<RestLibraryTrack>& tracks,
        const RestLibrarySettings& settings) {
    m_settings = settings;
    if (!m_settings.hasAudioDownloadConfigured() || !m_pNetworkAccessManager) {
        return;
    }

    QDir cacheDir(m_settings.cacheDirectoryPath);
    if (!cacheDir.exists() && !cacheDir.mkpath(QStringLiteral("."))) {
        for (const RestLibraryTrack& track : tracks) {
            if (!track.remoteId.isEmpty()) {
                emitState(
                        track.remoteId,
                        RestLibraryCacheState::Failed,
                        {},
                        tr("Cache directory could not be created."));
            }
        }
        return;
    }

    pruneExpiredCachedFiles(tracks);
    pruneCacheSize();

    for (const RestLibraryTrack& track : tracks) {
        if (track.remoteId.isEmpty() ||
                m_knownPendingRemoteIds.contains(track.remoteId) ||
                !existingCachedFilePath(track.remoteId).isEmpty()) {
            continue;
        }
        m_downloadQueue.enqueue(track);
        m_knownPendingRemoteIds.insert(track.remoteId, true);
        emitState(track.remoteId, RestLibraryCacheState::Missing);
    }
    startNextDownloads();
}

void RestLibraryCacheManager::abortAll() {
    m_downloadQueue.clear();
    m_knownPendingRemoteIds.clear();
    for (ActiveDownload& download : m_activeDownloads) {
        if (download.reply) {
            disconnect(download.reply, nullptr, this, nullptr);
            download.reply->abort();
            download.reply->deleteLater();
        }
        cleanupActiveDownload(&download);
        QFile::remove(download.tempFilePath);
    }
    m_activeDownloads.clear();
}

QString RestLibraryCacheManager::cacheFileStemForTesting(const QString& remoteId) {
    return cacheFileStem(remoteId);
}

QString RestLibraryCacheManager::extensionFromContentTypeForTesting(
        const QByteArray& contentType) {
    return extensionFromContentType(contentType);
}

QString RestLibraryCacheManager::existingCachedFilePathForTesting(
        const QString& cacheDirectoryPath,
        const QString& remoteId) {
    QDir cacheDir(cacheDirectoryPath);
    const QString stem = cacheFileStem(remoteId);
    const QStringList matches = cacheDir.entryList(
            QStringList{stem + QStringLiteral(".*")},
            QDir::Files,
            QDir::Name);
    for (const QString& match : matches) {
        if (!match.endsWith(QStringLiteral(".download"))) {
            return QDir::fromNativeSeparators(cacheDir.filePath(match));
        }
    }
    return {};
}

void RestLibraryCacheManager::slotReadyRead() {
    auto* pReply = qobject_cast<QNetworkReply*>(sender());
    ActiveDownload* pDownload = activeDownloadForReply(pReply);
    if (!pDownload || !pDownload->pFile) {
        return;
    }
    const QByteArray bytes = pReply->readAll();
    if (bytes.isEmpty()) {
        return;
    }
    const qint64 written = pDownload->pFile->write(bytes);
    if (written > 0) {
        pDownload->bytesWritten += written;
    }
}

void RestLibraryCacheManager::slotDownloadFinished() {
    auto* pReply = qobject_cast<QNetworkReply*>(sender());
    finishDownload(pReply);
}

QNetworkRequest RestLibraryCacheManager::newDownloadRequest(
        const RestLibraryTrack& track) const {
    QNetworkRequest request(urlWithPathTemplate(
            m_settings,
            m_settings.audioDownloadPathTemplate,
            track.remoteId));
    request.setAttribute(
            QNetworkRequest::RedirectPolicyAttribute,
            QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kDownloadTimeoutMillis);
    if (!m_settings.bearerToken.isEmpty()) {
        request.setRawHeader(
                "Authorization",
                QByteArray("Bearer ") + m_settings.bearerToken.toUtf8());
    }
    kLogger.info()
            << "REST library audio download request"
            << request.url().toString(QUrl::RemoveUserInfo);
    return request;
}

void RestLibraryCacheManager::startNextDownloads() {
    while (!m_downloadQueue.isEmpty() &&
            m_activeDownloads.size() < m_settings.maxConcurrentDownloads) {
        startDownload(m_downloadQueue.dequeue());
    }
}

void RestLibraryCacheManager::startDownload(const RestLibraryTrack& track) {
    QDir cacheDir(m_settings.cacheDirectoryPath);
    const QString tempFilePath = cacheDir.filePath(
            cacheFileStem(track.remoteId) + QStringLiteral(".download"));
    QFile::remove(tempFilePath);

    auto* pFile = new QFile(tempFilePath, this);
    if (!pFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        pFile->deleteLater();
        m_knownPendingRemoteIds.remove(track.remoteId);
        emitState(
                track.remoteId,
                RestLibraryCacheState::Failed,
                {},
                tr("Cache file could not be opened."));
        startNextDownloads();
        return;
    }

    QNetworkReply* pReply = m_pNetworkAccessManager->get(newDownloadRequest(track));
    pReply->setParent(this);
    m_activeDownloads.push_back(ActiveDownload{
            track,
            QPointer<QNetworkReply>(pReply),
            pFile,
            tempFilePath,
            0});
    emitState(track.remoteId, RestLibraryCacheState::Downloading);

    connect(pReply, &QNetworkReply::readyRead, this, &RestLibraryCacheManager::slotReadyRead);
    connect(pReply, &QNetworkReply::finished, this, &RestLibraryCacheManager::slotDownloadFinished);
}

void RestLibraryCacheManager::finishDownload(QNetworkReply* pReply) {
    ActiveDownload* pDownload = activeDownloadForReply(pReply);
    if (!pDownload) {
        return;
    }

    slotReadyRead();
    if (pDownload->pFile) {
        pDownload->pFile->close();
    }
    if (pReply) {
        pReply->deleteLater();
    }

    const QString remoteId = pDownload->track.remoteId;
    const int statusCode = pReply ? statusCodeFromReply(*pReply) : 0;
    const QByteArray remainingBody = pReply ? pReply->readAll() : QByteArray();
    const bool success = pReply &&
            pReply->error() == QNetworkReply::NoError &&
            isSuccessStatus(statusCode) &&
            pDownload->bytesWritten > 0;

    if (!success) {
        if (pReply) {
            kLogger.warning()
                    << "REST library audio download failed"
                    << pReply->request().url().toString(QUrl::RemoveUserInfo)
                    << "status" << statusCode
                    << "network error" << pReply->error()
                    << pReply->errorString()
                    << "bytes written" << pDownload->bytesWritten
                    << "body" << responseSnippet(remainingBody);
        } else {
            kLogger.warning() << "REST library audio download failed without a reply";
        }
        QFile::remove(pDownload->tempFilePath);
        emitState(
                remoteId,
                RestLibraryCacheState::Failed,
                {},
                tr("Audio download failed."));
        cleanupActiveDownload(pDownload);
        removeActiveDownload(remoteId);
        m_knownPendingRemoteIds.remove(remoteId);
        startNextDownloads();
        return;
    }

    const QString finalFilePath = finalCachedFilePath(pDownload->track, *pReply);
    if (finalFilePath.isEmpty()) {
        QFile::remove(pDownload->tempFilePath);
        emitState(
                remoteId,
                RestLibraryCacheState::Failed,
                {},
                tr("Audio file type could not be determined."));
        cleanupActiveDownload(pDownload);
        removeActiveDownload(remoteId);
        m_knownPendingRemoteIds.remove(remoteId);
        startNextDownloads();
        return;
    }

    QFile::remove(finalFilePath);
    if (!QFile::rename(pDownload->tempFilePath, finalFilePath)) {
        QFile::remove(pDownload->tempFilePath);
        emitState(
                remoteId,
                RestLibraryCacheState::Failed,
                {},
                tr("Cached audio file could not be finalized."));
        cleanupActiveDownload(pDownload);
        removeActiveDownload(remoteId);
        m_knownPendingRemoteIds.remove(remoteId);
        startNextDownloads();
        return;
    }

    if (QFileInfo(finalFilePath).size() >
            static_cast<qint64>(m_settings.cacheMaxMegabytes) * kBytesPerMegabyte) {
        QFile::remove(finalFilePath);
        emitState(
                remoteId,
                RestLibraryCacheState::Failed,
                {},
                tr("Cached audio file exceeds the cache size limit."));
        cleanupActiveDownload(pDownload);
        removeActiveDownload(remoteId);
        m_knownPendingRemoteIds.remove(remoteId);
        startNextDownloads();
        return;
    }

    pruneCacheSize(finalFilePath);
    emitState(remoteId, RestLibraryCacheState::Ready, finalFilePath);
    cleanupActiveDownload(pDownload);
    removeActiveDownload(remoteId);
    m_knownPendingRemoteIds.remove(remoteId);
    startNextDownloads();
}

RestLibraryCacheManager::ActiveDownload* RestLibraryCacheManager::activeDownloadForReply(
        QNetworkReply* pReply) {
    if (!pReply) {
        return nullptr;
    }
    for (ActiveDownload& download : m_activeDownloads) {
        if (download.reply == pReply) {
            return &download;
        }
    }
    return nullptr;
}

void RestLibraryCacheManager::pruneExpiredCachedFiles(const QList<RestLibraryTrack>& tracks) {
    const QDateTime cutoff =
            QDateTime::currentDateTimeUtc().addDays(-m_settings.cacheMaxAgeDays);
    QHash<QString, QString> remoteIdByCacheStem;
    for (const RestLibraryTrack& track : tracks) {
        if (!track.remoteId.isEmpty()) {
            remoteIdByCacheStem.insert(cacheFileStem(track.remoteId), track.remoteId);
        }
    }

    for (const QFileInfo& fileInfo : cachedFileInfos()) {
        if (fileInfo.lastModified().toUTC() >= cutoff) {
            continue;
        }
        const QString filePath = QDir::fromNativeSeparators(fileInfo.absoluteFilePath());
        QFile::remove(filePath);
        const QString remoteId = remoteIdByCacheStem.value(fileInfo.completeBaseName());
        if (!remoteId.isEmpty()) {
            emitState(remoteId, RestLibraryCacheState::Stale);
        }
    }
}

void RestLibraryCacheManager::pruneCacheSize(const QString& preservedFilePath) {
    QList<QFileInfo> fileInfos = cachedFileInfos();
    qint64 totalBytes = 0;
    for (const QFileInfo& fileInfo : fileInfos) {
        totalBytes += fileInfo.size();
    }

    const qint64 maxBytes =
            static_cast<qint64>(m_settings.cacheMaxMegabytes) * kBytesPerMegabyte;
    if (totalBytes <= maxBytes) {
        return;
    }

    const QString normalizedPreservedFilePath =
            QDir::fromNativeSeparators(QFileInfo(preservedFilePath).absoluteFilePath());
    std::sort(fileInfos.begin(), fileInfos.end(), [](const QFileInfo& lhs, const QFileInfo& rhs) {
        return lhs.lastModified() < rhs.lastModified();
    });

    for (const QFileInfo& fileInfo : std::as_const(fileInfos)) {
        const QString filePath = QDir::fromNativeSeparators(fileInfo.absoluteFilePath());
        if (filePath == normalizedPreservedFilePath) {
            continue;
        }
        if (QFile::remove(filePath)) {
            totalBytes -= fileInfo.size();
        }
        if (totalBytes <= maxBytes) {
            return;
        }
    }
}

QList<QFileInfo> RestLibraryCacheManager::cachedFileInfos() const {
    const QDir cacheDir(m_settings.cacheDirectoryPath);
    const QFileInfoList entries = cacheDir.entryInfoList(
            QDir::Files | QDir::NoDotAndDotDot,
            QDir::Name);
    QList<QFileInfo> result;
    result.reserve(entries.size());
    for (const QFileInfo& entry : entries) {
        if (isCacheFileName(entry.fileName())) {
            result.append(entry);
        }
    }
    return result;
}

QString RestLibraryCacheManager::existingCachedFilePath(const QString& remoteId) const {
    return existingCachedFilePathForTesting(m_settings.cacheDirectoryPath, remoteId);
}

QString RestLibraryCacheManager::finalCachedFilePath(
        const RestLibraryTrack& track,
        const QNetworkReply& reply) const {
    QString extension = normalizedExtension(track.audioFileExtension);
    if (extension.isEmpty()) {
        extension = extensionFromContentDisposition(
                reply.rawHeader(QByteArrayLiteral("Content-Disposition")));
    }
    if (extension.isEmpty()) {
        extension = normalizedExtension(QFileInfo(reply.url().path()).suffix());
    }
    if (extension.isEmpty()) {
        extension = extensionFromContentType(
                reply.header(QNetworkRequest::ContentTypeHeader).toByteArray());
    }
    if (extension.isEmpty()) {
        return {};
    }
    return QDir::fromNativeSeparators(QDir(m_settings.cacheDirectoryPath)
                                              .filePath(cacheFileStem(track.remoteId) +
                                                      QStringLiteral(".") +
                                                      extension));
}

void RestLibraryCacheManager::emitState(
        const QString& remoteId,
        RestLibraryCacheState cacheState,
        const QString& cachedFilePath,
        const QString& errorText) {
    emit trackCacheStateChanged(RestLibraryCacheResult{
            remoteId,
            cacheState,
            QDir::fromNativeSeparators(cachedFilePath),
            errorText});
}

void RestLibraryCacheManager::cleanupActiveDownload(ActiveDownload* pDownload) {
    if (!pDownload || !pDownload->pFile) {
        return;
    }
    pDownload->pFile->close();
    pDownload->pFile->deleteLater();
    pDownload->pFile = nullptr;
}

void RestLibraryCacheManager::removeActiveDownload(const QString& remoteId) {
    for (auto it = m_activeDownloads.begin(); it != m_activeDownloads.end(); ++it) {
        if (it->track.remoteId == remoteId) {
            m_activeDownloads.erase(it);
            return;
        }
    }
}

QString RestLibraryCacheManager::cacheFileStem(const QString& remoteId) {
    const QByteArray hash = QCryptographicHash::hash(
            remoteId.toUtf8(),
            QCryptographicHash::Sha256).toHex();
    return QString::fromLatin1(hash.left(kCacheHashLength));
}

bool RestLibraryCacheManager::isCacheFileName(const QString& fileName) {
    static const QRegularExpression cacheFileRegex(
            QStringLiteral("^[0-9a-f]{%1}\\.[a-z0-9]{1,10}$").arg(kCacheHashLength));
    return cacheFileRegex.match(fileName).hasMatch();
}

QString RestLibraryCacheManager::normalizedExtension(QString extension) {
    extension = extension.trimmed();
    if (extension.startsWith(QLatin1Char('.'))) {
        extension.remove(0, 1);
    }
    extension = extension.toLower();
    static const QRegularExpression safeExtension(
            QStringLiteral("^[a-z0-9][a-z0-9]{0,9}$"));
    if (!safeExtension.match(extension).hasMatch()) {
        return {};
    }
    return extension;
}

QString RestLibraryCacheManager::extensionFromContentDisposition(
        const QByteArray& contentDisposition) {
    const QString header = QString::fromLatin1(contentDisposition);
    static const QRegularExpression filenameRegex(
            QStringLiteral("filename\\*?=(?:UTF-8''|\\\")?([^\\\";]+)"),
            QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = filenameRegex.match(header);
    if (!match.hasMatch()) {
        return {};
    }
    return normalizedExtension(QFileInfo(match.captured(1).trimmed()).suffix());
}

QString RestLibraryCacheManager::extensionFromContentType(const QByteArray& contentType) {
    const QString type = QString::fromLatin1(contentType).toLower();
    if (type.contains(QStringLiteral("mpeg")) || type.contains(QStringLiteral("mp3"))) {
        return QStringLiteral("mp3");
    }
    if (type.contains(QStringLiteral("mp4")) || type.contains(QStringLiteral("aac"))) {
        return QStringLiteral("m4a");
    }
    if (type.contains(QStringLiteral("wav"))) {
        return QStringLiteral("wav");
    }
    if (type.contains(QStringLiteral("flac"))) {
        return QStringLiteral("flac");
    }
    if (type.contains(QStringLiteral("ogg")) || type.contains(QStringLiteral("opus"))) {
        return QStringLiteral("ogg");
    }
    return {};
}

QUrl RestLibraryCacheManager::urlWithPathTemplate(
        const RestLibrarySettings& settings,
        const QString& pathTemplate,
        const QString& remoteId) {
    QString path = pathTemplate;
    path.replace(QStringLiteral("%1"), QString::fromUtf8(QUrl::toPercentEncoding(remoteId)));

    const QUrl templateUrl(path);
    if (templateUrl.isValid() && !templateUrl.isRelative()) {
        return templateUrl;
    }

    QUrl url = settings.baseUrl;
    url.setPath(path.startsWith(QLatin1Char('/')) ? path : QStringLiteral("/") + path);
    return url;
}

} // namespace mixxx::library::rest
