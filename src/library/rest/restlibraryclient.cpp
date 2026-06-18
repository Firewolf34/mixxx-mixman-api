#include "library/rest/restlibraryclient.h"

#include <algorithm>
#include <cmath>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QVariant>

#include "moc_restlibraryclient.cpp"
#include "track/track.h"
#include "util/logger.h"

namespace mixxx::library::rest {

namespace {

const Logger kLogger("RestLibraryClient");

constexpr int kRequestTimeoutMillis = 15000;
constexpr qsizetype kMaxLoggedResponseBytes = 500;
const QString kSessionCreateOperation = QStringLiteral("session_create");
const QString kSessionSnapshotOperation = QStringLiteral("session_snapshot");
const QString kSessionIntentOperation = QStringLiteral("session_intent");
const QString kSessionHeartbeatOperation = QStringLiteral("session_heartbeat");
const char* kRequestGenerationProperty = "requestGeneration";

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

QString positiveIntegerString(const QString& value) {
    const QString trimmedValue = value.trimmed();
    if (trimmedValue.isEmpty()) {
        return {};
    }
    bool ok = false;
    const qlonglong integerValue = trimmedValue.toLongLong(&ok);
    if (!ok || integerValue <= 0 || QString::number(integerValue) != trimmedValue) {
        return {};
    }
    return QString::number(integerValue);
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

QString pathWithQueryItem(QString path, const QString& key, const QString& value) {
    QUrl url(path);
    QUrlQuery query(url);
    query.removeAllQueryItems(key);
    query.addQueryItem(key, value);
    url.setQuery(query);
    return url.toString();
}

QJsonObject objectForTrackId(const QJsonObject& tracksById, const QString& remoteId) {
    const QJsonValue value = tracksById.value(remoteId);
    return value.isObject() ? value.toObject() : QJsonObject();
}

QStringList readStringArray(const QJsonObject& object, const QString& key) {
    QStringList result;
    const QJsonValue value = object.value(key);
    if (!value.isArray()) {
        return result;
    }
    const QJsonArray values = value.toArray();
    result.reserve(values.size());
    for (const QJsonValue& item : values) {
        const QString stringValue = valueToString(item);
        if (!stringValue.isEmpty()) {
            result.append(stringValue);
        }
    }
    return result;
}

QString responseSnippet(const QByteArray& body) {
    QString snippet = QString::fromUtf8(body.left(kMaxLoggedResponseBytes)).trimmed();
    snippet.replace(QChar('\n'), QChar(' '));
    snippet.replace(QChar('\r'), QChar(' '));
    return snippet;
}

QByteArray jsonBody(const QJsonObject& object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

void insertIfNotEmpty(QJsonObject* pObject, const QString& key, const QString& value) {
    if (pObject && !value.trimmed().isEmpty()) {
        pObject->insert(key, value.trimmed());
    }
}

void insertIntegerStringIfValid(QJsonObject* pObject, const QString& key, const QString& value) {
    bool ok = false;
    const int integerValue = value.toInt(&ok);
    if (pObject && ok && integerValue > 0) {
        pObject->insert(key, integerValue);
    }
}

QJsonObject baseSessionClientObject(const QString& clientId) {
    QJsonObject object;
    insertIfNotEmpty(&object, QStringLiteral("client_id"), clientId);
    object.insert(QStringLiteral("source"), QStringLiteral("mixxx"));
    object.insert(QStringLiteral("surface"), QStringLiteral("rest_library"));
    object.insert(QStringLiteral("role"), QStringLiteral("policy_console"));
    return object;
}

} // namespace

RestLibraryClient::RestLibraryClient(
        QNetworkAccessManager* pNetworkAccessManager,
        QObject* parent)
        : QObject(parent),
          m_pNetworkAccessManager(pNetworkAccessManager) {
    qRegisterMetaType<RestLibraryTrack>("mixxx::library::rest::RestLibraryTrack");
    qRegisterMetaType<QList<RestLibraryTrack>>("QList<mixxx::library::rest::RestLibraryTrack>");
    qRegisterMetaType<RestLibraryDiagnostics>("mixxx::library::rest::RestLibraryDiagnostics");
    qRegisterMetaType<RestLibrarySession>("mixxx::library::rest::RestLibrarySession");
    qRegisterMetaType<RestLibrarySessionWriteStatus>(
            "mixxx::library::rest::RestLibrarySessionWriteStatus");
    qRegisterMetaType<QList<RestLibraryPolicyPreset>>(
            "QList<mixxx::library::rest::RestLibraryPolicyPreset>");
    qRegisterMetaType<RestLibraryPolicyPath>("mixxx::library::rest::RestLibraryPolicyPath");
}

void RestLibraryClient::fetchTracks(const RestLibrarySettings& settings) {
    const int requestGeneration = ++m_trackListRequestGeneration;
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
    pReply->setProperty(kRequestGenerationProperty, requestGeneration);
    connect(pReply, &QNetworkReply::finished, this, &RestLibraryClient::slotTrackListFinished);
}

void RestLibraryClient::lookupTrack(
        const RestLibrarySettings& settings,
        const TrackPointer& pTrack) {
    const int requestGeneration = ++m_trackListRequestGeneration;
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
    pReply->setProperty(kRequestGenerationProperty, requestGeneration);
    connect(pReply, &QNetworkReply::finished, this, &RestLibraryClient::slotTrackListFinished);
}

void RestLibraryClient::fetchRecommendations(
        const RestLibrarySettings& settings,
        const QString& remoteId) {
    const int requestGeneration = ++m_trackListRequestGeneration;
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
    pReply->setProperty(kRequestGenerationProperty, requestGeneration);
    connect(pReply, &QNetworkReply::finished, this, &RestLibraryClient::slotTrackListFinished);
}

void RestLibraryClient::fetchMixManDiagnostics(const RestLibrarySettings& settings) {
    const int requestGeneration = ++m_mixManDiagnosticsRequestGeneration;
    m_settings = settings;
    if (!m_pNetworkAccessManager || !m_settings.isConfigured()) {
        return;
    }

    QNetworkReply* pHealthReply = m_pNetworkAccessManager->get(
            newRequest(config::mixManHealthPath(), 0));
    pHealthReply->setParent(this);
    pHealthReply->setProperty(kRequestGenerationProperty, requestGeneration);
    connect(pHealthReply, &QNetworkReply::finished, this, &RestLibraryClient::slotHealthFinished);

    QNetworkReply* pIndexReply = m_pNetworkAccessManager->get(
            newRequest(config::mixManIndexStatusPath(), 0));
    pIndexReply->setParent(this);
    pIndexReply->setProperty(kRequestGenerationProperty, requestGeneration);
    connect(pIndexReply,
            &QNetworkReply::finished,
            this,
            &RestLibraryClient::slotIndexStatusFinished);
}

void RestLibraryClient::fetchMixManPolicyPresets(const RestLibrarySettings& settings) {
    const int requestGeneration = ++m_policyPresetsRequestGeneration;
    m_settings = settings;
    if (!m_pNetworkAccessManager || !m_settings.isConfigured()) {
        emit policyPresetsFetched({});
        return;
    }

    QNetworkReply* pReply = m_pNetworkAccessManager->get(
            newRequest(config::mixManPolicyPresetsPath(), 0));
    pReply->setParent(this);
    pReply->setProperty(kRequestGenerationProperty, requestGeneration);
    connect(pReply, &QNetworkReply::finished, this, &RestLibraryClient::slotPolicyPresetsFinished);
}

void RestLibraryClient::fetchMixManPolicyPath(
        const RestLibrarySettings& settings,
        const QString& remoteId,
        const QString& sessionId,
        const QString& previousTrackId,
        const QStringList& recentTrackIds) {
    const int requestGeneration = ++m_policyPathRequestGeneration;
    clearPendingDetails();
    m_settings = settings;
    if (!m_pNetworkAccessManager) {
        emit fetchFailed(tr("Network access is not available."));
        return;
    }
    if (!m_settings.hasRecommendationsConfigured() || remoteId.trimmed().isEmpty()) {
        emit mixManPolicyPathFetched({});
        return;
    }

    const QString normalizedRemoteId = positiveIntegerString(remoteId);
    if (normalizedRemoteId.isEmpty()) {
        emit fetchFailed(tr("MixMan track IDs must be positive integers."));
        return;
    }

    QString path = pathForRemoteId(m_settings.recommendationPathTemplate, normalizedRemoteId);
    path = pathWithQueryItem(
            path,
            QStringLiteral("candidate_limit"),
            QString::number(m_settings.recommendationLimit));
    path = pathWithQueryItem(
            path,
            QStringLiteral("planning_depth"),
            QString::number(m_settings.mixManPathDepth));
    path = pathWithQueryItem(
            path,
            QStringLiteral("admin_approved_only"),
            m_settings.mixManAdminApprovedOnly ? QStringLiteral("true") : QStringLiteral("false"));
    if (!m_settings.mixManPolicyPreset.trimmed().isEmpty()) {
        path = pathWithQueryItem(
                path,
                QStringLiteral("policy_preset"),
                m_settings.mixManPolicyPreset.trimmed());
    }
    if (m_settings.mixManTargetEnergyEnabled) {
        path = pathWithQueryItem(
                path,
                QStringLiteral("target_energy"),
                QString::number(m_settings.mixManTargetEnergyNormalized(), 'f', 2));
    }
    if (m_settings.mixManTargetColorEnabled &&
            !m_settings.mixManTargetColor.trimmed().isEmpty()) {
        path = pathWithQueryItem(
                path,
                QStringLiteral("target_color"),
                m_settings.mixManTargetColor.trimmed());
    }
    if (!sessionId.trimmed().isEmpty()) {
        path = pathWithQueryItem(path, QStringLiteral("session_id"), sessionId.trimmed());
    }
    const QString normalizedPreviousTrackId = positiveIntegerString(previousTrackId);
    if (!normalizedPreviousTrackId.isEmpty()) {
        path = pathWithQueryItem(
                path,
                QStringLiteral("previous_track_id"),
                normalizedPreviousTrackId);
    }
    QStringList normalizedRecentTrackIds;
    for (const QString& recentTrackId : recentTrackIds) {
        const QString normalizedRecentTrackId = positiveIntegerString(recentTrackId);
        if (!normalizedRecentTrackId.isEmpty() &&
                !normalizedRecentTrackIds.contains(normalizedRecentTrackId)) {
            normalizedRecentTrackIds.append(normalizedRecentTrackId);
        }
    }
    if (!normalizedRecentTrackIds.isEmpty()) {
        path = pathWithQueryItem(
                path,
                QStringLiteral("recent_track_ids"),
                normalizedRecentTrackIds.join(QLatin1Char(',')));
    }

    QNetworkReply* pReply = m_pNetworkAccessManager->get(newRequest(path, 0));
    pReply->setParent(this);
    pReply->setProperty(kRequestGenerationProperty, requestGeneration);
    connect(pReply, &QNetworkReply::finished, this, &RestLibraryClient::slotPolicyPathFinished);
}

void RestLibraryClient::createMixManSession(
        const RestLibrarySettings& settings,
        const QString& clientId,
        const QJsonObject& metadata) {
    const int requestGeneration = ++m_sessionRequestGeneration;
    m_settings = settings;
    if (!m_pNetworkAccessManager || !m_settings.isConfigured()) {
        RestLibrarySessionWriteStatus status;
        status.operation = kSessionCreateOperation;
        status.errorText = tr("Session publishing is not configured.");
        emit mixManSessionWriteStatusUpdated(status);
        return;
    }

    QJsonObject payload = baseSessionClientObject(clientId);
    payload.insert(QStringLiteral("metadata"), metadata);
    QNetworkReply* pReply = m_pNetworkAccessManager->post(
            newJsonRequest(config::mixManSessionsPath()),
            jsonBody(payload));
    pReply->setParent(this);
    pReply->setProperty("operation", kSessionCreateOperation);
    pReply->setProperty(kRequestGenerationProperty, requestGeneration);
    connect(pReply, &QNetworkReply::finished, this, &RestLibraryClient::slotSessionCreateFinished);
}

void RestLibraryClient::publishMixManSessionSnapshot(
        const RestLibrarySettings& settings,
        const QString& sessionId,
        const RestLibrarySessionSnapshot& snapshot) {
    m_settings = settings;
    if (!m_pNetworkAccessManager || !m_settings.isConfigured() || sessionId.trimmed().isEmpty()) {
        RestLibrarySessionWriteStatus status;
        status.operation = kSessionSnapshotOperation;
        status.errorText = tr("MixMan session snapshot could not be published.");
        emit mixManSessionWriteStatusUpdated(status);
        return;
    }

    QJsonObject payload = baseSessionClientObject(snapshot.clientId);
    insertIfNotEmpty(&payload, QStringLiteral("surface"), snapshot.surface);
    insertIfNotEmpty(&payload, QStringLiteral("source"), snapshot.source);
    insertIntegerStringIfValid(&payload, QStringLiteral("current_track_id"), snapshot.currentTrackId);
    insertIfNotEmpty(&payload, QStringLiteral("cue"), snapshot.cue);
    insertIfNotEmpty(&payload, QStringLiteral("playback_state"), snapshot.playbackState);
    payload.insert(QStringLiteral("snapshot"), snapshot.snapshot);
    if (!snapshot.metadata.isEmpty()) {
        payload.insert(QStringLiteral("metadata"), snapshot.metadata);
    }

    QNetworkReply* pReply = m_pNetworkAccessManager->put(
            newJsonRequest(config::mixManSessionSnapshotPath(sessionId.trimmed())),
            jsonBody(payload));
    pReply->setParent(this);
    pReply->setProperty("operation", kSessionSnapshotOperation);
    pReply->setProperty(kRequestGenerationProperty, m_sessionRequestGeneration);
    connect(pReply, &QNetworkReply::finished, this, &RestLibraryClient::slotSessionWriteFinished);
}

void RestLibraryClient::updateMixManSessionIntent(
        const RestLibrarySettings& settings,
        const QString& sessionId,
        const RestLibrarySessionIntent& intent) {
    m_settings = settings;
    if (!m_pNetworkAccessManager || !m_settings.isConfigured() || sessionId.trimmed().isEmpty()) {
        RestLibrarySessionWriteStatus status;
        status.operation = kSessionIntentOperation;
        status.errorText = tr("MixMan session intent could not be updated.");
        emit mixManSessionWriteStatusUpdated(status);
        return;
    }

    QJsonObject payload = baseSessionClientObject(intent.clientId);
    payload.insert(QStringLiteral("status"), QStringLiteral("active"));
    insertIfNotEmpty(&payload, QStringLiteral("source"), intent.source);
    insertIfNotEmpty(&payload, QStringLiteral("surface"), intent.surface);
    insertIfNotEmpty(&payload, QStringLiteral("policy_preset"), intent.policyPreset);
    if (intent.targetEnergyEnabled) {
        payload.insert(QStringLiteral("target_energy"), intent.targetEnergy);
    }
    if (intent.targetColorEnabled && !intent.targetColor.trimmed().isEmpty()) {
        payload.insert(QStringLiteral("target_color"), intent.targetColor.trimmed());
    }
    if (!intent.metadata.isEmpty()) {
        payload.insert(QStringLiteral("metadata"), intent.metadata);
    }

    QNetworkReply* pReply = m_pNetworkAccessManager->put(
            newJsonRequest(config::mixManSessionIntentPath(sessionId.trimmed())),
            jsonBody(payload));
    pReply->setParent(this);
    pReply->setProperty("operation", kSessionIntentOperation);
    pReply->setProperty(kRequestGenerationProperty, m_sessionRequestGeneration);
    connect(pReply, &QNetworkReply::finished, this, &RestLibraryClient::slotSessionWriteFinished);
}

void RestLibraryClient::sendMixManSessionHeartbeat(
        const RestLibrarySettings& settings,
        const QString& sessionId,
        const QString& clientId,
        const QJsonObject& metadata) {
    m_settings = settings;
    if (!m_pNetworkAccessManager || !m_settings.isConfigured() || sessionId.trimmed().isEmpty()) {
        return;
    }

    QJsonObject payload = baseSessionClientObject(clientId);
    payload.insert(QStringLiteral("status"), QStringLiteral("active"));
    if (!metadata.isEmpty()) {
        payload.insert(QStringLiteral("metadata"), metadata);
    }
    QNetworkReply* pReply = m_pNetworkAccessManager->post(
            newJsonRequest(config::mixManSessionHeartbeatPath(sessionId.trimmed())),
            jsonBody(payload));
    pReply->setParent(this);
    pReply->setProperty("operation", kSessionHeartbeatOperation);
    pReply->setProperty(kRequestGenerationProperty, m_sessionRequestGeneration);
    connect(pReply, &QNetworkReply::finished, this, &RestLibraryClient::slotSessionWriteFinished);
}

void RestLibraryClient::invalidateMixManRequests() {
    ++m_mixManDiagnosticsRequestGeneration;
    ++m_policyPresetsRequestGeneration;
    ++m_policyPathRequestGeneration;
    ++m_sessionRequestGeneration;
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

QNetworkRequest RestLibraryClient::newJsonRequest(const QString& path) const {
    QNetworkRequest request = newRequest(path, 0);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
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
    const bool staleReply =
            pReply->property(kRequestGenerationProperty).toInt() !=
            m_trackListRequestGeneration;
    pReply->deleteLater();
    if (staleReply) {
        return;
    }

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
        pReply->setProperty(kRequestGenerationProperty, m_trackListRequestGeneration);
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
    const bool staleReply =
            pReply->property(kRequestGenerationProperty).toInt() !=
            m_trackListRequestGeneration;
    pReply->deleteLater();
    if (staleReply) {
        return;
    }
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

void RestLibraryClient::slotHealthFinished() {
    auto* pReply = qobject_cast<QNetworkReply*>(sender());
    if (!pReply) {
        return;
    }
    const bool staleReply =
            pReply->property(kRequestGenerationProperty).toInt() !=
            m_mixManDiagnosticsRequestGeneration;
    pReply->deleteLater();
    if (staleReply) {
        return;
    }

    RestLibraryDiagnostics diagnostics;
    diagnostics.healthKnown = true;
    diagnostics.lastStatusCode = statusCodeFromReply(*pReply);
    diagnostics.healthOk = isSuccessStatus(diagnostics.lastStatusCode);
    if (!diagnostics.healthOk) {
        diagnostics.lastError = tr("MixMan health check failed.");
    }
    emit diagnosticsUpdated(diagnostics);
}

void RestLibraryClient::slotIndexStatusFinished() {
    auto* pReply = qobject_cast<QNetworkReply*>(sender());
    if (!pReply) {
        return;
    }
    const bool staleReply =
            pReply->property(kRequestGenerationProperty).toInt() !=
            m_mixManDiagnosticsRequestGeneration;
    pReply->deleteLater();
    if (staleReply) {
        return;
    }

    const QByteArray responseBody = pReply->readAll();
    RestLibraryDiagnostics diagnostics;
    diagnostics.indexKnown = true;
    diagnostics.lastStatusCode = statusCodeFromReply(*pReply);
    if (!isSuccessStatus(diagnostics.lastStatusCode)) {
        diagnostics.lastError = tr("MixMan index status request failed.");
        emit diagnosticsUpdated(diagnostics);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(responseBody, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        diagnostics.lastError = tr("MixMan index status was not valid JSON.");
        emit diagnosticsUpdated(diagnostics);
        return;
    }

    diagnostics = parseIndexStatusDocument(document);
    diagnostics.lastStatusCode = statusCodeFromReply(*pReply);
    emit diagnosticsUpdated(diagnostics);
}

void RestLibraryClient::slotPolicyPresetsFinished() {
    auto* pReply = qobject_cast<QNetworkReply*>(sender());
    if (!pReply) {
        return;
    }
    const bool staleReply =
            pReply->property(kRequestGenerationProperty).toInt() !=
            m_policyPresetsRequestGeneration;
    pReply->deleteLater();
    if (staleReply) {
        return;
    }

    const QByteArray responseBody = pReply->readAll();
    const int statusCode = statusCodeFromReply(*pReply);
    if (!isSuccessStatus(statusCode)) {
        emit policyPresetsFetched({});
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(responseBody, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        emit policyPresetsFetched({});
        return;
    }
    emit policyPresetsFetched(parsePolicyPresetsDocument(document));
}

void RestLibraryClient::slotPolicyPathFinished() {
    auto* pReply = qobject_cast<QNetworkReply*>(sender());
    if (!pReply) {
        emit fetchFailed(tr("MixMan policy path request failed."));
        return;
    }
    const bool staleReply =
            pReply->property(kRequestGenerationProperty).toInt() !=
            m_policyPathRequestGeneration;
    pReply->deleteLater();
    if (staleReply) {
        return;
    }

    const QByteArray responseBody = pReply->readAll();
    const int statusCode = statusCodeFromReply(*pReply);
    if (!isSuccessStatus(statusCode)) {
        kLogger.warning()
                << "MixMan policy path request failed"
                << pReply->request().url().toString(QUrl::RemoveUserInfo)
                << "status" << statusCode
                << "body" << responseSnippet(responseBody);
        emit fetchFailed(tr("MixMan policy path request returned an unsuccessful status."));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(responseBody, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        emit fetchFailed(tr("MixMan policy path response was not valid JSON."));
        return;
    }
    emit mixManPolicyPathFetched(parsePolicyPathDocument(document));
}

void RestLibraryClient::slotSessionCreateFinished() {
    auto* pReply = qobject_cast<QNetworkReply*>(sender());
    if (!pReply) {
        return;
    }
    const bool staleReply =
            pReply->property(kRequestGenerationProperty).toInt() !=
            m_sessionRequestGeneration;
    pReply->deleteLater();
    if (staleReply) {
        return;
    }

    const QByteArray responseBody = pReply->readAll();
    RestLibrarySessionWriteStatus status;
    status.operation = kSessionCreateOperation;
    status.statusCode = statusCodeFromReply(*pReply);
    status.success = isSuccessStatus(status.statusCode);
    if (!status.success) {
        status.errorText = tr("MixMan session creation failed.");
        kLogger.warning()
                << "MixMan session creation failed"
                << pReply->request().url().toString(QUrl::RemoveUserInfo)
                << "status" << status.statusCode
                << "body" << responseSnippet(responseBody);
        emit mixManSessionWriteStatusUpdated(status);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(responseBody, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        status.success = false;
        status.errorText = tr("MixMan session response was not valid JSON.");
        emit mixManSessionWriteStatusUpdated(status);
        return;
    }

    RestLibrarySession session = parseSessionDocument(document);
    if (session.id.isEmpty()) {
        status.success = false;
        status.errorText = tr("MixMan session response did not include a session ID.");
        emit mixManSessionWriteStatusUpdated(status);
        return;
    }

    emit mixManSessionCreated(session);
    emit mixManSessionWriteStatusUpdated(status);
}

void RestLibraryClient::slotSessionWriteFinished() {
    auto* pReply = qobject_cast<QNetworkReply*>(sender());
    if (!pReply) {
        return;
    }
    const bool staleReply =
            pReply->property(kRequestGenerationProperty).toInt() !=
            m_sessionRequestGeneration;
    pReply->deleteLater();
    if (staleReply) {
        return;
    }

    const QByteArray responseBody = pReply->readAll();
    RestLibrarySessionWriteStatus status;
    status.operation = pReply->property("operation").toString();
    status.statusCode = statusCodeFromReply(*pReply);
    status.success = isSuccessStatus(status.statusCode);
    if (!status.success) {
        status.errorText = tr("MixMan session write failed.");
        kLogger.warning()
                << "MixMan session write failed"
                << status.operation
                << pReply->request().url().toString(QUrl::RemoveUserInfo)
                << "status" << status.statusCode
                << "body" << responseSnippet(responseBody);
    }
    emit mixManSessionWriteStatusUpdated(status);
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

RestLibraryPolicyPath RestLibraryClient::parsePolicyPathDocumentForTesting(
        const QJsonDocument& document) {
    return parsePolicyPathDocument(document);
}

QList<RestLibraryPolicyPreset> RestLibraryClient::parsePolicyPresetsDocumentForTesting(
        const QJsonDocument& document) {
    return parsePolicyPresetsDocument(document);
}

RestLibraryDiagnostics RestLibraryClient::parseIndexStatusDocumentForTesting(
        const QJsonDocument& document) {
    return parseIndexStatusDocument(document);
}

RestLibrarySession RestLibraryClient::parseSessionDocumentForTesting(
        const QJsonDocument& document) {
    return parseSessionDocument(document);
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
    const QJsonObject metadata = object.value(QStringLiteral("metadata")).toObject();
    track.remoteId = readString(object, {"id", "track_id"});
    track.reviewId = readString(object, {"review_id", "hash_id"});
    track.title = readString(object, {"title", "name", "label"});
    track.artist = readString(object, {"artist", "artists"});
    track.album = readString(object, {"album"});
    track.genre = readString(object, {"genre"});
    track.composer = readString(object, {"composer"});
    track.comment = readString(object, {"comment"});
    track.keyText = readString(object, {"key", "musical_key"});
    track.trackNumber = readString(object, {"track_number", "tracknumber"});
    track.label = readString(object, {"label"});
    track.sourceLabel = readString(object, {"source", "mode"});
    track.audioFileExtension = readString(
            object,
            {"extension", "file_extension", "audio_extension", "download_file_extension"});
    track.bpm = readDouble(object, {"bpm"});
    if (track.bpm <= 0.0) {
        track.bpm = readDouble(metadata, {"bpm"});
    }
    track.durationSeconds = readDouble(object, {"duration", "duration_seconds"});
    if (track.durationSeconds <= 0.0) {
        track.durationSeconds = readDouble(metadata, {"duration", "duration_seconds"});
    }
    track.rating = readRating(object);
    if (track.rating <= 0) {
        track.rating = readRating(metadata);
    }
    if (track.keyText.isEmpty()) {
        track.keyText = readString(metadata, {"key", "musical_key"});
    }
    if (track.genre.isEmpty()) {
        track.genre = readString(metadata, {"genre"});
    }
    if (track.audioFileExtension.isEmpty()) {
        track.audioFileExtension = readString(metadata, {"download_file_extension"});
    }
    track.quality = readDouble(object, {"quality", "quality_score"});
    if (track.quality <= 0.0) {
        track.quality = readDouble(metadata, {"quality", "quality_score"});
    }
    track.score = readDouble(object, {"score"});
    track.mode = readString(object, {"mode", "map_mode"});
    if (track.mode.isEmpty()) {
        track.mode = readString(metadata, {"mode", "map_mode"});
    }
    track.fallbackMode = readString(object, {"fallback_mode"});
    if (track.fallbackMode.isEmpty()) {
        track.fallbackMode = readString(metadata, {"fallback_mode"});
    }
    track.moveType = readString(object, {"resolved_move_type", "move_type"});
    track.color = readString(object, {"color", "colour"});
    track.region = readString(object, {"region", "region_id"});

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

RestLibraryPolicyPath RestLibraryClient::parsePolicyPathDocument(const QJsonDocument& document) {
    RestLibraryPolicyPath result;
    if (!document.isObject()) {
        return result;
    }

    const QJsonObject root = document.object();
    const QJsonObject tracksById = root.value(QStringLiteral("tracks_by_id")).toObject();
    const QJsonObject plan = root.value(QStringLiteral("plan")).toObject();
    const QJsonObject path = root.value(QStringLiteral("path")).toObject();
    const QJsonObject pathSource = !path.isEmpty() ? path : plan;
    result.policyPreset = readString(root, {"policy_preset"});
    result.resolvedMoveType = readString(root, {"resolved_move_type"});
    result.recommendationEventId =
            static_cast<int>(readDouble(root, {"recommendation_event_id"}));
    result.selectedBranchScore = readDouble(pathSource, {"selected_branch_score"});

    const QJsonArray alternatives = plan.value(QStringLiteral("alternatives")).toArray();
    result.candidates.reserve(alternatives.size());
    for (const QJsonValue& value : alternatives) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject recommendation = value.toObject();
        const QString remoteId = readString(recommendation, {"id", "track_id"});
        RestLibraryTrack track = parseTrackObject(objectForTrackId(tracksById, remoteId));
        if (track.remoteId.isEmpty()) {
            track.remoteId = remoteId;
        }
        if (track.remoteId.isEmpty()) {
            continue;
        }
        if (track.title.isEmpty()) {
            track.title = readString(recommendation, {"title", "label"});
        }
        if (track.artist.isEmpty()) {
            track.artist = readString(recommendation, {"artist"});
        }
        track.score = readDouble(recommendation, {"score"});
        track.quality = track.score;
        track.recommendationEventId =
                static_cast<int>(readDouble(recommendation, {"recommendation_event_id"}));
        track.recommendationItemId =
                static_cast<int>(readDouble(recommendation, {"recommendation_item_id"}));
        track.recommendationPosition =
                static_cast<int>(readDouble(recommendation, {"position"}));
        track.planned = recommendation.value(QStringLiteral("planned")).toBool(false);
        track.moveType = readString(recommendation, {"resolved_move_type", "move_type"});
        track.transitionRisk = readDouble(recommendation, {"transition_risk"});
        track.targetImprovement = readDouble(recommendation, {"target_distance_improvement"});
        track.targetDistance = readDouble(recommendation, {"target_distance_after_candidate"});
        track.region = readString(recommendation, {"region_id"});
        track.reasonCodes = readStringArray(recommendation, QStringLiteral("reason_codes"));

        const QJsonObject features =
                recommendation.value(QStringLiteral("candidate_features")).toObject();
        if (track.transitionRisk <= 0.0) {
            track.transitionRisk = readDouble(features, {"transition_risk"});
        }
        track.transitionFit = readDouble(features, {"transition_fit"});
        if (track.targetDistance <= 0.0) {
            track.targetDistance = readDouble(features, {"target_distance"});
        }
        if (track.targetImprovement == 0.0) {
            track.targetImprovement = readDouble(features, {"target_improvement"});
        }
        if (track.color.isEmpty()) {
            const QJsonObject lighting =
                    recommendation.value(QStringLiteral("lighting_payload")).toObject();
            track.color = readString(lighting, {"next_track_color"});
        }
        if (track.sourceLabel.isEmpty()) {
            track.sourceLabel = QStringLiteral("MixMan Policy");
        }
        result.candidates.append(std::move(track));
    }

    const QJsonArray steps = pathSource.value(QStringLiteral("steps")).toArray();
    result.path.reserve(steps.size());
    for (const QJsonValue& value : steps) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject stepObject = value.toObject();
        const QString remoteId = readString(stepObject, {"id", "track_id"});
        const RestLibraryTrack track = parseTrackObject(objectForTrackId(tracksById, remoteId));
        RestLibraryPathStep step;
        step.remoteId = track.remoteId.isEmpty() ? remoteId : track.remoteId;
        step.title = track.title;
        step.artist = track.artist;
        step.score = readDouble(stepObject, {"score"});
        step.position = static_cast<int>(readDouble(stepObject, {"position"}));
        step.moveType = readString(stepObject, {"resolved_move_type", "move_type"});
        step.color = track.color;
        if (step.color.isEmpty()) {
            const QJsonObject lighting = stepObject.value(QStringLiteral("lighting_payload")).toObject();
            step.color = readString(lighting, {"next_track_color"});
        }
        step.region = readString(stepObject, {"region_id"});
        if (!step.remoteId.isEmpty()) {
            result.path.append(std::move(step));
        }
    }

    return result;
}

QList<RestLibraryPolicyPreset> RestLibraryClient::parsePolicyPresetsDocument(
        const QJsonDocument& document) {
    QList<RestLibraryPolicyPreset> result;
    if (!document.isArray()) {
        return result;
    }
    const QJsonArray presets = document.array();
    result.reserve(presets.size());
    for (const QJsonValue& value : presets) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        RestLibraryPolicyPreset preset;
        preset.key = readString(object, {"key"});
        preset.label = readString(object, {"label"});
        preset.description = readString(object, {"description"});
        if (!preset.key.isEmpty()) {
            result.append(std::move(preset));
        }
    }
    return result;
}

RestLibraryDiagnostics RestLibraryClient::parseIndexStatusDocument(const QJsonDocument& document) {
    RestLibraryDiagnostics diagnostics;
    diagnostics.indexKnown = true;
    if (!document.isObject()) {
        diagnostics.lastError = QObject::tr("MixMan index status was not a JSON object.");
        return diagnostics;
    }
    const QJsonObject object = document.object();
    diagnostics.indexReady = object.value(QStringLiteral("ready")).toBool(false);
    diagnostics.indexCount = static_cast<int>(readDouble(object, {"count"}));
    diagnostics.indexDimension = static_cast<int>(readDouble(object, {"dim"}));
    return diagnostics;
}

RestLibrarySession RestLibraryClient::parseSessionDocument(const QJsonDocument& document) {
    RestLibrarySession session;
    if (!document.isObject()) {
        return session;
    }
    const QJsonObject root = document.object();
    const QJsonObject sessionObject = root.value(QStringLiteral("session")).isObject()
            ? root.value(QStringLiteral("session")).toObject()
            : root;
    session.id = readString(sessionObject, {"id", "session_id"});
    session.displayName = readString(sessionObject, {"display_name", "name"});
    session.status = readString(sessionObject, {"status"});
    if (root.value(QStringLiteral("snapshot")).isObject()) {
        session.snapshot = root.value(QStringLiteral("snapshot")).toObject();
    }
    if (root.value(QStringLiteral("intent")).isObject()) {
        session.intent = root.value(QStringLiteral("intent")).toObject();
    }
    if (root.value(QStringLiteral("policy_event")).isObject()) {
        session.policyEvent = root.value(QStringLiteral("policy_event")).toObject();
    }
    if (root.value(QStringLiteral("clients")).isArray()) {
        session.clients = root.value(QStringLiteral("clients")).toArray();
    }
    if (root.value(QStringLiteral("recent_events")).isArray()) {
        session.recentEvents = root.value(QStringLiteral("recent_events")).toArray();
    }
    return session;
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
