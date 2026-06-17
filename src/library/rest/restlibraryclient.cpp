#include "library/rest/restlibraryclient.h"

#include <algorithm>
#include <cmath>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QUrlQuery>

#include "moc_restlibraryclient.cpp"
#include "track/track.h"
#include "util/logger.h"

namespace mixxx::library::rest {

namespace {

const Logger kLogger("RestLibraryClient");

constexpr int kRequestTimeoutMillis = 15000;
constexpr qsizetype kMaxLoggedResponseBytes = 500;

bool isSuccessStatus(int statusCode) {
    return statusCode >= 200 && statusCode < 300;
}

int statusCodeFromReply(const QNetworkReply& reply) {
    return reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
}

QJsonValue findFirstArrayValue(const QJsonObject& object) {
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (it.value().isArray()) {
            return it.value();
        }
    }
    return {};
}

QString valueToString(const QJsonValue& value) {
    if (value.isString()) {
        return value.toString();
    }
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (std::floor(number) == number) {
            return QString::number(static_cast<qlonglong>(number));
        }
        return QString::number(number);
    }
    return {};
}

QUrl urlWithPath(const QUrl& baseUrl, const QString& path) {
    QUrl url = baseUrl;
    const QUrl relativeUrl(path);
    const QString relativePath = relativeUrl.path().isEmpty() ? path : relativeUrl.path();
    url.setPath(relativePath.startsWith('/') ? relativePath : QStringLiteral("/") + relativePath);
    url.setQuery(relativeUrl.query());
    return url;
}

QString percentEncode(const QString& value) {
    return QString::fromUtf8(QUrl::toPercentEncoding(value));
}

QString pathForRemoteId(const QString& pathTemplate, const QString& remoteId) {
    QString path = pathTemplate;
    path.replace(QStringLiteral("%1"), percentEncode(remoteId));
    return path;
}

QString pathForTrackLookup(const QString& pathTemplate, const TrackPointer& pTrack) {
    QString path = pathTemplate;
    if (!pTrack) {
        return path;
    }
    path.replace(QStringLiteral("%artist"), percentEncode(pTrack->getArtist()));
    path.replace(QStringLiteral("%title"), percentEncode(pTrack->getTitle()));
    path.replace(
            QStringLiteral("%duration"),
            percentEncode(QString::number(qRound(pTrack->getDuration()))));
    path.replace(QStringLiteral("%location"), percentEncode(pTrack->getLocation()));
    return path;
}

QString responseSnippet(const QByteArray& body) {
    QString snippet = QString::fromUtf8(body.left(kMaxLoggedResponseBytes)).trimmed();
    snippet.replace(QChar('\n'), QChar(' '));
    snippet.replace(QChar('\r'), QChar(' '));
    return snippet;
}

} // namespace

RestLibraryClient::RestLibraryClient(
        QNetworkAccessManager* pNetworkAccessManager,
        QObject* parent)
        : QObject(parent),
          m_pNetworkAccessManager(pNetworkAccessManager) {
    qRegisterMetaType<RestLibraryTrack>("mixxx::library::rest::RestLibraryTrack");
    qRegisterMetaType<QList<RestLibraryTrack>>("QList<mixxx::library::rest::RestLibraryTrack>");
}

void RestLibraryClient::fetchTracks(const RestLibrarySettings& settings) {
    clearPendingDetails();
    m_settings = settings;
    m_requestPurpose = RequestPurpose::Tracks;
    m_pendingTracks.clear();
    m_finishedDetailCount = 0;
    m_detailBatchFailed = false;

    if (!m_pNetworkAccessManager) {
        emit fetchFailed(tr("Network access is not available."));
        return;
    }
    if (!m_settings.isConfigured()) {
        emit tracksFetched({});
        return;
    }

    QNetworkReply* pReply = m_pNetworkAccessManager->get(
            newRequest(m_settings.trackListPath, m_settings.pageSize));
    pReply->setParent(this);
    connect(pReply, &QNetworkReply::finished, this, &RestLibraryClient::slotTrackListFinished);
}

void RestLibraryClient::lookupTrack(
        const RestLibrarySettings& settings,
        const TrackPointer& pTrack) {
    clearPendingDetails();
    m_settings = settings;
    m_requestPurpose = RequestPurpose::TrackLookup;
    m_pendingTracks.clear();
    m_finishedDetailCount = 0;
    m_detailBatchFailed = false;

    if (!m_pNetworkAccessManager) {
        emit trackLookupMissed(tr("Network access is not available."));
        return;
    }
    if (!m_settings.hasTrackLookupConfigured() || !pTrack) {
        emit trackLookupMissed(tr("Remote track lookup is not configured."));
        return;
    }

    QNetworkReply* pReply = m_pNetworkAccessManager->get(
            newRequest(pathForTrackLookup(m_settings.trackLookupPathTemplate, pTrack), 1));
    pReply->setParent(this);
    connect(pReply, &QNetworkReply::finished, this, &RestLibraryClient::slotTrackListFinished);
}

void RestLibraryClient::fetchRecommendations(
        const RestLibrarySettings& settings,
        const QString& remoteId) {
    clearPendingDetails();
    m_settings = settings;
    m_requestPurpose = RequestPurpose::Recommendations;
    m_pendingTracks.clear();
    m_finishedDetailCount = 0;
    m_detailBatchFailed = false;

    if (!m_pNetworkAccessManager) {
        emit fetchFailed(tr("Network access is not available."));
        return;
    }
    if (!m_settings.hasRecommendationsConfigured() || remoteId.trimmed().isEmpty()) {
        emit recommendationsFetched({});
        return;
    }

    QNetworkReply* pReply = m_pNetworkAccessManager->get(
            newRequest(
                    pathForRemoteId(m_settings.recommendationPathTemplate, remoteId),
                    m_settings.recommendationLimit));
    pReply->setParent(this);
    connect(pReply, &QNetworkReply::finished, this, &RestLibraryClient::slotTrackListFinished);
}

QNetworkRequest RestLibraryClient::newRequest(const QString& path, int limit) const {
    QUrl url = urlWithPath(m_settings.baseUrl, path);
    QUrlQuery query(url);
    if (limit > 0 && !query.hasQueryItem(QStringLiteral("limit"))) {
        query.addQueryItem(QStringLiteral("limit"), QString::number(limit));
    }
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setAttribute(
            QNetworkRequest::RedirectPolicyAttribute,
            QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kRequestTimeoutMillis);
    if (!m_settings.bearerToken.isEmpty()) {
        request.setRawHeader(
                "Authorization",
                QByteArray("Bearer ") + m_settings.bearerToken.toUtf8());
    }
    kLogger.info() << "REST library request" << url.toString(QUrl::RemoveUserInfo);
    return request;
}

QNetworkRequest RestLibraryClient::newDetailRequest(const QString& remoteId) const {
    return newRequest(pathForRemoteId(m_settings.trackDetailPathTemplate, remoteId), 0);
}

void RestLibraryClient::slotTrackListFinished() {
    auto* pReply = qobject_cast<QNetworkReply*>(sender());
    if (!pReply) {
        emit fetchFailed(tr("Remote library request failed."));
        return;
    }
    pReply->deleteLater();

    const QByteArray responseBody = pReply->readAll();
    const int statusCode = statusCodeFromReply(*pReply);
    if (!isSuccessStatus(statusCode)) {
        kLogger.warning()
                << "REST library request failed"
                << pReply->request().url().toString(QUrl::RemoveUserInfo)
                << "status" << statusCode
                << "network error" << pReply->error()
                << pReply->errorString()
                << "body" << responseSnippet(responseBody);
        emitFailureForCurrentPurpose(
                tr("Remote library request returned an unsuccessful status."));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(responseBody, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        kLogger.warning()
                << "Failed to parse remote library JSON response from"
                << pReply->request().url().toString(QUrl::RemoveUserInfo)
                << "body" << responseSnippet(responseBody);
        emitFailureForCurrentPurpose(tr("Remote library response was not valid JSON."));
        return;
    }

    QStringList remoteIds;
    m_pendingTracks = parseTrackListDocument(document, &remoteIds);
    if (m_requestPurpose == RequestPurpose::TrackLookup) {
        if (!m_pendingTracks.isEmpty()) {
            emit trackLookupSucceeded(m_pendingTracks.constFirst().remoteId);
            return;
        }
        if (!remoteIds.isEmpty()) {
            emit trackLookupSucceeded(remoteIds.constFirst());
            return;
        }
        emit trackLookupMissed(tr("No matching remote track was found."));
        return;
    }

    if (remoteIds.isEmpty() || m_settings.trackDetailPathTemplate.trimmed().isEmpty()) {
        emitTracksForCurrentPurpose(m_pendingTracks);
        return;
    }
    startDetailRequests(remoteIds);
}

void RestLibraryClient::startDetailRequests(const QStringList& remoteIds) {
    if (!m_pNetworkAccessManager) {
        emitTracksForCurrentPurpose(m_pendingTracks);
        return;
    }

    const int requestCount = std::min(
            static_cast<int>(remoteIds.size()),
            m_requestPurpose == RequestPurpose::Recommendations
                    ? m_settings.recommendationLimit
                    : m_settings.pageSize);
    m_pendingDetails.reserve(requestCount);
    for (int i = 0; i < requestCount; ++i) {
        QNetworkReply* pReply = m_pNetworkAccessManager->get(newDetailRequest(remoteIds.at(i)));
        pReply->setParent(this);
        m_pendingDetails.push_back(PendingDetail{
                QPointer<QNetworkReply>(pReply),
                remoteIds.at(i)});
        connect(pReply, &QNetworkReply::finished, this, &RestLibraryClient::slotTrackDetailFinished);
    }
}

void RestLibraryClient::slotTrackDetailFinished() {
    auto* pReply = qobject_cast<QNetworkReply*>(sender());
    if (!pReply) {
        return;
    }
    pReply->deleteLater();
    ++m_finishedDetailCount;

    const QByteArray responseBody = pReply->readAll();
    const int statusCode = statusCodeFromReply(*pReply);
    if (!isSuccessStatus(statusCode)) {
        kLogger.warning()
                << "REST library track detail request failed"
                << pReply->request().url().toString(QUrl::RemoveUserInfo)
                << "status" << statusCode
                << "network error" << pReply->error()
                << pReply->errorString()
                << "body" << responseSnippet(responseBody);
        m_detailBatchFailed = true;
        finishDetailBatchIfComplete();
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(responseBody, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        kLogger.warning()
                << "REST library track detail response was not a JSON object from"
                << pReply->request().url().toString(QUrl::RemoveUserInfo)
                << "body" << responseSnippet(responseBody);
        m_detailBatchFailed = true;
        finishDetailBatchIfComplete();
        return;
    }

    RestLibraryTrack track = parseTrackObject(document.object());
    if (track.remoteId.isEmpty()) {
        for (const auto& detail : std::as_const(m_pendingDetails)) {
            if (detail.reply == pReply) {
                track.remoteId = detail.remoteId;
                break;
            }
        }
    }
    if (!track.remoteId.isEmpty()) {
        m_pendingTracks.append(std::move(track));
    }

    finishDetailBatchIfComplete();
}

void RestLibraryClient::finishDetailBatchIfComplete() {
    if (m_finishedDetailCount < m_pendingDetails.size()) {
        return;
    }
    if (m_pendingTracks.isEmpty() && m_detailBatchFailed) {
        emitFailureForCurrentPurpose(tr("Remote library track details could not be loaded."));
    } else {
        emitTracksForCurrentPurpose(m_pendingTracks);
    }
    m_pendingDetails.clear();
}

void RestLibraryClient::clearPendingDetails() {
    for (const auto& detail : std::as_const(m_pendingDetails)) {
        if (detail.reply) {
            if (!detail.reply->isFinished()) {
                detail.reply->abort();
            }
            detail.reply->deleteLater();
        }
    }
    m_pendingDetails.clear();
}

void RestLibraryClient::emitTracksForCurrentPurpose(const QList<RestLibraryTrack>& tracks) {
    switch (m_requestPurpose) {
    case RequestPurpose::Recommendations:
        emit recommendationsFetched(tracks);
        break;
    case RequestPurpose::TrackLookup:
        if (tracks.isEmpty()) {
            emit trackLookupMissed(tr("No matching remote track was found."));
        } else {
            emit trackLookupSucceeded(tracks.constFirst().remoteId);
        }
        break;
    case RequestPurpose::Tracks:
        emit tracksFetched(tracks);
        break;
    }
}

void RestLibraryClient::emitFailureForCurrentPurpose(const QString& message) {
    switch (m_requestPurpose) {
    case RequestPurpose::TrackLookup:
        emit trackLookupMissed(message);
        break;
    case RequestPurpose::Recommendations:
    case RequestPurpose::Tracks:
        emit fetchFailed(message);
        break;
    }
}

QList<RestLibraryTrack> RestLibraryClient::parseTrackListDocumentForTesting(
        const QJsonDocument& document) {
    return parseTrackListDocument(document, nullptr);
}

RestLibraryTrack RestLibraryClient::parseTrackObjectForTesting(
        const QJsonObject& object) {
    return parseTrackObject(object);
}

QList<RestLibraryTrack> RestLibraryClient::parseTrackListDocument(
        const QJsonDocument& document,
        QStringList* pRemoteIds) {
    QJsonValue listValue;
    if (document.isArray()) {
        listValue = document.array();
    } else if (document.isObject()) {
        listValue = findFirstArrayValue(document.object());
    }

    QList<RestLibraryTrack> tracks;
    if (!listValue.isArray()) {
        return tracks;
    }

    const QJsonArray values = listValue.toArray();
    for (const auto& value : values) {
        if (value.isObject()) {
            RestLibraryTrack track = parseTrackObject(value.toObject());
            if (!track.remoteId.isEmpty()) {
                tracks.append(std::move(track));
            }
        } else {
            const QString remoteId = valueToString(value);
            if (!remoteId.isEmpty() && pRemoteIds) {
                pRemoteIds->append(remoteId);
            }
        }
    }
    return tracks;
}

RestLibraryTrack RestLibraryClient::parseTrackObject(const QJsonObject& object) {
    RestLibraryTrack track;
    track.remoteId = readString(object, {"id"});
    track.reviewId = readString(object, {"review_id", "hash_id"});
    track.title = readString(object, {"title", "name"});
    track.artist = readString(object, {"artist", "artists"});
    track.album = readString(object, {"album"});
    track.genre = readString(object, {"genre"});
    track.composer = readString(object, {"composer"});
    track.comment = readString(object, {"comment"});
    track.keyText = readString(object, {"key", "musical_key"});
    track.trackNumber = readString(object, {"track_number", "tracknumber"});
    track.label = readString(object, {"label"});
    track.sourceLabel = readString(object, {"source"});
    track.audioFileExtension = readString(object, {"extension", "file_extension", "audio_extension"});
    track.bpm = readDouble(object, {"bpm"});
    track.durationSeconds = readDouble(object, {"duration", "duration_seconds"});
    track.rating = readRating(object);

    const QString releaseDate = readString(object, {"release_date", "date"});
    if (!releaseDate.isEmpty()) {
        track.releaseDate = QDate::fromString(releaseDate.left(10), Qt::ISODate);
    }

    const QString sourceUrl = readString(object, {"permalink", "source_url", "url"});
    if (!sourceUrl.isEmpty()) {
        track.sourceUrl = QUrl(sourceUrl);
    }
    const QString artworkUrl = readString(object, {"artwork", "artwork_url", "cover_url"});
    if (!artworkUrl.isEmpty()) {
        track.artworkUrl = QUrl(artworkUrl);
    }
    return track;
}

QString RestLibraryClient::readString(
        const QJsonObject& object,
        std::initializer_list<QString> keys) {
    for (const QString& key : keys) {
        const QJsonValue value = object.value(key);
        const QString stringValue = valueToString(value).trimmed();
        if (!stringValue.isEmpty()) {
            return stringValue;
        }
    }
    return {};
}

double RestLibraryClient::readDouble(
        const QJsonObject& object,
        std::initializer_list<QString> keys) {
    for (const QString& key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isDouble()) {
            return value.toDouble();
        }
        if (value.isString()) {
            bool ok = false;
            const double result = value.toString().toDouble(&ok);
            if (ok) {
                return result;
            }
        }
    }
    return 0.0;
}

int RestLibraryClient::readRating(const QJsonObject& object) {
    const double rating = readDouble(object, {"rating"});
    if (rating <= 0.0) {
        return 0;
    }
    if (rating <= 5.0) {
        return static_cast<int>(std::round(rating));
    }
    return std::clamp(static_cast<int>(std::round(rating)), 0, 100);
}

} // namespace mixxx::library::rest
