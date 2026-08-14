#include "library/rest/restlibrarycachemanager.h"

#include <algorithm>
#include <utility>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringList>

#include "moc_restlibrarycachemanager.cpp"
#include "util/logger.h"

namespace mixxx::library::rest {

namespace {

constexpr int kDownloadTimeoutMillis = 30000;
constexpr qsizetype kCacheHashLength = 24;
constexpr qsizetype kMaxLoggedResponseBytes = 500;
constexpr qint64 kBytesPerMegabyte = 1024 * 1024;

const Logger kLogger("RestLibraryCacheManager");

bool isSuccessStatus(int statusCode) {
    return statusCode >= 200 && statusCode < 300;
}

int statusCodeFromReply(const QNetworkReply& reply) {
    return reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
}

QString responseSnippet(const QByteArray& body) {
    QString snippet = QString::fromUtf8(body.left(kMaxLoggedResponseBytes)).trimmed();
    snippet.replace(QChar('\n'), QChar(' '));
    snippet.replace(QChar('\r'), QChar(' '));
    return snippet;
}

QString diagnosticStringFromValue(const QJsonValue& value) {
    if (value.isString()) {
        return value.toString();
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble());
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        const QStringList keys{
                QStringLiteral("message"),
                QStringLiteral("reason"),
                QStringLiteral("code"),
                QStringLiteral("error")};
        QStringList parts;
        for (const QString& key : keys) {
            const QString text = diagnosticStringFromValue(object.value(key));
            if (!text.isEmpty()) {
                parts.append(text);
            }
        }
        return parts.join(QStringLiteral(": "));
    }
    return {};
}

QString errorTextFromResponse(const QByteArray& body) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }
    const QJsonObject object = document.object();
    const QStringList keys{
            QStringLiteral("detail"),
            QStringLiteral("error"),
            QStringLiteral("message")};
    for (const QString& key : keys) {
        const QString text = diagnosticStringFromValue(object.value(key));
        if (!text.isEmpty()) {
            return text;
        }
    }
    return {};
}

int requestPriority(RestLibraryCacheRequestOwner owner) {
    switch (owner) {
    case RestLibraryCacheRequestOwner::BrowserLoad:
        return 2;
    case RestLibraryCacheRequestOwner::BrowserAutoDJ:
        return 1;
    case RestLibraryCacheRequestOwner::RecommendationPrefetch:
        return 0;
    }
    DEBUG_ASSERT(false);
    return 0;
}

int requestPriority(const QSet<RestLibraryCacheRequestOwner>& owners) {
    int priority = 0;
    for (const auto owner : owners) {
        priority = std::max(priority, requestPriority(owner));
    }
    return priority;
}

} // namespace

RestLibraryCacheManager::RestLibraryCacheManager(
        QNetworkAccessManager* pNetworkAccessManager,
        QObject* parent,
        CacheFileFactory cacheFileFactory)
        : QObject(parent),
          m_pNetworkAccessManager(pNetworkAccessManager),
          m_cacheFileFactory(cacheFileFactory ? std::move(cacheFileFactory)
                                              : CacheFileFactory([](
                                                        const QString& filePath,
                                                        QObject* parent) {
                                                    return new QFile(filePath, parent);
                                                })) {
    qRegisterMetaType<RestLibraryCacheResult>(
            "mixxx::library::rest::RestLibraryCacheResult");
    qRegisterMetaType<RestLibraryRequestDiagnostic>(
            "mixxx::library::rest::RestLibraryRequestDiagnostic");
}

RestLibraryCacheManager::~RestLibraryCacheManager() {
    abortAll();
}

void RestLibraryCacheManager::reconcileTracks(
        const QList<RestLibraryTrack>& tracks,
        const RestLibrarySettings& settings) {
    if (!settings.hasAudioDownloadConfigured()) {
        return;
    }

    rememberTrackCacheStems(tracks, settings);
    pruneExpiredCachedFiles(tracks, settings);
    pruneCacheSize(settings);

    for (const RestLibraryTrack& track : tracks) {
        if (track.remoteId.isEmpty()) {
            continue;
        }
        const QString cachedFilePath = existingCachedFilePath(settings, track.remoteId);
        if (!cachedFilePath.isEmpty()) {
            emitState(settings,
                    track.remoteId,
                    RestLibraryCacheState::Ready,
                    cachedFilePath);
        }
    }
}

void RestLibraryCacheManager::cacheTracks(
        const QList<RestLibraryTrack>& tracks,
        const RestLibrarySettings& settings,
        RestLibraryCacheRequestOwner owner) {
    if (!settings.hasAudioDownloadConfigured() || !m_pNetworkAccessManager) {
        return;
    }

    rememberTrackCacheStems(tracks, settings);
    QDir cacheDir(settings.cacheDirectoryPath);
    if (!cacheDir.exists() && !cacheDir.mkpath(QStringLiteral("."))) {
        for (const RestLibraryTrack& track : tracks) {
            if (!track.remoteId.isEmpty()) {
                emitState(
                        settings,
                        track.remoteId,
                        RestLibraryCacheState::Failed,
                        {},
                        tr("Cache directory could not be created."));
            }
        }
        return;
    }

    pruneExpiredCachedFiles(tracks, settings);
    pruneCacheSize(settings);

    for (const RestLibraryTrack& track : tracks) {
        const QString requestKey = cacheFileStem(settings, track.remoteId);
        if (track.remoteId.isEmpty() ||
                !existingCachedFilePath(settings, track.remoteId).isEmpty()) {
            continue;
        }
        if (ActiveDownload* pActive = activeDownloadForKey(requestKey)) {
            pActive->owners.insert(owner);
            continue;
        }
        bool alreadyPending = false;
        for (qsizetype i = 0; i < m_downloadQueue.size(); ++i) {
            if (m_downloadQueue.at(i).requestKey != requestKey) {
                continue;
            }
            alreadyPending = true;
            if (!m_downloadQueue.at(i).owners.contains(owner)) {
                PendingDownload promoted = m_downloadQueue.takeAt(i);
                promoted.owners.insert(owner);
                enqueuePendingDownload(std::move(promoted));
            }
            break;
        }
        if (alreadyPending) {
            continue;
        }
        enqueuePendingDownload(PendingDownload{
                track,
                settings,
                requestKey,
                QSet<RestLibraryCacheRequestOwner>{owner}});
        emitState(settings, track.remoteId, RestLibraryCacheState::Missing);
    }
    startNextDownloads();
}

void RestLibraryCacheManager::cancelRequests(
        RestLibraryCacheRequestOwner owner) {
    QList<std::pair<RestLibrarySettings, QString>> cancelledDownloads;
    QList<PendingDownload> retainedDownloads;
    retainedDownloads.reserve(m_downloadQueue.size());
    for (PendingDownload& pending : m_downloadQueue) {
        pending.owners.remove(owner);
        if (!pending.owners.isEmpty()) {
            retainedDownloads.append(std::move(pending));
        }
    }
    m_downloadQueue.clear();
    for (PendingDownload& pending : retainedDownloads) {
        enqueuePendingDownload(std::move(pending));
    }

    for (auto it = m_activeDownloads.begin(); it != m_activeDownloads.end();) {
        it->owners.remove(owner);
        if (!it->owners.isEmpty()) {
            ++it;
            continue;
        }
        const RestLibrarySettings settings = it->settings;
        const QString remoteId = it->track.remoteId;
        if (it->reply) {
            disconnect(it->reply, nullptr, this, nullptr);
            it->reply->abort();
            it->reply->deleteLater();
        }
        cleanupActiveDownload(&*it);
        QFile::remove(it->tempFilePath);
        it = m_activeDownloads.erase(it);
        cancelledDownloads.append({settings, remoteId});
    }
    for (const auto& [settings, remoteId] : cancelledDownloads) {
        emitState(settings, remoteId, RestLibraryCacheState::Missing);
    }
    startNextDownloads();
}

void RestLibraryCacheManager::abortAll() {
    m_downloadQueue.clear();
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

QString RestLibraryCacheManager::serverIdentity(
        const RestLibrarySettings& settings) {
    QUrl scopedUrl = settings.baseUrl.adjusted(
            QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment);
    QString path = scopedUrl.path();
    while (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    scopedUrl.setPath(path);
    return scopedUrl.toString(QUrl::FullyEncoded);
}

QString RestLibraryCacheManager::cacheFileStemForTesting(const QString& remoteId) {
    return legacyCacheFileStem(remoteId);
}

QString RestLibraryCacheManager::cacheFileStemForTesting(
        const QUrl& baseUrl,
        const QString& remoteId) {
    RestLibrarySettings settings;
    settings.baseUrl = baseUrl;
    return cacheFileStem(settings, remoteId);
}

QString RestLibraryCacheManager::extensionFromContentTypeForTesting(
        const QByteArray& contentType) {
    return extensionFromContentType(contentType);
}

QString RestLibraryCacheManager::existingCachedFilePathForTesting(
        const QString& cacheDirectoryPath,
        const QString& remoteId) {
    QDir cacheDir(cacheDirectoryPath);
    const QString stem = legacyCacheFileStem(remoteId);
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
    consumeReplyBytes(pReply);
}

void RestLibraryCacheManager::consumeReplyBytes(QNetworkReply* pReply) {
    ActiveDownload* pDownload = activeDownloadForReply(pReply);
    if (!pDownload || !pDownload->pFile) {
        return;
    }
    const QByteArray bytes = pReply->readAll();
    if (bytes.isEmpty()) {
        return;
    }

    const qsizetype remainingDiagnosticBytes =
            kMaxLoggedResponseBytes - pDownload->responsePrefix.size();
    if (remainingDiagnosticBytes > 0) {
        pDownload->responsePrefix.append(bytes.left(remainingDiagnosticBytes));
    }
    if (pDownload->writeFailed || pDownload->sizeLimitExceeded) {
        return;
    }

    const qint64 maxBytes =
            static_cast<qint64>(pDownload->settings.cacheMaxMegabytes) *
            kBytesPerMegabyte;
    if (bytes.size() > maxBytes - pDownload->bytesWritten) {
        pDownload->sizeLimitExceeded = true;
        pReply->abort();
        return;
    }

    const qint64 written = pDownload->pFile->write(bytes);
    if (written > 0) {
        pDownload->bytesWritten += written;
    }
    if (written != bytes.size()) {
        pDownload->writeFailed = true;
        pDownload->fileError = pDownload->pFile->errorString();
        pReply->abort();
    }
}

void RestLibraryCacheManager::slotDownloadFinished() {
    auto* pReply = qobject_cast<QNetworkReply*>(sender());
    finishDownload(pReply);
}

QNetworkRequest RestLibraryCacheManager::newDownloadRequest(
        const RestLibraryTrack& track,
        const RestLibrarySettings& settings) const {
    QNetworkRequest request(urlWithPathTemplate(
            settings,
            settings.audioDownloadPathTemplate,
            track.remoteId));
    request.setAttribute(
            QNetworkRequest::RedirectPolicyAttribute,
            QNetworkRequest::SameOriginRedirectPolicy);
    request.setTransferTimeout(kDownloadTimeoutMillis);
    if (settings.maySendBearerTokenTo(request.url())) {
        request.setRawHeader(
                "Authorization",
                QByteArray("Bearer ") + settings.bearerToken.toUtf8());
    }
    kLogger.info()
            << "REST library audio download request"
            << request.url().toString(QUrl::RemoveUserInfo);
    return request;
}

void RestLibraryCacheManager::startNextDownloads() {
    while (!m_downloadQueue.isEmpty() &&
            m_activeDownloads.size() <
                    m_downloadQueue.constFirst().settings.maxConcurrentDownloads) {
        startDownload(m_downloadQueue.takeFirst());
    }
}

void RestLibraryCacheManager::enqueuePendingDownload(
        PendingDownload pendingDownload) {
    const int priority = requestPriority(pendingDownload.owners);
    auto insertBefore = m_downloadQueue.end();
    for (auto it = m_downloadQueue.begin(); it != m_downloadQueue.end(); ++it) {
        if (requestPriority(it->owners) < priority) {
            insertBefore = it;
            break;
        }
    }
    m_downloadQueue.insert(insertBefore, std::move(pendingDownload));
}

RestLibraryCacheManager::ActiveDownload*
RestLibraryCacheManager::activeDownloadForKey(const QString& requestKey) {
    for (ActiveDownload& active : m_activeDownloads) {
        if (active.requestKey == requestKey) {
            return &active;
        }
    }
    return nullptr;
}

void RestLibraryCacheManager::startDownload(PendingDownload pendingDownload) {
    const RestLibraryTrack& track = pendingDownload.track;
    const RestLibrarySettings& settings = pendingDownload.settings;
    QDir cacheDir(settings.cacheDirectoryPath);
    const QString tempFilePath = cacheDir.filePath(
            cacheFileStem(settings, track.remoteId) + QStringLiteral(".download"));
    QFile::remove(tempFilePath);

    QFile* pFile = m_cacheFileFactory(tempFilePath, this);
    if (!pFile) {
        emitState(
                settings,
                track.remoteId,
                RestLibraryCacheState::Failed,
                {},
                tr("Cache file could not be created."));
        startNextDownloads();
        return;
    }
    if (!pFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        pFile->deleteLater();
        emitState(
                settings,
                track.remoteId,
                RestLibraryCacheState::Failed,
                {},
                tr("Cache file could not be opened."));
        startNextDownloads();
        return;
    }

    QNetworkReply* pReply =
            m_pNetworkAccessManager->get(newDownloadRequest(track, settings));
    pReply->setParent(this);
    m_activeDownloads.push_back(ActiveDownload{
            track,
            settings,
            pendingDownload.requestKey,
            pendingDownload.owners,
            QPointer<QNetworkReply>(pReply),
            pFile,
            tempFilePath,
            0,
            {},
            {},
            false,
            false});
    emitState(settings, track.remoteId, RestLibraryCacheState::Downloading);

    connect(pReply, &QNetworkReply::readyRead, this, &RestLibraryCacheManager::slotReadyRead);
    connect(pReply, &QNetworkReply::metaDataChanged, this, [this, pReply] {
        ActiveDownload* pDownload = activeDownloadForReply(pReply);
        if (!pDownload || pDownload->sizeLimitExceeded) {
            return;
        }
        bool validContentLength = false;
        const qint64 contentLength = pReply
                                             ->header(QNetworkRequest::ContentLengthHeader)
                                             .toLongLong(&validContentLength);
        const qint64 maxBytes =
                static_cast<qint64>(pDownload->settings.cacheMaxMegabytes) *
                kBytesPerMegabyte;
        if (validContentLength && contentLength > maxBytes) {
            pDownload->sizeLimitExceeded = true;
            pReply->abort();
        }
    });
    connect(pReply, &QNetworkReply::finished, this, &RestLibraryCacheManager::slotDownloadFinished);
}

void RestLibraryCacheManager::finishDownload(QNetworkReply* pReply) {
    ActiveDownload* pDownload = activeDownloadForReply(pReply);
    if (!pDownload) {
        return;
    }

    const int statusCode = pReply ? statusCodeFromReply(*pReply) : 0;
    const bool replyMayContainAudio = pReply &&
            pReply->error() == QNetworkReply::NoError &&
            isSuccessStatus(statusCode);
    if (replyMayContainAudio) {
        consumeReplyBytes(pReply);
    }
    if (pDownload->pFile) {
        pDownload->pFile->close();
    }
    if (pReply) {
        pReply->deleteLater();
    }

    const QString remoteId = pDownload->track.remoteId;
    const QString requestKey = pDownload->requestKey;
    const RestLibrarySettings downloadSettings = pDownload->settings;
    if (pReply) {
        const QByteArray remainingBytes = pReply->readAll();
        const qsizetype remainingDiagnosticBytes =
                kMaxLoggedResponseBytes - pDownload->responsePrefix.size();
        if (remainingDiagnosticBytes > 0) {
            pDownload->responsePrefix.append(
                    remainingBytes.left(remainingDiagnosticBytes));
        }
    }
    const QByteArray responsePrefix = pDownload->responsePrefix;
    const bool success = replyMayContainAudio && !pDownload->writeFailed &&
            !pDownload->sizeLimitExceeded &&
            pDownload->bytesWritten > 0;

    if (!success) {
        QString failureSummary = tr("Audio download failed.");
        int networkError = 0;
        if (pReply) {
            kLogger.warning()
                    << "REST library audio download failed"
                    << pReply->request().url().toString(QUrl::RemoveUserInfo)
                    << "status" << statusCode << "network error" << pReply->error()
                    << pReply->errorString() << "bytes written"
                    << pDownload->bytesWritten << "body"
                    << responseSnippet(responsePrefix);
            RestLibraryRequestDiagnostic diagnostic;
            diagnostic.stage = tr("Audio download");
            diagnostic.method = QStringLiteral("GET");
            diagnostic.url =
                    pReply->request().url().toString(QUrl::RemoveUserInfo);
            diagnostic.success = false;
            diagnostic.statusCode = statusCode;
            const bool localFileFailure =
                    pDownload->writeFailed || pDownload->sizeLimitExceeded;
            diagnostic.networkError =
                    localFileFailure ? static_cast<int>(QNetworkReply::NoError)
                                     : static_cast<int>(pReply->error());
            networkError = diagnostic.networkError;
            if (pDownload->sizeLimitExceeded) {
                diagnostic.errorText =
                        tr("Cached audio file exceeds the cache size limit.");
            } else if (pDownload->writeFailed) {
                diagnostic.errorText =
                        pDownload->fileError.isEmpty()
                        ? tr("Cache file could not be written completely.")
                        : tr("Cache file could not be written completely: %1")
                                  .arg(pDownload->fileError);
            } else {
                diagnostic.errorText = pReply->error() == QNetworkReply::NoError
                        ? errorTextFromResponse(responsePrefix)
                        : pReply->errorString();
            }
            diagnostic.responseSnippet = responseSnippet(responsePrefix);
            if (localFileFailure) {
                diagnostic.summary = diagnostic.errorText;
            } else if (diagnostic.networkError !=
                            static_cast<int>(QNetworkReply::NoError) &&
                    !diagnostic.errorText.isEmpty()) {
                diagnostic.summary =
                        tr("Audio download failed: %1.").arg(diagnostic.errorText);
            } else if (!diagnostic.errorText.isEmpty()) {
                diagnostic.summary =
                        tr("Audio download failed: %1.").arg(diagnostic.errorText);
            } else if (diagnostic.statusCode > 0) {
                diagnostic.summary = tr("Audio download failed with HTTP %1.")
                                             .arg(diagnostic.statusCode);
            } else {
                diagnostic.summary = tr("Audio download failed.");
            }
            failureSummary = diagnostic.summary;
            emit requestDiagnosticUpdated(diagnostic);
        } else {
            kLogger.warning() << "REST library audio download failed without a reply";
        }
        QFile::remove(pDownload->tempFilePath);
        cleanupActiveDownload(pDownload);
        removeActiveDownload(requestKey);
        emitState(downloadSettings,
                remoteId,
                RestLibraryCacheState::Failed,
                {},
                failureSummary,
                statusCode,
                networkError);
        startNextDownloads();
        return;
    }

    const QString finalFilePath = finalCachedFilePath(
            pDownload->settings,
            pDownload->track,
            *pReply);
    if (finalFilePath.isEmpty()) {
        QFile::remove(pDownload->tempFilePath);
        cleanupActiveDownload(pDownload);
        removeActiveDownload(requestKey);
        emitState(downloadSettings,
                remoteId,
                RestLibraryCacheState::Failed,
                {},
                tr("Audio file type could not be determined."));
        startNextDownloads();
        return;
    }

    QFile::remove(finalFilePath);
    if (!QFile::rename(pDownload->tempFilePath, finalFilePath)) {
        QFile::remove(pDownload->tempFilePath);
        cleanupActiveDownload(pDownload);
        removeActiveDownload(requestKey);
        emitState(downloadSettings,
                remoteId,
                RestLibraryCacheState::Failed,
                {},
                tr("Cached audio file could not be finalized."));
        startNextDownloads();
        return;
    }

    if (QFileInfo(finalFilePath).size() >
            static_cast<qint64>(pDownload->settings.cacheMaxMegabytes) * kBytesPerMegabyte) {
        QFile::remove(finalFilePath);
        cleanupActiveDownload(pDownload);
        removeActiveDownload(requestKey);
        emitState(downloadSettings,
                remoteId,
                RestLibraryCacheState::Failed,
                {},
                tr("Cached audio file exceeds the cache size limit."));
        startNextDownloads();
        return;
    }

    cleanupActiveDownload(pDownload);
    removeActiveDownload(requestKey);
    pruneCacheSize(downloadSettings, finalFilePath);
    emitState(downloadSettings,
            remoteId,
            RestLibraryCacheState::Ready,
            finalFilePath);
    startNextDownloads();
}

void RestLibraryCacheManager::rememberTrackCacheStems(
        const QList<RestLibraryTrack>& tracks,
        const RestLibrarySettings& settings) {
    for (const RestLibraryTrack& track : tracks) {
        if (!track.remoteId.isEmpty()) {
            const QString stem = cacheFileStem(settings, track.remoteId);
            m_remoteIdByCacheStem.insert(stem, track.remoteId);
            m_serverIdentityByCacheStem.insert(stem, serverIdentity(settings));
        }
    }
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

void RestLibraryCacheManager::pruneExpiredCachedFiles(
        const QList<RestLibraryTrack>& tracks,
        const RestLibrarySettings& settings) {
    const QDateTime cutoff =
            QDateTime::currentDateTimeUtc().addDays(-settings.cacheMaxAgeDays);
    QHash<QString, QString> remoteIdByCacheStem;
    for (const RestLibraryTrack& track : tracks) {
        if (!track.remoteId.isEmpty()) {
            remoteIdByCacheStem.insert(
                    cacheFileStem(settings, track.remoteId), track.remoteId);
        }
    }

    for (const QFileInfo& fileInfo : cachedFileInfos(settings)) {
        if (fileInfo.lastModified().toUTC() >= cutoff) {
            continue;
        }
        const QString filePath = QDir::fromNativeSeparators(fileInfo.absoluteFilePath());
        QFile::remove(filePath);
        const QString remoteId = remoteIdByCacheStem.value(fileInfo.completeBaseName());
        if (!remoteId.isEmpty()) {
            emitState(settings, remoteId, RestLibraryCacheState::Stale);
        }
    }
}

void RestLibraryCacheManager::pruneCacheSize(
        const RestLibrarySettings& settings,
        const QString& preservedFilePath) {
    QList<QFileInfo> fileInfos = cachedFileInfos(settings);
    qint64 totalBytes = 0;
    for (const QFileInfo& fileInfo : fileInfos) {
        totalBytes += fileInfo.size();
    }

    const qint64 maxBytes =
            static_cast<qint64>(settings.cacheMaxMegabytes) * kBytesPerMegabyte;
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
            const QString remoteId =
                    m_remoteIdByCacheStem.value(fileInfo.completeBaseName());
            if (!remoteId.isEmpty() &&
                    m_serverIdentityByCacheStem.value(fileInfo.completeBaseName()) ==
                            serverIdentity(settings)) {
                emitState(settings, remoteId, RestLibraryCacheState::Stale);
            }
        }
        if (totalBytes <= maxBytes) {
            return;
        }
    }
}

QList<QFileInfo> RestLibraryCacheManager::cachedFileInfos(
        const RestLibrarySettings& settings) const {
    const QDir cacheDir(settings.cacheDirectoryPath);
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

QString RestLibraryCacheManager::existingCachedFilePath(
        const RestLibrarySettings& settings,
        const QString& remoteId) const {
    QDir cacheDir(settings.cacheDirectoryPath);
    const QString stem = cacheFileStem(settings, remoteId);
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

QString RestLibraryCacheManager::finalCachedFilePath(
        const RestLibrarySettings& settings,
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
    return QDir::fromNativeSeparators(QDir(settings.cacheDirectoryPath)
                                              .filePath(cacheFileStem(settings, track.remoteId) +
                                                      QStringLiteral(".") +
                                                      extension));
}

void RestLibraryCacheManager::emitState(
        const RestLibrarySettings& settings,
        const QString& remoteId,
        RestLibraryCacheState cacheState,
        const QString& cachedFilePath,
        const QString& errorText,
        int statusCode,
        int networkError) {
    emit trackCacheStateChanged(RestLibraryCacheResult{
            remoteId,
            cacheState,
            QDir::fromNativeSeparators(cachedFilePath),
            errorText,
            statusCode,
            networkError,
            serverIdentity(settings)});
}

void RestLibraryCacheManager::cleanupActiveDownload(ActiveDownload* pDownload) {
    if (!pDownload || !pDownload->pFile) {
        return;
    }
    pDownload->pFile->close();
    pDownload->pFile->deleteLater();
    pDownload->pFile = nullptr;
}

void RestLibraryCacheManager::removeActiveDownload(const QString& requestKey) {
    for (auto it = m_activeDownloads.begin(); it != m_activeDownloads.end(); ++it) {
        if (it->requestKey == requestKey) {
            m_activeDownloads.erase(it);
            return;
        }
    }
}

QString RestLibraryCacheManager::legacyCacheFileStem(const QString& remoteId) {
    const QByteArray hash = QCryptographicHash::hash(
            remoteId.toUtf8(),
            QCryptographicHash::Sha256).toHex();
    return QString::fromLatin1(hash.left(kCacheHashLength));
}

QString RestLibraryCacheManager::cacheFileStem(
        const RestLibrarySettings& settings,
        const QString& remoteId) {
    const QByteArray identity =
            serverIdentity(settings).toUtf8() + '\0' + remoteId.toUtf8();
    const QByteArray hash =
            QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex();
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

    return config::urlWithRestPath(settings.baseUrl, path);
}

} // namespace mixxx::library::rest
