#include "library/rest/restlibraryclient.h"

#include <algorithm>
#include <cmath>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDateTime>
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
constexpr qint64 kMaxMetadataResponseBytes = 4 * 1024 * 1024;
constexpr qsizetype kMaxLoggedResponseBytes = 500;
const QString kSessionCreateOperation = QStringLiteral("session_create");
const QString kSessionRegisterOperation = QStringLiteral("session_register");
const QString kSessionDisconnectOperation = QStringLiteral("session_disconnect");
const QString kSessionFetchOperation = QStringLiteral("session_fetch");
const QString kSessionSnapshotOperation = QStringLiteral("session_snapshot");
const QString kSessionIntentOperation = QStringLiteral("session_intent");
const QString kSessionHeartbeatOperation = QStringLiteral("session_heartbeat");
const QString kSessionPlaybackOperation = QStringLiteral("session_playback");
const QString kSessionPlaybackControlClaimOperation =
        QStringLiteral("session_playback_control_claim");
const QString kSessionPlaybackControlRenewOperation =
        QStringLiteral("session_playback_control_renew");
const QString kSessionPlaybackControlReleaseOperation =
        QStringLiteral("session_playback_control_release");
const QString kSessionCandidateSelectOperation = QStringLiteral("session_candidate_select");
const QString kSessionPolicyRefreshOperation = QStringLiteral("session_policy_refresh");
const char* kRequestGenerationProperty = "requestGeneration";
const char* kRequestStartedAtProperty = "requestStartedAt";
const char* kRequestStageProperty = "requestStage";
const char* kAuthoritativeGenerationProperty = "authoritativeGeneration";
const char* kRequestMethodProperty = "requestMethod";
const char* kMutationSequenceProperty = "mutationSequence";
const char* kResponseTooLargeProperty = "restLibraryResponseTooLarge";

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

QString valueToDiagnosticString(const QJsonValue& value) {
    if (value.isString()) {
        return value.toString();
    }
    if (value.isDouble() || value.isBool()) {
        if (value.isBool()) {
            return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
        }
        return valueToString(value);
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
            const QString text = valueToDiagnosticString(object.value(key));
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
        const QString text = valueToDiagnosticString(object.value(key));
        if (!text.isEmpty()) {
            return text;
        }
    }
    return {};
}

QString errorReasonFromResponse(const QByteArray& body) {
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject()) {
        return {};
    }
    const QJsonValue detail = document.object().value(QStringLiteral("detail"));
    return detail.isObject()
            ? detail.toObject().value(QStringLiteral("reason")).toString().trimmed()
            : QString();
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

} // namespace

RestLibraryClient::RestLibraryClient(
        QNetworkAccessManager* pNetworkAccessManager,
        QObject* parent)
        : QObject(parent),
          m_pNetworkAccessManager(pNetworkAccessManager) {
    qRegisterMetaType<RestLibraryTrack>("mixxx::library::rest::RestLibraryTrack");
    qRegisterMetaType<QList<RestLibraryTrack>>("QList<mixxx::library::rest::RestLibraryTrack>");
    qRegisterMetaType<RestLibraryDiagnostics>("mixxx::library::rest::RestLibraryDiagnostics");
    qRegisterMetaType<RestLibraryRequestDiagnostic>(
            "mixxx::library::rest::RestLibraryRequestDiagnostic");
    qRegisterMetaType<RestLibrarySession>("mixxx::library::rest::RestLibrarySession");
    qRegisterMetaType<RestLibrarySessionWriteStatus>(
            "mixxx::library::rest::RestLibrarySessionWriteStatus");
    qRegisterMetaType<RestLibrarySessionContract>(
            "mixxx::library::rest::RestLibrarySessionContract");
    qRegisterMetaType<RestLibrarySessionRegistration>(
            "mixxx::library::rest::RestLibrarySessionRegistration");
    qRegisterMetaType<QList<RestLibraryPolicyPreset>>(
            "QList<mixxx::library::rest::RestLibraryPolicyPreset>");
    qRegisterMetaType<RestLibraryCatalogPage>(
            "mixxx::library::rest::RestLibraryCatalogPage");
    qRegisterMetaType<RestLibraryPolicyPath>("mixxx::library::rest::RestLibraryPolicyPath");
}

void RestLibraryClient::fetchTrackCatalogPage(
        const RestLibrarySettings& settings,
        const QString& cursor) {
    cancelTrackCatalogRequest();
    const int requestGeneration = ++m_trackCatalogRequestGeneration;
    m_settings = settings;
    if (!m_pNetworkAccessManager || !settings.isConfigured()) {
        emit trackCatalogFetchFailed(tr("REST Library is not configured."));
        return;
    }

    QNetworkRequest request = newRequest(settings, settings.trackListPath, 0);
    QUrl url = request.url();
    QUrlQuery query(url);
    query.removeAllQueryItems(QStringLiteral("include_details"));
    query.addQueryItem(QStringLiteral("include_details"), QStringLiteral("true"));
    query.removeAllQueryItems(QStringLiteral("limit"));
    query.addQueryItem(QStringLiteral("limit"), QString::number(settings.pageSize));
    query.removeAllQueryItems(QStringLiteral("offset"));
    query.removeAllQueryItems(QStringLiteral("cursor"));
    if (!cursor.isEmpty()) {
        query.addQueryItem(QStringLiteral("cursor"), cursor);
    }
    url.setQuery(query);

    request.setUrl(url);
    QNetworkReply* pReply = m_pNetworkAccessManager->get(request);
    monitorMetadataReply(pReply);
    pReply->setProperty(kRequestGenerationProperty, requestGeneration);
    pReply->setProperty(kRequestStartedAtProperty, QDateTime::currentMSecsSinceEpoch());
    m_pTrackCatalogReply = pReply;
    connect(pReply,
            &QNetworkReply::finished,
            this,
            &RestLibraryClient::slotTrackCatalogFinished);
}

void RestLibraryClient::cancelTrackCatalogRequest() {
    ++m_trackCatalogRequestGeneration;
    if (m_pTrackCatalogReply) {
        m_metadataResponseBodies.remove(m_pTrackCatalogReply);
        disconnect(m_pTrackCatalogReply, nullptr, this, nullptr);
        m_pTrackCatalogReply->abort();
        m_pTrackCatalogReply->deleteLater();
        m_pTrackCatalogReply.clear();
    }
}

void RestLibraryClient::fetchTracks(const RestLibrarySettings& settings) {
    const int requestGeneration = ++m_trackListRequestGeneration;
    clearPendingDetails();
    m_settings = settings;

    if (!m_pNetworkAccessManager) {
        emit fetchFailed(tr("Network access is not available."));
        return;
    }
    if (!m_settings.isConfigured()) {
        emit tracksFetched({});
        return;
    }

    m_trackBatches.insert(requestGeneration, TrackRequestBatch{
            m_settings,
            RequestPurpose::Tracks,
            {},
            {},
            0,
            false});
    QNetworkReply* pReply = m_pNetworkAccessManager->get(
            newRequest(m_settings, m_settings.trackListPath, m_settings.pageSize));
    monitorMetadataReply(pReply);
    pReply->setProperty(kRequestGenerationProperty, requestGeneration);
    pReply->setProperty(kRequestStartedAtProperty, QDateTime::currentMSecsSinceEpoch());
    m_requestContexts.insert(pReply, RequestContext{
            m_settings,
            RequestPurpose::Tracks,
            requestGeneration,
            tr("Track list"),
            QStringLiteral("GET"),
            {}});
    connect(pReply, &QNetworkReply::finished, this, &RestLibraryClient::slotTrackListFinished);
}

void RestLibraryClient::lookupTrack(
        const RestLibrarySettings& settings,
        const TrackPointer& pTrack) {
    const int requestGeneration = ++m_trackListRequestGeneration;
    clearPendingDetails();
    m_settings = settings;

    if (!m_pNetworkAccessManager) {
        emit trackLookupMissed(tr("Network access is not available."));
        return;
    }
    if (!m_settings.hasTrackLookupConfigured() || !pTrack) {
        emit trackLookupMissed(tr("Remote track lookup is not configured."));
        return;
    }

    m_trackBatches.insert(requestGeneration, TrackRequestBatch{
            m_settings,
            RequestPurpose::TrackLookup,
            {},
            {},
            0,
            false});
    QNetworkReply* pReply = m_pNetworkAccessManager->get(
            newRequest(
                    m_settings,
                    pathForTrackLookup(m_settings.trackLookupPathTemplate, pTrack),
                    1));
    monitorMetadataReply(pReply);
    pReply->setProperty(kRequestGenerationProperty, requestGeneration);
    pReply->setProperty(kRequestStartedAtProperty, QDateTime::currentMSecsSinceEpoch());
    m_requestContexts.insert(pReply, RequestContext{
            m_settings,
            RequestPurpose::TrackLookup,
            requestGeneration,
            tr("Track lookup"),
            QStringLiteral("GET"),
            {}});
    connect(pReply, &QNetworkReply::finished, this, &RestLibraryClient::slotTrackListFinished);
}

void RestLibraryClient::fetchRecommendations(
        const RestLibrarySettings& settings,
        const QString& remoteId) {
    const int requestGeneration = ++m_trackListRequestGeneration;
    clearPendingDetails();
    m_settings = settings;

    if (!m_pNetworkAccessManager) {
        emit fetchFailed(tr("Network access is not available."));
        return;
    }
    if (!m_settings.hasRecommendationsConfigured() || remoteId.trimmed().isEmpty()) {
        emit recommendationsFetched({});
        return;
    }

    m_trackBatches.insert(requestGeneration, TrackRequestBatch{
            m_settings,
            RequestPurpose::Recommendations,
            {},
            {},
            0,
            false});
    QNetworkReply* pReply = m_pNetworkAccessManager->get(
            newRequest(
                    m_settings,
                    pathForRemoteId(m_settings.recommendationPathTemplate, remoteId),
                    m_settings.recommendationLimit));
    monitorMetadataReply(pReply);
    pReply->setProperty(kRequestGenerationProperty, requestGeneration);
    pReply->setProperty(kRequestStartedAtProperty, QDateTime::currentMSecsSinceEpoch());
    m_requestContexts.insert(pReply, RequestContext{
            m_settings,
            RequestPurpose::Recommendations,
            requestGeneration,
            tr("Recommendations"),
            QStringLiteral("GET"),
            remoteId});
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
    monitorMetadataReply(pHealthReply);
    pHealthReply->setProperty(kRequestGenerationProperty, requestGeneration);
    pHealthReply->setProperty(kRequestStartedAtProperty, QDateTime::currentMSecsSinceEpoch());
    connect(pHealthReply, &QNetworkReply::finished, this, &RestLibraryClient::slotHealthFinished);

    QNetworkReply* pIndexReply = m_pNetworkAccessManager->get(
            newRequest(config::mixManIndexStatusPath(), 0));
    monitorMetadataReply(pIndexReply);
    pIndexReply->setProperty(kRequestGenerationProperty, requestGeneration);
    pIndexReply->setProperty(kRequestStartedAtProperty, QDateTime::currentMSecsSinceEpoch());
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
    monitorMetadataReply(pReply);
    pReply->setProperty(kRequestGenerationProperty, requestGeneration);
    pReply->setProperty(kRequestStartedAtProperty, QDateTime::currentMSecsSinceEpoch());
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
    if (m_settings.mixManTargetBpmEnabled) {
        path = pathWithQueryItem(
                path,
                QStringLiteral("target_bpm"),
                QString::number(m_settings.mixManTargetBpm));
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
    monitorMetadataReply(pReply);
    pReply->setProperty(kRequestGenerationProperty, requestGeneration);
    pReply->setProperty(kRequestStartedAtProperty, QDateTime::currentMSecsSinceEpoch());
    connect(pReply, &QNetworkReply::finished, this, &RestLibraryClient::slotPolicyPathFinished);
}

void RestLibraryClient::createMixManSession(
        const RestLibrarySettings& settings,
        const QString& sessionId,
        const QJsonObject& metadata) {
    const int requestGeneration = ++m_sessionRequestGeneration;
    m_settings = settings;
    m_pendingSessionId = sessionId.trimmed();
    m_pendingSessionMetadata = metadata;
    m_pendingResumeCredentials = {};
    m_pendingFreshRegistrationRetried = false;
    m_pendingCreateAttempted = false;
    if (!m_pNetworkAccessManager || !m_settings.isConfigured() || m_pendingSessionId.isEmpty()) {
        RestLibrarySessionWriteStatus status;
        status.operation = kSessionCreateOperation;
        status.errorText = tr("Session publishing is not configured.");
        emit mixManSessionWriteStatusUpdated(status);
        return;
    }

    QNetworkReply* pReply = m_pNetworkAccessManager->get(
            newJsonRequest(config::mixManConfigPath()));
    monitorMetadataReply(pReply);
    pReply->setProperty(kRequestGenerationProperty, requestGeneration);
    pReply->setProperty(kRequestStartedAtProperty, QDateTime::currentMSecsSinceEpoch());
    connect(pReply,
            &QNetworkReply::finished,
            this,
            &RestLibraryClient::slotSessionContractFinished);
}

void RestLibraryClient::registerMixManSessionInstance(
        const RestLibrarySettings& settings,
        const QString& sessionId,
        const RestLibrarySessionCredentials& credentials,
        const QJsonObject& metadata) {
    ++m_sessionRequestGeneration;
    m_settings = settings;
    m_pendingSessionId = sessionId.trimmed();
    m_pendingSessionMetadata = metadata;
    m_pendingResumeCredentials = credentials;
    m_pendingFreshRegistrationRetried = false;
    m_pendingCreateAttempted = false;
    if (!m_pNetworkAccessManager || !m_settings.isConfigured() ||
            m_pendingSessionId.isEmpty()) {
        RestLibrarySessionWriteStatus status;
        status.operation = kSessionRegisterOperation;
        status.errorText = tr("Session registration is not configured.");
        emit mixManSessionWriteStatusUpdated(status);
        return;
    }
    startMixManInstanceRegistration(
            m_pendingSessionId,
            m_pendingResumeCredentials,
            m_pendingSessionMetadata);
}

void RestLibraryClient::startMixManInstanceRegistration(
        const QString& sessionId,
        const RestLibrarySessionCredentials& credentials,
        const QJsonObject& metadata) {
    if (!m_pNetworkAccessManager || sessionId.trimmed().isEmpty()) {
        return;
    }
    QJsonObject payload{
            {QStringLiteral("application"), QStringLiteral("mixxx")},
            {QStringLiteral("surface"), QStringLiteral("rest_library")},
    };
    if (credentials.isComplete()) {
        payload.insert(QStringLiteral("resume_instance_id"), credentials.instanceId);
        payload.insert(QStringLiteral("resume_token"), credentials.resumeToken);
    }
    if (!metadata.isEmpty()) {
        payload.insert(QStringLiteral("metadata"), metadata);
    }
    QNetworkReply* pReply = m_pNetworkAccessManager->post(
            newJsonRequest(config::mixManSessionInstancesPath(sessionId.trimmed())),
            jsonBody(payload));
    monitorMetadataReply(pReply);
    pReply->setProperty("operation", kSessionRegisterOperation);
    pReply->setProperty(kRequestMethodProperty, QStringLiteral("POST"));
    pReply->setProperty(kRequestGenerationProperty, m_sessionRequestGeneration);
    pReply->setProperty(kRequestStartedAtProperty, QDateTime::currentMSecsSinceEpoch());
    connect(pReply, &QNetworkReply::finished, this, &RestLibraryClient::slotSessionRegisterFinished);
}

void RestLibraryClient::startMixManSessionCreate(
        const QString& sessionId,
        const QJsonObject& metadata) {
    m_pendingCreateAttempted = true;
    QJsonObject payload{{QStringLiteral("session_id"), sessionId.trimmed()}};
    if (!metadata.isEmpty()) {
        payload.insert(QStringLiteral("metadata"), metadata);
    }
    QNetworkReply* pReply = m_pNetworkAccessManager->post(
            newJsonRequest(config::mixManSessionsPath()), jsonBody(payload));
    monitorMetadataReply(pReply);
    pReply->setProperty("operation", kSessionCreateOperation);
    pReply->setProperty(kRequestMethodProperty, QStringLiteral("POST"));
    pReply->setProperty(kRequestGenerationProperty, m_sessionRequestGeneration);
    pReply->setProperty(kRequestStartedAtProperty, QDateTime::currentMSecsSinceEpoch());
    connect(pReply, &QNetworkReply::finished, this, &RestLibraryClient::slotSessionCreateFinished);
}

QNetworkReply* RestLibraryClient::disconnectMixManSessionInstance(
        const RestLibrarySettings& settings,
        const QString& sessionId,
        const QString& instanceId) {
    m_settings = settings;
    if (!m_pNetworkAccessManager || sessionId.trimmed().isEmpty() || instanceId.trimmed().isEmpty()) {
        return nullptr;
    }
    QNetworkReply* pReply = m_pNetworkAccessManager->post(
            newJsonRequest(config::mixManSessionInstanceDisconnectPath(
                    sessionId.trimmed(), instanceId.trimmed())),
            QByteArrayLiteral("{}"));
    monitorMetadataReply(pReply);
    pReply->setProperty("operation", kSessionDisconnectOperation);
    pReply->setProperty(kRequestMethodProperty, QStringLiteral("POST"));
    pReply->setProperty(kRequestGenerationProperty, m_sessionRequestGeneration);
    pReply->setProperty(kRequestStartedAtProperty, QDateTime::currentMSecsSinceEpoch());
    connect(pReply, &QNetworkReply::finished, this, &RestLibraryClient::slotSessionWriteFinished);
    return pReply;
}

void RestLibraryClient::fetchMixManSession(
        const RestLibrarySettings& settings,
        const QString& sessionId,
        const QString& instanceId) {
    m_settings = settings;
    if (!m_pNetworkAccessManager || !m_settings.isConfigured() || sessionId.trimmed().isEmpty() ||
            instanceId.trimmed().isEmpty()) {
        emit mixManSessionFetched({});
        return;
    }

    const int authoritativeGeneration = ++m_sessionAuthoritativeGeneration;
    QNetworkReply* pReply = m_pNetworkAccessManager->get(
            newJsonRequest(config::mixManSessionStatePath(
                    sessionId.trimmed(), instanceId.trimmed())));
    monitorMetadataReply(pReply);
    pReply->setProperty(kRequestGenerationProperty, m_sessionRequestGeneration);
    pReply->setProperty(kAuthoritativeGenerationProperty, authoritativeGeneration);
    pReply->setProperty(kRequestStartedAtProperty, QDateTime::currentMSecsSinceEpoch());
    connect(pReply, &QNetworkReply::finished, this, &RestLibraryClient::slotSessionFetchFinished);
}

void RestLibraryClient::publishMixManSessionPlayback(
        const RestLibrarySettings& settings,
        const QString& sessionId,
        const RestLibrarySessionPlayback& playback,
        quint64 mutationSequence) {
    m_settings = settings;
    if (!m_pNetworkAccessManager || !m_settings.isConfigured() || sessionId.trimmed().isEmpty()) {
        RestLibrarySessionWriteStatus status;
        status.operation = kSessionPlaybackOperation;
        status.mutationSequence = mutationSequence;
        status.errorText = tr("MixMan session playback could not be published.");
        emit mixManSessionWriteStatusUpdated(status);
        return;
    }

    QJsonObject payload{
            {QStringLiteral("instance_id"), playback.lease.instanceId},
            {QStringLiteral("lease_id"), playback.lease.leaseId},
            {QStringLiteral("lease_generation"), playback.lease.generation},
    };
    insertIntegerStringIfValid(
            &payload,
            QStringLiteral("current_track_id"),
            playback.currentTrackId);
    insertIfNotEmpty(&payload, QStringLiteral("cue"), playback.cue);
    insertIfNotEmpty(&payload, QStringLiteral("playback_state"), playback.playbackState);
    if (!playback.currentTrack.isEmpty()) {
        payload.insert(QStringLiteral("current_track"), playback.currentTrack);
    }
    if (!playback.metadata.isEmpty()) {
        payload.insert(QStringLiteral("metadata"), playback.metadata);
    }

    const int authoritativeGeneration = ++m_sessionAuthoritativeGeneration;
    QNetworkReply* pReply = m_pNetworkAccessManager->post(
            newJsonRequest(config::mixManSessionPlaybackPath(sessionId.trimmed())),
            jsonBody(payload));
    monitorMetadataReply(pReply);
    pReply->setProperty("operation", kSessionPlaybackOperation);
    pReply->setProperty(kRequestMethodProperty, QStringLiteral("POST"));
    pReply->setProperty(kRequestGenerationProperty, m_sessionRequestGeneration);
    pReply->setProperty(kAuthoritativeGenerationProperty, authoritativeGeneration);
    pReply->setProperty(kMutationSequenceProperty, mutationSequence);
    pReply->setProperty(kRequestStartedAtProperty, QDateTime::currentMSecsSinceEpoch());
    connect(pReply, &QNetworkReply::finished, this, &RestLibraryClient::slotSessionWriteFinished);
}

void RestLibraryClient::publishMixManSessionSnapshot(
        const RestLibrarySettings& settings,
        const QString& sessionId,
        const RestLibrarySessionSnapshot& snapshot,
        quint64 mutationSequence) {
    m_settings = settings;
    if (!m_pNetworkAccessManager || !m_settings.isConfigured() || sessionId.trimmed().isEmpty()) {
        RestLibrarySessionWriteStatus status;
        status.operation = kSessionSnapshotOperation;
        status.mutationSequence = mutationSequence;
        status.errorText = tr("MixMan session snapshot could not be published.");
        emit mixManSessionWriteStatusUpdated(status);
        return;
    }

    QJsonObject payload{
            {QStringLiteral("instance_id"), snapshot.lease.instanceId},
            {QStringLiteral("lease_id"), snapshot.lease.leaseId},
            {QStringLiteral("lease_generation"), snapshot.lease.generation},
    };
    insertIntegerStringIfValid(
            &payload,
            QStringLiteral("current_track_id"),
            snapshot.currentTrackId);
    insertIfNotEmpty(&payload, QStringLiteral("cue"), snapshot.cue);
    insertIfNotEmpty(&payload, QStringLiteral("playback_state"), snapshot.playbackState);
    payload.insert(QStringLiteral("snapshot"), snapshot.snapshot);
    if (!snapshot.metadata.isEmpty()) {
        payload.insert(QStringLiteral("metadata"), snapshot.metadata);
    }

    const int authoritativeGeneration = ++m_sessionAuthoritativeGeneration;
    QNetworkReply* pReply = m_pNetworkAccessManager->put(
            newJsonRequest(config::mixManSessionSnapshotPath(sessionId.trimmed())),
            jsonBody(payload));
    monitorMetadataReply(pReply);
    pReply->setProperty("operation", kSessionSnapshotOperation);
    pReply->setProperty(kRequestMethodProperty, QStringLiteral("PUT"));
    pReply->setProperty(kRequestGenerationProperty, m_sessionRequestGeneration);
    pReply->setProperty(kAuthoritativeGenerationProperty, authoritativeGeneration);
    pReply->setProperty(kMutationSequenceProperty, mutationSequence);
    pReply->setProperty(kRequestStartedAtProperty, QDateTime::currentMSecsSinceEpoch());
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

    QJsonObject payload{{QStringLiteral("instance_id"), intent.instanceId}};
    payload.insert(
            QStringLiteral("status"),
            intent.status == QStringLiteral("cleared") ? QStringLiteral("cleared")
                                                        : QStringLiteral("active"));
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
    monitorMetadataReply(pReply);
    pReply->setProperty("operation", kSessionIntentOperation);
    pReply->setProperty(kRequestMethodProperty, QStringLiteral("PUT"));
    pReply->setProperty(kRequestGenerationProperty, m_sessionRequestGeneration);
    pReply->setProperty(kRequestStartedAtProperty, QDateTime::currentMSecsSinceEpoch());
    connect(pReply, &QNetworkReply::finished, this, &RestLibraryClient::slotSessionWriteFinished);
}

void RestLibraryClient::sendMixManSessionHeartbeat(
        const RestLibrarySettings& settings,
        const QString& sessionId,
        const QString& instanceId,
        const QJsonObject& metadata) {
    m_settings = settings;
    if (!m_pNetworkAccessManager || !m_settings.isConfigured() || sessionId.trimmed().isEmpty()) {
        return;
    }

    QJsonObject payload{{QStringLiteral("status"), QStringLiteral("active")}};
    if (!metadata.isEmpty()) {
        payload.insert(QStringLiteral("metadata"), metadata);
    }
    QNetworkReply* pReply = m_pNetworkAccessManager->post(
            newJsonRequest(config::mixManSessionInstanceHeartbeatPath(
                    sessionId.trimmed(), instanceId.trimmed())),
            jsonBody(payload));
    monitorMetadataReply(pReply);
    pReply->setProperty("operation", kSessionHeartbeatOperation);
    pReply->setProperty(kRequestMethodProperty, QStringLiteral("POST"));
    pReply->setProperty(kRequestGenerationProperty, m_sessionRequestGeneration);
    pReply->setProperty(kRequestStartedAtProperty, QDateTime::currentMSecsSinceEpoch());
    connect(pReply, &QNetworkReply::finished, this, &RestLibraryClient::slotSessionWriteFinished);
}

void RestLibraryClient::claimMixManPlaybackControl(
        const RestLibrarySettings& settings,
        const QString& sessionId,
        const QString& instanceId,
        int ttlSeconds,
        const QJsonObject& metadata,
        quint64 mutationSequence) {
    QJsonObject requestMetadata = metadata;
    requestMetadata.insert(
            QStringLiteral("ttl_seconds"), std::clamp(ttlSeconds, 5, 300));
    requestMixManPlaybackControl(
            settings,
            sessionId,
            RestLibraryPlaybackLease{instanceId, {}, 0, false},
            QStringLiteral("claim"),
            kSessionPlaybackControlClaimOperation,
            requestMetadata,
            mutationSequence);
}

void RestLibraryClient::renewMixManPlaybackControl(
        const RestLibrarySettings& settings,
        const QString& sessionId,
        const RestLibraryPlaybackLease& lease,
        quint64 mutationSequence) {
    requestMixManPlaybackControl(
            settings,
            sessionId,
            lease,
            QStringLiteral("renew"),
            kSessionPlaybackControlRenewOperation,
            {},
            mutationSequence);
}

void RestLibraryClient::releaseMixManPlaybackControl(
        const RestLibrarySettings& settings,
        const QString& sessionId,
        const RestLibraryPlaybackLease& lease,
        quint64 mutationSequence) {
    requestMixManPlaybackControl(
            settings,
            sessionId,
            lease,
            QStringLiteral("release"),
            kSessionPlaybackControlReleaseOperation,
            {},
            mutationSequence);
}

void RestLibraryClient::requestMixManPlaybackControl(
        const RestLibrarySettings& settings,
        const QString& sessionId,
        const RestLibraryPlaybackLease& lease,
        const QString& action,
        const QString& operation,
        const QJsonObject& metadata,
        quint64 mutationSequence) {
    m_settings = settings;
    if (!m_pNetworkAccessManager || !m_settings.isConfigured() || sessionId.trimmed().isEmpty()) {
        RestLibrarySessionWriteStatus status;
        status.operation = operation;
        status.mutationSequence = mutationSequence;
        status.errorText = tr("MixMan playback control is not configured.");
        emit mixManSessionWriteStatusUpdated(status);
        return;
    }

    QJsonObject payload{{QStringLiteral("instance_id"), lease.instanceId}};
    if (action == QStringLiteral("claim")) {
        payload.insert(QStringLiteral("ttl_seconds"),
                metadata.value(QStringLiteral("ttl_seconds")).toInt(30));
    } else {
        payload.insert(QStringLiteral("lease_id"), lease.leaseId);
        payload.insert(QStringLiteral("generation"), lease.generation);
    }
    if (action == QStringLiteral("claim") && !metadata.isEmpty()) {
        QJsonObject publishedMetadata = metadata;
        publishedMetadata.remove(QStringLiteral("ttl_seconds"));
        if (!publishedMetadata.isEmpty()) {
            payload.insert(QStringLiteral("metadata"), publishedMetadata);
        }
    }

    const int authoritativeGeneration = ++m_sessionAuthoritativeGeneration;
    QString path;
    if (action == QStringLiteral("renew")) {
        path = config::mixManSessionPlaybackControlRenewPath(sessionId.trimmed());
    } else if (action == QStringLiteral("release")) {
        path = config::mixManSessionPlaybackControlReleasePath(sessionId.trimmed());
    } else {
        path = config::mixManSessionPlaybackControlClaimPath(sessionId.trimmed());
    }
    QNetworkReply* pReply = m_pNetworkAccessManager->post(
            newJsonRequest(path),
            jsonBody(payload));
    monitorMetadataReply(pReply);
    pReply->setProperty("operation", operation);
    pReply->setProperty(kRequestMethodProperty, QStringLiteral("POST"));
    pReply->setProperty(kRequestGenerationProperty, m_sessionRequestGeneration);
    pReply->setProperty(kAuthoritativeGenerationProperty, authoritativeGeneration);
    pReply->setProperty(kMutationSequenceProperty, mutationSequence);
    pReply->setProperty(kRequestStartedAtProperty, QDateTime::currentMSecsSinceEpoch());
    connect(pReply, &QNetworkReply::finished, this, &RestLibraryClient::slotSessionWriteFinished);
}

void RestLibraryClient::selectMixManSessionCandidate(
        const RestLibrarySettings& settings,
        const QString& sessionId,
        const QString& trackId,
        const RestLibraryPlaybackLease& lease,
        const QJsonObject& metadata,
        quint64 mutationSequence) {
    m_settings = settings;
    const QString normalizedTrackId = positiveIntegerString(trackId);
    if (!m_pNetworkAccessManager ||
            !m_settings.isConfigured() ||
            sessionId.trimmed().isEmpty() ||
            normalizedTrackId.isEmpty()) {
        RestLibrarySessionWriteStatus status;
        status.operation = kSessionCandidateSelectOperation;
        status.mutationSequence = mutationSequence;
        status.errorText = tr("MixMan candidate selection could not be published.");
        emit mixManSessionWriteStatusUpdated(status);
        return;
    }

    QJsonObject payload{
            {QStringLiteral("instance_id"), lease.instanceId},
            {QStringLiteral("lease_id"), lease.leaseId},
            {QStringLiteral("lease_generation"), lease.generation},
            {QStringLiteral("selection_origin"), QStringLiteral("authoritative_candidate")},
            {QStringLiteral("allow_external_candidate"), false},
    };
    if (!metadata.isEmpty()) {
        payload.insert(QStringLiteral("metadata"), metadata);
    }

    const int authoritativeGeneration = ++m_sessionAuthoritativeGeneration;
    QNetworkReply* pReply = m_pNetworkAccessManager->post(
            newJsonRequest(config::mixManSessionCandidateSelectPath(
                    sessionId.trimmed(),
                    normalizedTrackId)),
            jsonBody(payload));
    monitorMetadataReply(pReply);
    pReply->setProperty("operation", kSessionCandidateSelectOperation);
    pReply->setProperty(kRequestMethodProperty, QStringLiteral("POST"));
    pReply->setProperty(kRequestGenerationProperty, m_sessionRequestGeneration);
    pReply->setProperty(kAuthoritativeGenerationProperty, authoritativeGeneration);
    pReply->setProperty(kMutationSequenceProperty, mutationSequence);
    pReply->setProperty(kRequestStartedAtProperty, QDateTime::currentMSecsSinceEpoch());
    connect(pReply, &QNetworkReply::finished, this, &RestLibraryClient::slotSessionWriteFinished);
}

void RestLibraryClient::publishMixManPolicyRefreshAction(
        const RestLibrarySettings& settings,
        const QString& sessionId,
        const QString& instanceId,
        const QJsonObject& metadata,
        quint64 mutationSequence) {
    m_settings = settings;
    if (!m_pNetworkAccessManager || !m_settings.isConfigured() || sessionId.trimmed().isEmpty()) {
        RestLibrarySessionWriteStatus status;
        status.operation = kSessionPolicyRefreshOperation;
        status.mutationSequence = mutationSequence;
        status.errorText = tr("MixMan policy refresh could not be published.");
        emit mixManSessionWriteStatusUpdated(status);
        return;
    }

    QJsonObject payload{
            {QStringLiteral("action_type"), QStringLiteral("policy_refresh")},
            {QStringLiteral("instance_id"), instanceId},
    };
    insertIfNotEmpty(&payload, QStringLiteral("policy_preset"), m_settings.mixManPolicyPreset);
    if (m_settings.mixManTargetColorEnabled &&
            !m_settings.mixManTargetColor.trimmed().isEmpty()) {
        payload.insert(QStringLiteral("target_color"), m_settings.mixManTargetColor.trimmed());
    }
    if (m_settings.mixManTargetEnergyEnabled) {
        payload.insert(QStringLiteral("target_energy"), m_settings.mixManTargetEnergyNormalized());
    }
    if (m_settings.mixManTargetBpmEnabled) {
        payload.insert(QStringLiteral("target_bpm"), m_settings.mixManTargetBpm);
    }
    payload.insert(
            QStringLiteral("reroll_constraints"),
            QJsonObject{
                    {QStringLiteral("mode"), QStringLiteral("fuzzy")},
                    {QStringLiteral("limit"), m_settings.recommendationLimit}});
    if (!metadata.isEmpty()) {
        payload.insert(QStringLiteral("metadata"), metadata);
    }

    const int authoritativeGeneration = ++m_sessionAuthoritativeGeneration;
    QNetworkReply* pReply = m_pNetworkAccessManager->post(
            newJsonRequest(config::mixManSessionActionsPath(sessionId.trimmed())),
            jsonBody(payload));
    monitorMetadataReply(pReply);
    pReply->setProperty("operation", kSessionPolicyRefreshOperation);
    pReply->setProperty(kRequestMethodProperty, QStringLiteral("POST"));
    pReply->setProperty(kRequestGenerationProperty, m_sessionRequestGeneration);
    pReply->setProperty(kAuthoritativeGenerationProperty, authoritativeGeneration);
    pReply->setProperty(kMutationSequenceProperty, mutationSequence);
    pReply->setProperty(kRequestStartedAtProperty, QDateTime::currentMSecsSinceEpoch());
    connect(pReply, &QNetworkReply::finished, this, &RestLibraryClient::slotSessionWriteFinished);
}

void RestLibraryClient::testMixManConnection(
        const RestLibrarySettings& settings,
        const QString& clientId,
        bool createSession) {
    cancelMixManConnectionTest();
    ++m_connectionTestRequestGeneration;
    m_connectionTestSettings = settings;
    m_connectionTestClientId = clientId.trimmed().isEmpty()
            ? QStringLiteral("mixxx-connection-test")
            : clientId.trimmed();
    m_connectionTestSessionId.clear();
    m_connectionTestInstanceId.clear();
    m_connectionTestLease = {};
    m_connectionTestCreateSession = createSession;
    m_connectionTestFailed = false;
    m_settings = settings;

    if (!m_pNetworkAccessManager) {
        emitConfigurationDiagnostic(tr("Network access is not available."));
        emit connectionTestFinished(false);
        return;
    }
    if (!m_settings.enabled) {
        emitConfigurationDiagnostic(tr("REST Library is disabled."));
        emit connectionTestFinished(false);
        return;
    }
    if (!m_settings.baseUrl.isValid() || m_settings.baseUrl.isEmpty() ||
            m_settings.baseUrl.isRelative()) {
        emitConfigurationDiagnostic(tr("Base URL is not valid."));
        emit connectionTestFinished(false);
        return;
    }

    QNetworkReply* pReply = startConnectionTestGet(
            config::mixManHealthPath(),
            0,
            tr("Health check"));
    connect(pReply,
            &QNetworkReply::finished,
            this,
            &RestLibraryClient::slotConnectionTestHealthFinished);
}

void RestLibraryClient::cancelMixManConnectionTest() {
    if (!m_connectionTestSessionId.isEmpty() &&
            !m_connectionTestInstanceId.isEmpty()) {
        disconnectMixManSessionInstance(
                m_connectionTestSettings,
                m_connectionTestSessionId,
                m_connectionTestInstanceId);
    }
    ++m_connectionTestRequestGeneration;
    const QVector<QPointer<QNetworkReply>> replies = m_connectionTestReplies;
    m_connectionTestReplies.clear();
    for (const auto& reply : replies) {
        if (!reply) {
            continue;
        }
        if (!reply->isFinished()) {
            reply->abort();
        }
        reply->deleteLater();
    }
    m_connectionTestSessionId.clear();
    m_connectionTestInstanceId.clear();
    m_connectionTestLease = {};
    m_connectionTestFailed = false;
}

void RestLibraryClient::invalidateMixManRequests() {
    ++m_mixManDiagnosticsRequestGeneration;
    ++m_policyPresetsRequestGeneration;
    ++m_policyPathRequestGeneration;
    ++m_sessionRequestGeneration;
    ++m_sessionAuthoritativeGeneration;
    ++m_connectionTestRequestGeneration;
}

QNetworkRequest RestLibraryClient::newRequest(const QString& path, int limit) const {
    return newRequest(m_settings, path, limit);
}

QNetworkRequest RestLibraryClient::newRequest(
        const RestLibrarySettings& settings,
        const QString& path,
        int limit) const {
    QUrl url = config::urlWithRestPath(settings.baseUrl, path);
    QUrlQuery query(url);
    if (limit > 0 && !query.hasQueryItem(QStringLiteral("limit"))) {
        query.addQueryItem(QStringLiteral("limit"), QString::number(limit));
    }
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setAttribute(
            QNetworkRequest::RedirectPolicyAttribute,
            QNetworkRequest::SameOriginRedirectPolicy);
    request.setTransferTimeout(kRequestTimeoutMillis);
    if (settings.maySendBearerTokenTo(url)) {
        request.setRawHeader(
                "Authorization",
                QByteArray("Bearer ") + settings.bearerToken.toUtf8());
    }
    kLogger.info() << "REST library request" << url.toString(QUrl::RemoveUserInfo);
    return request;
}

void RestLibraryClient::monitorMetadataReply(QNetworkReply* pReply) {
    if (!pReply) {
        return;
    }
    pReply->setParent(this);
    pReply->setReadBufferSize(kMaxMetadataResponseBytes + 1);
    m_metadataResponseBodies.insert(pReply, {});
    connect(pReply, &QNetworkReply::readyRead, this, [this, pReply] {
        consumeMetadataReply(pReply, true);
    });
    connect(pReply, &QNetworkReply::metaDataChanged, this, [this, pReply] {
        bool validContentLength = false;
        const qint64 contentLength = pReply
                                             ->header(QNetworkRequest::ContentLengthHeader)
                                             .toLongLong(&validContentLength);
        if (validContentLength && contentLength > kMaxMetadataResponseBytes &&
                !pReply->property(kResponseTooLargeProperty).toBool()) {
            pReply->setProperty(kResponseTooLargeProperty, true);
            pReply->abort();
        }
    });
    connect(pReply, &QObject::destroyed, this, [this, pReply] {
        m_metadataResponseBodies.remove(pReply);
    });
}

void RestLibraryClient::consumeMetadataReply(
        QNetworkReply* pReply,
        bool abortIfOversized) {
    if (!pReply || !pReply->isOpen() ||
            pReply->property(kResponseTooLargeProperty).toBool()) {
        return;
    }
    QByteArray& responseBody = m_metadataResponseBodies[pReply];
    const qint64 remaining = kMaxMetadataResponseBytes - responseBody.size();
    const QByteArray chunk = pReply->read(remaining + 1);
    if (chunk.size() > remaining) {
        responseBody.append(chunk.constData(), static_cast<qsizetype>(remaining));
        pReply->setProperty(kResponseTooLargeProperty, true);
        if (abortIfOversized) {
            pReply->abort();
        }
        return;
    }
    responseBody.append(chunk);
}

QByteArray RestLibraryClient::takeMetadataReplyBody(QNetworkReply* pReply) {
    consumeMetadataReply(pReply, false);
    return m_metadataResponseBodies.take(pReply);
}

QNetworkRequest RestLibraryClient::newJsonRequest(const QString& path) const {
    QNetworkRequest request = newRequest(path, 0);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    return request;
}

QNetworkRequest RestLibraryClient::newDetailRequest(
        const RestLibrarySettings& settings,
        const QString& remoteId) const {
    return newRequest(pathForRemoteId(settings.trackDetailPathTemplate, remoteId), 0);
}

QNetworkReply* RestLibraryClient::startConnectionTestGet(
        const QString& path,
        int limit,
        const QString& stage) {
    QNetworkReply* pReply = m_pNetworkAccessManager->get(
            newRequest(m_connectionTestSettings, path, limit));
    monitorMetadataReply(pReply);
    pReply->setProperty(kRequestGenerationProperty, m_connectionTestRequestGeneration);
    pReply->setProperty(kRequestStartedAtProperty, QDateTime::currentMSecsSinceEpoch());
    pReply->setProperty(kRequestStageProperty, stage);
    pReply->setProperty(kRequestMethodProperty, QStringLiteral("GET"));
    m_connectionTestReplies.append(QPointer<QNetworkReply>(pReply));
    return pReply;
}

QNetworkReply* RestLibraryClient::startConnectionTestPost(
        const QString& path,
        const QJsonObject& payload,
        const QString& stage) {
    QNetworkRequest request = newRequest(m_connectionTestSettings, path, 0);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply* pReply = m_pNetworkAccessManager->post(request, jsonBody(payload));
    monitorMetadataReply(pReply);
    pReply->setProperty(kRequestGenerationProperty, m_connectionTestRequestGeneration);
    pReply->setProperty(kRequestStartedAtProperty, QDateTime::currentMSecsSinceEpoch());
    pReply->setProperty(kRequestStageProperty, stage);
    pReply->setProperty(kRequestMethodProperty, QStringLiteral("POST"));
    m_connectionTestReplies.append(QPointer<QNetworkReply>(pReply));
    return pReply;
}

void RestLibraryClient::finishConnectionTestStep(
        QNetworkReply* pReply,
        const QString& failureSummary,
        bool* pSuccess) {
    if (!pReply) {
        if (pSuccess) {
            *pSuccess = false;
        }
        return;
    }
    const QByteArray responseBody = takeMetadataReplyBody(pReply);
    const bool success =
            pReply->error() == QNetworkReply::NoError &&
            isSuccessStatus(statusCodeFromReply(*pReply));
    emitReplyDiagnostic(
            *pReply,
            responseBody,
            pReply->property(kRequestStageProperty).toString(),
            pReply->property(kRequestMethodProperty).toString(),
            failureSummary,
            success);
    if (!success) {
        m_connectionTestFailed = true;
    }
    if (pSuccess) {
        *pSuccess = success;
    }
}

RestLibraryRequestDiagnostic RestLibraryClient::diagnosticForReply(
        const QNetworkReply& reply,
        const QByteArray& responseBody,
        const QString& stage,
        const QString& method,
        const QString& fallbackSummary,
        bool success) const {
    RestLibraryRequestDiagnostic diagnostic;
    diagnostic.stage = stage;
    diagnostic.method = method;
    diagnostic.url = reply.request().url().toString(QUrl::RemoveUserInfo);
    const bool responseTooLarge =
            reply.property(kResponseTooLargeProperty).toBool();
    diagnostic.success = success && !responseTooLarge;
    diagnostic.statusCode = statusCodeFromReply(reply);
    diagnostic.networkError = responseTooLarge
            ? static_cast<int>(QNetworkReply::NoError)
            : static_cast<int>(reply.error());
    const qint64 startedAt = reply.property(kRequestStartedAtProperty).toLongLong();
    if (startedAt > 0) {
        diagnostic.elapsedMillis = static_cast<int>(
                std::max<qint64>(0, QDateTime::currentMSecsSinceEpoch() - startedAt));
    }
    diagnostic.errorText = responseTooLarge
            ? tr("The server response exceeded the 4 MiB metadata limit.")
            : reply.error() == QNetworkReply::NoError ? QString()
                                                      : reply.errorString();
    diagnostic.responseSnippet = responseSnippet(responseBody);
    if (diagnostic.errorText.isEmpty() && !success) {
        diagnostic.errorText = errorTextFromResponse(responseBody);
    }
    diagnostic.summary = responseTooLarge
            ? tr("%1 failed because the server response was too large.").arg(stage)
            : diagnosticSummary(diagnostic, fallbackSummary);
    return diagnostic;
}

QString RestLibraryClient::diagnosticSummary(
        const RestLibraryRequestDiagnostic& diagnostic,
        const QString& fallbackSummary) const {
    if (diagnostic.success) {
        return diagnostic.statusCode > 0
                ? tr("%1 succeeded (%2).").arg(diagnostic.stage).arg(diagnostic.statusCode)
                : tr("%1 succeeded.").arg(diagnostic.stage);
    }
    if (diagnostic.networkError == static_cast<int>(QNetworkReply::OperationCanceledError)) {
        return tr("%1 was canceled.").arg(diagnostic.stage);
    }
    if (diagnostic.networkError == static_cast<int>(QNetworkReply::TimeoutError)) {
        return tr("%1 timed out.").arg(diagnostic.stage);
    }
    if (diagnostic.networkError != static_cast<int>(QNetworkReply::NoError)) {
        return diagnostic.errorText.isEmpty()
                ? tr("%1 failed because of a network error.").arg(diagnostic.stage)
                : tr("%1 failed because of a network error: %2.")
                          .arg(diagnostic.stage, diagnostic.errorText);
    }
    if (diagnostic.statusCode > 0 && !isSuccessStatus(diagnostic.statusCode)) {
        QString category;
        switch (diagnostic.statusCode) {
        case 401:
            category = tr("Authentication failed");
            break;
        case 403:
            category = tr("Permission denied");
            break;
        case 404:
            category = tr("Endpoint not found");
            break;
        case 409:
            category = tr("MixMan conflict");
            break;
        default:
            if (diagnostic.statusCode >= 500) {
                category = tr("Server error");
            }
            break;
        }
        if (!category.isEmpty()) {
            return diagnostic.errorText.isEmpty()
                    ? tr("%1: %2 (HTTP %3).")
                              .arg(diagnostic.stage)
                              .arg(category)
                              .arg(diagnostic.statusCode)
                    : tr("%1: %2 (HTTP %3): %4.")
                              .arg(diagnostic.stage)
                              .arg(category)
                              .arg(diagnostic.statusCode)
                              .arg(diagnostic.errorText);
        }
        if (!diagnostic.errorText.isEmpty()) {
            return tr("%1 failed with HTTP %2: %3.")
                    .arg(diagnostic.stage)
                    .arg(diagnostic.statusCode)
                    .arg(diagnostic.errorText);
        }
        return tr("%1 failed with HTTP %2.").arg(diagnostic.stage).arg(diagnostic.statusCode);
    }
    return fallbackSummary;
}

void RestLibraryClient::emitReplyDiagnostic(
        const QNetworkReply& reply,
        const QByteArray& responseBody,
        const QString& stage,
        const QString& method,
        const QString& fallbackSummary,
        bool success) {
    emit requestDiagnosticUpdated(diagnosticForReply(
            reply,
            responseBody,
            stage,
            method,
            fallbackSummary,
            success));
}

void RestLibraryClient::emitConfigurationDiagnostic(const QString& summary) {
    RestLibraryRequestDiagnostic diagnostic;
    diagnostic.stage = tr("Configuration");
    diagnostic.method = QStringLiteral("-");
    diagnostic.url = m_settings.baseUrl.toString(QUrl::RemoveUserInfo);
    diagnostic.success = false;
    diagnostic.summary = summary;
    diagnostic.errorText = summary;
    emit requestDiagnosticUpdated(diagnostic);
}

void RestLibraryClient::forgetConnectionTestReply(QNetworkReply* pReply) {
    for (auto it = m_connectionTestReplies.begin(); it != m_connectionTestReplies.end();) {
        if (!*it || *it == pReply) {
            it = m_connectionTestReplies.erase(it);
        } else {
            ++it;
        }
    }
}

void RestLibraryClient::slotConnectionTestHealthFinished() {
    auto* pReply = qobject_cast<QNetworkReply*>(sender());
    if (!pReply) {
        emit connectionTestFinished(false);
        return;
    }
    const bool staleReply =
            pReply->property(kRequestGenerationProperty).toInt() !=
            m_connectionTestRequestGeneration;
    forgetConnectionTestReply(pReply);
    pReply->deleteLater();
    if (staleReply) {
        return;
    }

    bool success = false;
    finishConnectionTestStep(pReply, tr("MixMan health check failed."), &success);
    if (!success) {
        emit connectionTestFinished(false);
        return;
    }

    QNetworkReply* pConfigReply = startConnectionTestGet(
            config::mixManConfigPath(),
            0,
            tr("Session contract"));
    connect(pConfigReply,
            &QNetworkReply::finished,
            this,
            &RestLibraryClient::slotConnectionTestConfigFinished);
}

void RestLibraryClient::slotConnectionTestConfigFinished() {
    auto* pReply = qobject_cast<QNetworkReply*>(sender());
    if (!pReply) {
        emit connectionTestFinished(false);
        return;
    }
    const bool staleReply =
            pReply->property(kRequestGenerationProperty).toInt() !=
            m_connectionTestRequestGeneration;
    forgetConnectionTestReply(pReply);
    pReply->deleteLater();
    if (staleReply) {
        return;
    }

    const QByteArray responseBody = takeMetadataReplyBody(pReply);
    bool success =
            pReply->error() == QNetworkReply::NoError &&
            isSuccessStatus(statusCodeFromReply(*pReply));
    QString fallbackSummary = tr("MixMan session contract request failed.");
    if (success) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(responseBody, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            success = false;
            fallbackSummary = tr("MixMan config response was not valid JSON.");
        } else {
            const RestLibrarySessionContract contract =
                    parseSessionContractDocument(document);
            success = contract.valid;
            if (!success) {
                fallbackSummary = contract.errorText;
            }
        }
    }
    emitReplyDiagnostic(
            *pReply,
            responseBody,
            pReply->property(kRequestStageProperty).toString(),
            QStringLiteral("GET"),
            fallbackSummary,
            success);
    if (!success) {
        m_connectionTestFailed = true;
        emit connectionTestFinished(false);
        return;
    }

    QNetworkReply* pIndexReply = startConnectionTestGet(
            config::mixManIndexStatusPath(),
            0,
            tr("Index status"));
    connect(pIndexReply,
            &QNetworkReply::finished,
            this,
            &RestLibraryClient::slotConnectionTestIndexFinished);
}

void RestLibraryClient::slotConnectionTestIndexFinished() {
    auto* pReply = qobject_cast<QNetworkReply*>(sender());
    if (!pReply) {
        emit connectionTestFinished(false);
        return;
    }
    const bool staleReply =
            pReply->property(kRequestGenerationProperty).toInt() !=
            m_connectionTestRequestGeneration;
    forgetConnectionTestReply(pReply);
    pReply->deleteLater();
    if (staleReply) {
        return;
    }

    const QByteArray responseBody = takeMetadataReplyBody(pReply);
    bool success =
            pReply->error() == QNetworkReply::NoError &&
            isSuccessStatus(statusCodeFromReply(*pReply));
    QString fallbackSummary = tr("MixMan index status request failed.");
    if (success) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(responseBody, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            success = false;
            fallbackSummary = tr("MixMan index status was not valid JSON.");
        } else {
            const RestLibraryDiagnostics diagnostics = parseIndexStatusDocument(document);
            if (!diagnostics.lastError.isEmpty()) {
                success = false;
                fallbackSummary = diagnostics.lastError;
            } else if (!diagnostics.indexReady) {
                success = false;
                fallbackSummary = tr("Index status returned but the index is not ready.");
            }
        }
    }
    emitReplyDiagnostic(
            *pReply,
            responseBody,
            pReply->property(kRequestStageProperty).toString(),
            QStringLiteral("GET"),
            fallbackSummary,
            success);
    if (!success) {
        m_connectionTestFailed = true;
        emit connectionTestFinished(false);
        return;
    }

    QNetworkReply* pTracksReply = startConnectionTestGet(
            m_connectionTestSettings.trackListPath.trimmed().isEmpty()
                    ? config::mixManTrackListPath()
                    : m_connectionTestSettings.trackListPath,
            1,
            tr("Track probe"));
    connect(pTracksReply,
            &QNetworkReply::finished,
            this,
            &RestLibraryClient::slotConnectionTestTracksFinished);
}

void RestLibraryClient::slotConnectionTestTracksFinished() {
    auto* pReply = qobject_cast<QNetworkReply*>(sender());
    if (!pReply) {
        emit connectionTestFinished(false);
        return;
    }
    const bool staleReply =
            pReply->property(kRequestGenerationProperty).toInt() !=
            m_connectionTestRequestGeneration;
    forgetConnectionTestReply(pReply);
    pReply->deleteLater();
    if (staleReply) {
        return;
    }

    const QByteArray responseBody = takeMetadataReplyBody(pReply);
    bool success =
            pReply->error() == QNetworkReply::NoError &&
            isSuccessStatus(statusCodeFromReply(*pReply));
    QString fallbackSummary = tr("REST track probe failed.");
    if (success) {
        QJsonParseError parseError;
        QJsonDocument::fromJson(responseBody, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            success = false;
            fallbackSummary = tr("REST track probe response was not valid JSON.");
        }
    }
    emitReplyDiagnostic(
            *pReply,
            responseBody,
            pReply->property(kRequestStageProperty).toString(),
            QStringLiteral("GET"),
            fallbackSummary,
            success);
    if (!success) {
        m_connectionTestFailed = true;
        emit connectionTestFinished(false);
        return;
    }
    if (!m_connectionTestSettings.useMixManDefaults || !m_connectionTestCreateSession) {
        emit connectionTestFinished(!m_connectionTestFailed);
        return;
    }

    m_connectionTestSessionId = generateMixManSessionId(QStringLiteral("mixxx-test"));
    QJsonObject payload{
            {QStringLiteral("session_id"), m_connectionTestSessionId},
            {QStringLiteral("display_name"), QStringLiteral("Mixxx connection test")},
            {QStringLiteral("metadata"),
                    QJsonObject{{QStringLiteral("connection_test"), true}}},
    };
    QNetworkReply* pSessionReply = startConnectionTestPost(
            config::mixManSessionsPath(),
            payload,
            tr("Session create"));
    connect(pSessionReply,
            &QNetworkReply::finished,
            this,
            &RestLibraryClient::slotConnectionTestSessionFinished);
}

void RestLibraryClient::slotConnectionTestSessionFinished() {
    auto* pReply = qobject_cast<QNetworkReply*>(sender());
    if (!pReply) {
        emit connectionTestFinished(false);
        return;
    }
    const bool staleReply =
            pReply->property(kRequestGenerationProperty).toInt() !=
            m_connectionTestRequestGeneration;
    forgetConnectionTestReply(pReply);
    pReply->deleteLater();
    if (staleReply) {
        return;
    }

    const QByteArray responseBody = takeMetadataReplyBody(pReply);
    bool success =
            pReply->error() == QNetworkReply::NoError &&
            isSuccessStatus(statusCodeFromReply(*pReply));
    QString fallbackSummary = tr("MixMan temporary session creation failed.");
    if (success) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(responseBody, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            success = false;
            fallbackSummary = tr("MixMan session response was not valid JSON.");
        } else if (parseSessionDocument(document).id.isEmpty()) {
            success = false;
            fallbackSummary = tr("MixMan session response did not include a session ID.");
        } else {
            m_connectionTestSessionId = parseSessionDocument(document).id;
        }
    }
    emitReplyDiagnostic(
            *pReply,
            responseBody,
            pReply->property(kRequestStageProperty).toString(),
            QStringLiteral("POST"),
            fallbackSummary,
            success);
    if (!success) {
        m_connectionTestFailed = true;
        emit connectionTestFinished(false);
        return;
    }
    QJsonObject payload{
            {QStringLiteral("application"), QStringLiteral("mixxx")},
            {QStringLiteral("surface"), QStringLiteral("rest_library")},
            {QStringLiteral("metadata"),
                    QJsonObject{{QStringLiteral("connection_test"), true}}},
    };
    QNetworkReply* pRegisterReply = startConnectionTestPost(
            config::mixManSessionInstancesPath(m_connectionTestSessionId),
            payload,
            tr("Instance registration"));
    connect(pRegisterReply,
            &QNetworkReply::finished,
            this,
            &RestLibraryClient::slotConnectionTestRegisterFinished);
}

void RestLibraryClient::slotConnectionTestRegisterFinished() {
    auto* pReply = qobject_cast<QNetworkReply*>(sender());
    if (!pReply) {
        emit connectionTestFinished(false);
        return;
    }
    const bool stale = pReply->property(kRequestGenerationProperty).toInt() !=
            m_connectionTestRequestGeneration;
    forgetConnectionTestReply(pReply);
    pReply->deleteLater();
    if (stale) {
        return;
    }
    const QByteArray body = takeMetadataReplyBody(pReply);
    const RestLibrarySessionRegistration registration =
            parseSessionRegistrationDocument(QJsonDocument::fromJson(body));
    if (!registration.instance.instanceId.isEmpty()) {
        m_connectionTestInstanceId = registration.instance.instanceId;
    }
    static const QStringList required{
            QStringLiteral("session.read"),
            QStringLiteral("session.recommendations.read"),
            QStringLiteral("session.intent.write"),
            QStringLiteral("session.intent.ack"),
            QStringLiteral("session.plan.write"),
            QStringLiteral("session.action.write"),
            QStringLiteral("playback.claim"),
            QStringLiteral("playback.publish"),
    };
    bool success = pReply->error() == QNetworkReply::NoError &&
            isSuccessStatus(statusCodeFromReply(*pReply)) && registration.isValid();
    for (const QString& capability : required) {
        success = success && registration.instance.capabilities.contains(capability);
    }
    success = success && registration.instance.capabilities.size() == required.size();
    success = success && !registration.instance.capabilities.contains(
            QStringLiteral("session.controller.read"));
    emitReplyDiagnostic(*pReply,
            body,
            tr("Instance registration"),
            QStringLiteral("POST"),
            tr("MixMan did not grant the complete Mixxx v3 capability set."),
            success);
    if (!success) {
        m_connectionTestFailed = true;
        if (!m_connectionTestInstanceId.isEmpty()) {
            startConnectionTestDisconnect();
            return;
        }
        emit connectionTestFinished(false);
        return;
    }
    QNetworkReply* pStateReply = startConnectionTestGet(
            config::mixManSessionStatePath(
                    m_connectionTestSessionId, m_connectionTestInstanceId),
            0,
            tr("Instance state"));
    connect(pStateReply,
            &QNetworkReply::finished,
            this,
            &RestLibraryClient::slotConnectionTestStateFinished);
}

void RestLibraryClient::slotConnectionTestStateFinished() {
    auto* pReply = qobject_cast<QNetworkReply*>(sender());
    if (!pReply) {
        emit connectionTestFinished(false);
        return;
    }
    const bool stale = pReply->property(kRequestGenerationProperty).toInt() !=
            m_connectionTestRequestGeneration;
    forgetConnectionTestReply(pReply);
    pReply->deleteLater();
    if (stale) {
        return;
    }
    bool success = false;
    finishConnectionTestStep(pReply, tr("MixMan instance-bound state read failed."), &success);
    if (!success) {
        m_connectionTestFailed = true;
        startConnectionTestDisconnect();
        return;
    }
    QNetworkReply* pClaimReply = startConnectionTestPost(
            config::mixManSessionPlaybackControlClaimPath(m_connectionTestSessionId),
            QJsonObject{{QStringLiteral("instance_id"), m_connectionTestInstanceId}},
            tr("Playback authority claim"));
    connect(pClaimReply,
            &QNetworkReply::finished,
            this,
            &RestLibraryClient::slotConnectionTestClaimFinished);
}

void RestLibraryClient::slotConnectionTestClaimFinished() {
    auto* pReply = qobject_cast<QNetworkReply*>(sender());
    if (!pReply) {
        emit connectionTestFinished(false);
        return;
    }
    const bool stale = pReply->property(kRequestGenerationProperty).toInt() !=
            m_connectionTestRequestGeneration;
    forgetConnectionTestReply(pReply);
    pReply->deleteLater();
    if (stale) {
        return;
    }
    const QByteArray body = takeMetadataReplyBody(pReply);
    const RestLibraryAuthoritativeState state =
            parseAuthoritativeDocument(QJsonDocument::fromJson(body));
    const bool success = pReply->error() == QNetworkReply::NoError &&
            isSuccessStatus(statusCodeFromReply(*pReply)) &&
            state.playbackLease.isValidFor(m_connectionTestInstanceId);
    emitReplyDiagnostic(*pReply,
            body,
            tr("Playback authority claim"),
            QStringLiteral("POST"),
            tr("MixMan playback authority claim failed."),
            success);
    if (!success) {
        m_connectionTestFailed = true;
        startConnectionTestDisconnect();
        return;
    }
    m_connectionTestLease = state.playbackLease;
    QNetworkReply* pReleaseReply = startConnectionTestPost(
            config::mixManSessionPlaybackControlReleasePath(m_connectionTestSessionId),
            QJsonObject{
                    {QStringLiteral("instance_id"), m_connectionTestLease.instanceId},
                    {QStringLiteral("lease_id"), m_connectionTestLease.leaseId},
                    {QStringLiteral("generation"), m_connectionTestLease.generation},
            },
            tr("Playback authority release"));
    connect(pReleaseReply,
            &QNetworkReply::finished,
            this,
            &RestLibraryClient::slotConnectionTestReleaseFinished);
}

void RestLibraryClient::slotConnectionTestReleaseFinished() {
    auto* pReply = qobject_cast<QNetworkReply*>(sender());
    if (!pReply) {
        emit connectionTestFinished(false);
        return;
    }
    const bool stale = pReply->property(kRequestGenerationProperty).toInt() !=
            m_connectionTestRequestGeneration;
    forgetConnectionTestReply(pReply);
    pReply->deleteLater();
    if (stale) {
        return;
    }
    bool success = false;
    finishConnectionTestStep(pReply, tr("MixMan playback authority release failed."), &success);
    m_connectionTestFailed = m_connectionTestFailed || !success;
    startConnectionTestDisconnect();
}

void RestLibraryClient::startConnectionTestDisconnect() {
    QNetworkReply* pDisconnectReply = startConnectionTestPost(
            config::mixManSessionInstanceDisconnectPath(
                    m_connectionTestSessionId, m_connectionTestInstanceId),
            {},
            tr("Instance disconnect"));
    connect(pDisconnectReply,
            &QNetworkReply::finished,
            this,
            &RestLibraryClient::slotConnectionTestDisconnectFinished);
}

void RestLibraryClient::slotConnectionTestDisconnectFinished() {
    auto* pReply = qobject_cast<QNetworkReply*>(sender());
    if (!pReply) {
        emit connectionTestFinished(false);
        return;
    }
    const bool stale = pReply->property(kRequestGenerationProperty).toInt() !=
            m_connectionTestRequestGeneration;
    forgetConnectionTestReply(pReply);
    pReply->deleteLater();
    if (stale) {
        return;
    }
    bool success = false;
    finishConnectionTestStep(pReply, tr("MixMan test instance disconnect failed."), &success);
    m_connectionTestFailed = m_connectionTestFailed || !success;
    emit connectionTestFinished(!m_connectionTestFailed);
}

void RestLibraryClient::slotTrackListFinished() {
    auto* pReply = qobject_cast<QNetworkReply*>(sender());
    if (!pReply) {
        emit fetchFailed(tr("Remote library request failed."));
        return;
    }
    const RequestContext context = m_requestContexts.take(pReply);
    const bool staleReply =
            pReply->property(kRequestGenerationProperty).toInt() !=
            m_trackListRequestGeneration;
    pReply->deleteLater();
    if (staleReply) {
        m_trackBatches.remove(context.generation);
        return;
    }
    auto batchIt = m_trackBatches.find(context.generation);
    if (batchIt == m_trackBatches.end()) {
        emit fetchFailed(tr("Remote library request failed."));
        return;
    }
    TrackRequestBatch* pBatch = &batchIt.value();

    const QByteArray responseBody = takeMetadataReplyBody(pReply);
    const int statusCode = statusCodeFromReply(*pReply);
    if (pReply->error() != QNetworkReply::NoError || !isSuccessStatus(statusCode)) {
        kLogger.warning()
                << "REST library request failed"
                << pReply->request().url().toString(QUrl::RemoveUserInfo)
                << "status" << statusCode
                << "network error" << pReply->error()
                << pReply->errorString()
                << "body" << responseSnippet(responseBody);
        const auto diagnostic = diagnosticForReply(
                *pReply,
                responseBody,
                context.stage,
                context.method,
                tr("Remote library request returned an unsuccessful status."),
                false);
        emit requestDiagnosticUpdated(diagnostic);
        emitFailureForPurpose(context.purpose, diagnostic.summary);
        m_trackBatches.remove(context.generation);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(responseBody, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        kLogger.warning()
                << "Failed to parse remote library JSON response from"
                << pReply->request().url().toString(QUrl::RemoveUserInfo)
                << "body" << responseSnippet(responseBody);
        emitReplyDiagnostic(
                *pReply,
                responseBody,
                context.stage,
                context.method,
                tr("Remote library response was not valid JSON."),
                false);
        emitFailureForPurpose(context.purpose, tr("Remote library response was not valid JSON."));
        m_trackBatches.remove(context.generation);
        return;
    }

    QStringList remoteIds;
    pBatch->pendingTracks = parseTrackListDocument(document, &remoteIds);
    if (context.purpose == RequestPurpose::TrackLookup) {
        if (!pBatch->pendingTracks.isEmpty()) {
            emit trackLookupSucceeded(pBatch->pendingTracks.constFirst().remoteId);
            m_trackBatches.remove(context.generation);
            return;
        }
        if (!remoteIds.isEmpty()) {
            emit trackLookupSucceeded(remoteIds.constFirst());
            m_trackBatches.remove(context.generation);
            return;
        }
        emit trackLookupMissed(tr("No matching remote track was found."));
        m_trackBatches.remove(context.generation);
        return;
    }

    if (remoteIds.isEmpty() || context.settings.trackDetailPathTemplate.trimmed().isEmpty()) {
        emitTracksForPurpose(context.purpose, pBatch->pendingTracks);
        m_trackBatches.remove(context.generation);
        return;
    }
    startDetailRequests(context.generation, remoteIds);
}

void RestLibraryClient::slotTrackCatalogFinished() {
    auto* pReply = qobject_cast<QNetworkReply*>(sender());
    if (!pReply) {
        emit trackCatalogFetchFailed(tr("REST catalog request failed."));
        return;
    }
    const bool staleReply =
            pReply->property(kRequestGenerationProperty).toInt() !=
            m_trackCatalogRequestGeneration;
    if (m_pTrackCatalogReply == pReply) {
        m_pTrackCatalogReply.clear();
    }
    pReply->deleteLater();
    if (staleReply) {
        return;
    }

    const QByteArray responseBody = takeMetadataReplyBody(pReply);
    const int statusCode = statusCodeFromReply(*pReply);
    if (pReply->error() != QNetworkReply::NoError || !isSuccessStatus(statusCode)) {
        const auto diagnostic = diagnosticForReply(
                *pReply,
                responseBody,
                tr("Track catalog"),
                QStringLiteral("GET"),
                tr("MixMan hydrated track catalog request failed."),
                false);
        emit requestDiagnosticUpdated(diagnostic);
        emit trackCatalogFetchFailed(diagnostic.summary);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(responseBody, &parseError);
    bool valid = parseError.error == QJsonParseError::NoError;
    RestLibraryCatalogPage page;
    if (valid) {
        page = parseTrackCatalogPage(document, &valid);
    }
    if (!valid) {
        emitReplyDiagnostic(
                *pReply,
                responseBody,
                tr("Track catalog"),
                QStringLiteral("GET"),
                tr("MixMan hydrated track catalog response was invalid."),
                false);
        emit trackCatalogFetchFailed(
                tr("MixMan hydrated track catalog response was invalid."));
        return;
    }
    emit trackCatalogPageFetched(page);
}

void RestLibraryClient::startDetailRequests(int requestGeneration, const QStringList& remoteIds) {
    if (!m_pNetworkAccessManager) {
        auto batchIt = m_trackBatches.find(requestGeneration);
        if (batchIt != m_trackBatches.end()) {
            emitTracksForPurpose(batchIt.value().purpose, batchIt.value().pendingTracks);
            m_trackBatches.erase(batchIt);
        }
        return;
    }
    auto batchIt = m_trackBatches.find(requestGeneration);
    if (batchIt == m_trackBatches.end()) {
        return;
    }
    TrackRequestBatch& batch = batchIt.value();

    const int requestCount = std::min(
            static_cast<int>(remoteIds.size()),
            batch.purpose == RequestPurpose::Recommendations
                    ? batch.settings.recommendationLimit
                    : batch.settings.pageSize);
    batch.pendingDetails.reserve(requestCount);
    for (int i = 0; i < requestCount; ++i) {
        QNetworkReply* pReply = m_pNetworkAccessManager->get(
                newDetailRequest(batch.settings, remoteIds.at(i)));
        monitorMetadataReply(pReply);
        pReply->setProperty(kRequestGenerationProperty, requestGeneration);
        pReply->setProperty(kRequestStartedAtProperty, QDateTime::currentMSecsSinceEpoch());
        batch.pendingDetails.push_back(PendingDetail{
                QPointer<QNetworkReply>(pReply),
                remoteIds.at(i)});
        m_requestContexts.insert(pReply, RequestContext{
                batch.settings,
                batch.purpose,
                requestGeneration,
                tr("Track detail"),
                QStringLiteral("GET"),
                remoteIds.at(i)});
        connect(pReply, &QNetworkReply::finished, this, &RestLibraryClient::slotTrackDetailFinished);
    }
}

void RestLibraryClient::slotTrackDetailFinished() {
    auto* pReply = qobject_cast<QNetworkReply*>(sender());
    if (!pReply) {
        return;
    }
    const RequestContext context = m_requestContexts.take(pReply);
    const bool staleReply =
            pReply->property(kRequestGenerationProperty).toInt() !=
            m_trackListRequestGeneration;
    pReply->deleteLater();
    if (staleReply) {
        m_trackBatches.remove(context.generation);
        return;
    }
    auto batchIt = m_trackBatches.find(context.generation);
    if (batchIt == m_trackBatches.end()) {
        return;
    }
    TrackRequestBatch& batch = batchIt.value();
    ++batch.finishedDetailCount;

    const QByteArray responseBody = takeMetadataReplyBody(pReply);
    const int statusCode = statusCodeFromReply(*pReply);
    if (pReply->error() != QNetworkReply::NoError || !isSuccessStatus(statusCode)) {
        kLogger.warning()
                << "REST library track detail request failed"
                << pReply->request().url().toString(QUrl::RemoveUserInfo)
                << "status" << statusCode
                << "network error" << pReply->error()
                << pReply->errorString()
                << "body" << responseSnippet(responseBody);
        emitReplyDiagnostic(
                *pReply,
                responseBody,
                context.stage,
                context.method,
                tr("Remote library track detail request failed."),
                false);
        batch.detailBatchFailed = true;
        finishDetailBatchIfComplete(context.generation);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(responseBody, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        kLogger.warning()
                << "REST library track detail response was not a JSON object from"
                << pReply->request().url().toString(QUrl::RemoveUserInfo)
                << "body" << responseSnippet(responseBody);
        emitReplyDiagnostic(
                *pReply,
                responseBody,
                context.stage,
                context.method,
                tr("Remote library track detail response was not valid JSON."),
                false);
        batch.detailBatchFailed = true;
        finishDetailBatchIfComplete(context.generation);
        return;
    }

    RestLibraryTrack track = parseTrackObject(document.object());
    if (track.remoteId.isEmpty()) {
        for (const auto& detail : std::as_const(batch.pendingDetails)) {
            if (detail.reply == pReply) {
                track.remoteId = detail.remoteId;
                break;
            }
        }
        if (track.remoteId.isEmpty()) {
            track.remoteId = context.remoteId;
        }
    }
    if (!track.remoteId.isEmpty()) {
        batch.pendingTracks.append(std::move(track));
    }

    finishDetailBatchIfComplete(context.generation);
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
    const QByteArray responseBody = takeMetadataReplyBody(pReply);
    diagnostics.healthKnown = true;
    diagnostics.lastStatusCode = statusCodeFromReply(*pReply);
    diagnostics.healthOk =
            pReply->error() == QNetworkReply::NoError &&
            isSuccessStatus(diagnostics.lastStatusCode);
    if (!diagnostics.healthOk) {
        const auto requestDiagnostic = diagnosticForReply(
                *pReply,
                responseBody,
                tr("Health check"),
                QStringLiteral("GET"),
                tr("MixMan health check failed."),
                false);
        diagnostics.lastError = requestDiagnostic.summary;
        diagnostics.healthError = requestDiagnostic.summary;
        emit requestDiagnosticUpdated(requestDiagnostic);
    } else {
        emitReplyDiagnostic(
                *pReply,
                responseBody,
                tr("Health check"),
                QStringLiteral("GET"),
                {},
                true);
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

    const QByteArray responseBody = takeMetadataReplyBody(pReply);
    RestLibraryDiagnostics diagnostics;
    diagnostics.indexKnown = true;
    diagnostics.lastStatusCode = statusCodeFromReply(*pReply);
    if (pReply->error() != QNetworkReply::NoError ||
            !isSuccessStatus(diagnostics.lastStatusCode)) {
        const auto requestDiagnostic = diagnosticForReply(
                *pReply,
                responseBody,
                tr("Index status"),
                QStringLiteral("GET"),
                tr("MixMan index status request failed."),
                false);
        diagnostics.lastError = requestDiagnostic.summary;
        diagnostics.indexError = requestDiagnostic.summary;
        emit requestDiagnosticUpdated(requestDiagnostic);
        emit diagnosticsUpdated(diagnostics);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(responseBody, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        diagnostics.lastError = tr("MixMan index status was not valid JSON.");
        diagnostics.indexError = diagnostics.lastError;
        emitReplyDiagnostic(
                *pReply,
                responseBody,
                tr("Index status"),
                QStringLiteral("GET"),
                diagnostics.lastError,
                false);
        emit diagnosticsUpdated(diagnostics);
        return;
    }

    diagnostics = parseIndexStatusDocument(document);
    diagnostics.lastStatusCode = statusCodeFromReply(*pReply);
    emitReplyDiagnostic(
            *pReply,
            responseBody,
            tr("Index status"),
            QStringLiteral("GET"),
            {},
            true);
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

    const QByteArray responseBody = takeMetadataReplyBody(pReply);
    const int statusCode = statusCodeFromReply(*pReply);
    if (pReply->error() != QNetworkReply::NoError || !isSuccessStatus(statusCode)) {
        emitReplyDiagnostic(
                *pReply,
                responseBody,
                tr("Policy presets"),
                QStringLiteral("GET"),
                tr("MixMan policy presets request failed."),
                false);
        emit policyPresetsFetched({});
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(responseBody, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        emitReplyDiagnostic(
                *pReply,
                responseBody,
                tr("Policy presets"),
                QStringLiteral("GET"),
                tr("MixMan policy presets response was not valid JSON."),
                false);
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

    const QByteArray responseBody = takeMetadataReplyBody(pReply);
    const int statusCode = statusCodeFromReply(*pReply);
    if (pReply->error() != QNetworkReply::NoError || !isSuccessStatus(statusCode)) {
        kLogger.warning()
                << "MixMan policy path request failed"
                << pReply->request().url().toString(QUrl::RemoveUserInfo)
                << "status" << statusCode
                << "body" << responseSnippet(responseBody);
        const auto diagnostic = diagnosticForReply(
                *pReply,
                responseBody,
                tr("Policy path"),
                QStringLiteral("GET"),
                tr("MixMan policy path request returned an unsuccessful status."),
                false);
        emit requestDiagnosticUpdated(diagnostic);
        emit fetchFailed(diagnostic.summary);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(responseBody, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        emitReplyDiagnostic(
                *pReply,
                responseBody,
                tr("Policy path"),
                QStringLiteral("GET"),
                tr("MixMan policy path response was not valid JSON."),
                false);
        emit fetchFailed(tr("MixMan policy path response was not valid JSON."));
        return;
    }
    emit mixManPolicyPathFetched(parsePolicyPathDocument(document));
}

void RestLibraryClient::slotSessionContractFinished() {
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

    const QByteArray responseBody = takeMetadataReplyBody(pReply);
    const int statusCode = statusCodeFromReply(*pReply);
    RestLibrarySessionContract contract;
    const bool requestSucceeded =
            pReply->error() == QNetworkReply::NoError && isSuccessStatus(statusCode);
    if (requestSucceeded) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(responseBody, &parseError);
        if (parseError.error == QJsonParseError::NoError) {
            contract = parseSessionContractDocument(document);
        } else {
            contract.errorText = tr("MixMan config response was not valid JSON.");
        }
    } else {
        contract.errorText = tr("MixMan session contract could not be loaded.");
    }
    emit mixManSessionContractVerified(contract);

    if (!contract.valid) {
        RestLibrarySessionWriteStatus status;
        status.operation = kSessionCreateOperation;
        status.statusCode = statusCode;
        status.errorText = contract.errorText;
        const auto diagnostic = diagnosticForReply(
                *pReply,
                responseBody,
                tr("Session contract"),
                QStringLiteral("GET"),
                status.errorText,
                false);
        if (status.errorText.isEmpty()) {
            status.errorText = diagnostic.summary;
        }
        emit requestDiagnosticUpdated(diagnostic);
        emit mixManSessionWriteStatusUpdated(status);
        return;
    }

    m_pendingResumeCredentials =
            readMixManSessionCredentials(m_settings, m_pendingSessionId);
    startMixManInstanceRegistration(
            m_pendingSessionId,
            m_pendingResumeCredentials,
            m_pendingSessionMetadata);
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

    const QByteArray responseBody = takeMetadataReplyBody(pReply);
    RestLibrarySessionWriteStatus status;
    status.operation = kSessionCreateOperation;
    status.statusCode = statusCodeFromReply(*pReply);
    status.errorReason = errorReasonFromResponse(responseBody);
    status.success = pReply->error() == QNetworkReply::NoError &&
            (isSuccessStatus(status.statusCode) || status.statusCode == 409);
    if (!status.success) {
        if ((status.statusCode == 0 || status.statusCode >= 500) &&
                m_pendingCreateAttempted) {
            startMixManInstanceRegistration(
                    m_pendingSessionId, {}, m_pendingSessionMetadata);
            return;
        }
        const auto diagnostic = diagnosticForReply(
                *pReply,
                responseBody,
                tr("Session create"),
                QStringLiteral("POST"),
                tr("MixMan session creation failed."),
                false);
        status.errorText = diagnostic.summary;
        kLogger.warning()
                << "MixMan session creation failed"
                << pReply->request().url().toString(QUrl::RemoveUserInfo)
                << "status" << status.statusCode
                << "body" << responseSnippet(responseBody);
        emit requestDiagnosticUpdated(diagnostic);
        emit mixManSessionWriteStatusUpdated(status);
        return;
    }

    if (status.statusCode != 409) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(responseBody, &parseError);
        const RestLibrarySession session = parseSessionDocument(document);
        if (parseError.error != QJsonParseError::NoError || session.id.isEmpty()) {
            startMixManInstanceRegistration(
                    m_pendingSessionId, {}, m_pendingSessionMetadata);
            return;
        }
        emit mixManSessionCreated(session);
    }
    emit mixManSessionWriteStatusUpdated(status);
    startMixManInstanceRegistration(
            m_pendingSessionId, {}, m_pendingSessionMetadata);
}

void RestLibraryClient::slotSessionRegisterFinished() {
    auto* pReply = qobject_cast<QNetworkReply*>(sender());
    if (!pReply) {
        return;
    }
    const bool staleReply = pReply->property(kRequestGenerationProperty).toInt() !=
            m_sessionRequestGeneration;
    pReply->deleteLater();
    if (staleReply) {
        return;
    }

    const QByteArray responseBody = takeMetadataReplyBody(pReply);
    RestLibrarySessionWriteStatus status;
    status.operation = kSessionRegisterOperation;
    status.statusCode = statusCodeFromReply(*pReply);
    status.errorReason = errorReasonFromResponse(responseBody);
    status.success = pReply->error() == QNetworkReply::NoError &&
            isSuccessStatus(status.statusCode);
    if (status.success) {
        const RestLibrarySessionRegistration registration =
                parseSessionRegistrationDocument(QJsonDocument::fromJson(responseBody));
        if (!registration.isValid()) {
            status.success = false;
            status.errorText = tr("MixMan instance registration response was incomplete.");
        } else {
            const RestLibrarySessionCredentials credentials{
                    registration.instance.instanceId,
                    registration.resumeToken};
            if (!writeMixManSessionCredentials(
                        m_settings, registration.sessionId, credentials)) {
                status.errorText = tr("MixMan instance is active, but resume credentials could not be saved securely.");
            }
            emit mixManSessionInstanceRegistered(registration);
        }
        emit mixManSessionWriteStatusUpdated(status);
        return;
    }

    if (status.statusCode == 404 && !m_pendingCreateAttempted) {
        startMixManSessionCreate(m_pendingSessionId, m_pendingSessionMetadata);
        return;
    }
    if (status.statusCode == 409 && m_pendingResumeCredentials.isComplete() &&
            !m_pendingFreshRegistrationRetried) {
        clearMixManSessionCredentials(m_settings, m_pendingSessionId);
        m_pendingResumeCredentials = {};
        m_pendingFreshRegistrationRetried = true;
        startMixManInstanceRegistration(
                m_pendingSessionId, {}, m_pendingSessionMetadata);
        return;
    }

    const auto diagnostic = diagnosticForReply(
            *pReply,
            responseBody,
            tr("Session registration"),
            QStringLiteral("POST"),
            tr("MixMan instance registration failed."),
            false);
    status.errorText = diagnostic.summary;
    emit requestDiagnosticUpdated(diagnostic);
    emit mixManSessionWriteStatusUpdated(status);
}

void RestLibraryClient::slotSessionFetchFinished() {
    auto* pReply = qobject_cast<QNetworkReply*>(sender());
    if (!pReply) {
        emit mixManSessionFetched({});
        return;
    }
    const bool staleReply =
            pReply->property(kRequestGenerationProperty).toInt() !=
            m_sessionRequestGeneration;
    const bool staleAuthoritativeReply =
            pReply->property(kAuthoritativeGenerationProperty).toInt() !=
            m_sessionAuthoritativeGeneration;
    pReply->deleteLater();
    if (staleReply || staleAuthoritativeReply) {
        return;
    }

    const QByteArray responseBody = takeMetadataReplyBody(pReply);
    const int statusCode = statusCodeFromReply(*pReply);
    if (pReply->error() != QNetworkReply::NoError || !isSuccessStatus(statusCode)) {
        kLogger.warning()
                << "MixMan session fetch failed"
                << pReply->request().url().toString(QUrl::RemoveUserInfo)
                << "status" << statusCode
                << "body" << responseSnippet(responseBody);
        emitReplyDiagnostic(
                *pReply,
                responseBody,
                tr("Session fetch"),
                QStringLiteral("GET"),
                tr("MixMan session fetch failed."),
                false);
        RestLibrarySessionWriteStatus status;
        status.operation = kSessionFetchOperation;
        status.statusCode = statusCode;
        status.errorReason = errorReasonFromResponse(responseBody);
        status.errorText = tr("MixMan session fetch failed.");
        emit mixManSessionWriteStatusUpdated(status);
        emit mixManSessionFetched({});
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(responseBody, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        emitReplyDiagnostic(
                *pReply,
                responseBody,
                tr("Session fetch"),
                QStringLiteral("GET"),
                tr("MixMan session response was not valid JSON."),
                false);
        emit mixManSessionFetched({});
        return;
    }
    emit mixManSessionFetched(parseSessionDocument(document));
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

    const QByteArray responseBody = takeMetadataReplyBody(pReply);
    RestLibrarySessionWriteStatus status;
    status.operation = pReply->property("operation").toString();
    status.mutationSequence =
            pReply->property(kMutationSequenceProperty).toULongLong();
    const QString requestMethod =
            pReply->property(kRequestMethodProperty).toString();
    status.statusCode = statusCodeFromReply(*pReply);
    status.errorReason = errorReasonFromResponse(responseBody);
    status.success =
            pReply->error() == QNetworkReply::NoError &&
            isSuccessStatus(status.statusCode);
    if (!status.success) {
        const auto diagnostic = diagnosticForReply(*pReply,
                responseBody,
                tr("Session write"),
                requestMethod.isEmpty() ? QStringLiteral("POST")
                                        : requestMethod,
                tr("MixMan session write failed."),
                false);
        status.errorText = diagnostic.summary;
        kLogger.warning() << "MixMan session write failed" << status.operation
                          << pReply->request().url().toString(
                                     QUrl::RemoveUserInfo)
                          << "status" << status.statusCode << "body"
                          << responseSnippet(responseBody);
        emit requestDiagnosticUpdated(diagnostic);
    } else if (status.operation == kSessionPlaybackOperation ||
            status.operation == kSessionSnapshotOperation ||
            status.operation == kSessionPlaybackControlClaimOperation ||
            status.operation == kSessionPlaybackControlRenewOperation ||
            status.operation == kSessionPlaybackControlReleaseOperation ||
            status.operation == kSessionCandidateSelectOperation ||
            status.operation == kSessionPolicyRefreshOperation) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(responseBody, &parseError);
        // Successful mutations return a revisioned authoritative state. Do not
        // discard it merely because a later read was started: that read may
        // have observed the state before this mutation committed. Consumers
        // compare server revisions before applying out-of-order responses.
        if (parseError.error == QJsonParseError::NoError) {
            const RestLibrarySession session = parseSessionDocument(document);
            if (!session.id.isEmpty() || !session.authoritative.raw.isEmpty()) {
                emit mixManSessionFetched(session);
            }
        }
    }
    emit mixManSessionWriteStatusUpdated(status);
}

void RestLibraryClient::finishDetailBatchIfComplete(int requestGeneration) {
    auto batchIt = m_trackBatches.find(requestGeneration);
    if (batchIt == m_trackBatches.end()) {
        return;
    }
    TrackRequestBatch batch = batchIt.value();
    if (batch.finishedDetailCount < batch.pendingDetails.size()) {
        return;
    }
    if (batch.pendingTracks.isEmpty() && batch.detailBatchFailed) {
        emitFailureForPurpose(
                batch.purpose,
                tr("Remote library track details could not be loaded."));
    } else {
        emitTracksForPurpose(batch.purpose, batch.pendingTracks);
    }
    m_trackBatches.erase(batchIt);
}

void RestLibraryClient::clearPendingDetails() {
    for (auto batchIt = m_trackBatches.begin(); batchIt != m_trackBatches.end(); ++batchIt) {
        for (const auto& detail : std::as_const(batchIt.value().pendingDetails)) {
            if (detail.reply) {
                m_requestContexts.remove(detail.reply);
                if (!detail.reply->isFinished()) {
                    detail.reply->abort();
                }
                detail.reply->deleteLater();
            }
        }
    }
    m_trackBatches.clear();
}

void RestLibraryClient::emitTracksForPurpose(
        RequestPurpose purpose,
        const QList<RestLibraryTrack>& tracks) {
    switch (purpose) {
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

void RestLibraryClient::emitFailureForPurpose(
        RequestPurpose purpose,
        const QString& message) {
    switch (purpose) {
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

RestLibraryCatalogPage RestLibraryClient::parseTrackCatalogPageForTesting(
        const QJsonDocument& document,
        bool* pValid) {
    bool valid = false;
    RestLibraryCatalogPage page = parseTrackCatalogPage(document, &valid);
    if (pValid) {
        *pValid = valid;
    }
    return page;
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

RestLibraryAuthoritativeState RestLibraryClient::parseAuthoritativeDocumentForTesting(
        const QJsonDocument& document) {
    return parseAuthoritativeDocument(document);
}

RestLibrarySessionContract RestLibraryClient::parseSessionContractDocumentForTesting(
        const QJsonDocument& document) {
    return parseSessionContractDocument(document);
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
    track.playCount = static_cast<int>(readDouble(object, {"play_count"}));
    track.favour = readDouble(object, {"favour"});
    track.energy = readDouble(object, {"energy"});

    const QString releaseDate = readString(object, {"release_date", "date"});
    if (!releaseDate.isEmpty()) {
        track.releaseDate = QDate::fromString(releaseDate.left(10), Qt::ISODate);
    }

    const QString sourceUrl = readString(
            object, {"permalink", "permalink_url", "source_url", "url"});
    if (!sourceUrl.isEmpty()) {
        track.sourceUrl = QUrl(sourceUrl);
    }
    const QString artworkUrl = readString(object, {"artwork", "artwork_url", "cover_url"});
    if (!artworkUrl.isEmpty()) {
        track.artworkUrl = QUrl(artworkUrl);
    }
    return track;
}

RestLibraryCatalogPage RestLibraryClient::parseTrackCatalogPage(
        const QJsonDocument& document,
        bool* pValid) {
    RestLibraryCatalogPage page;
    bool valid = document.isObject();
    if (valid) {
        const QJsonObject root = document.object();
        const QJsonValue itemsValue = root.value(QStringLiteral("items"));
        const QJsonValue cursorValue = root.value(QStringLiteral("next_cursor"));
        valid = root.contains(QStringLiteral("next_cursor")) &&
                itemsValue.isArray() &&
                (cursorValue.isNull() || cursorValue.isUndefined() || cursorValue.isString());
        if (valid) {
            const QJsonArray items = itemsValue.toArray();
            page.tracks.reserve(items.size());
            for (const QJsonValue& value : items) {
                if (!value.isObject()) {
                    valid = false;
                    break;
                }
                RestLibraryTrack track = parseTrackObject(value.toObject());
                if (track.remoteId.isEmpty()) {
                    valid = false;
                    break;
                }
                page.tracks.append(std::move(track));
            }
            if (cursorValue.isString()) {
                page.nextCursor = cursorValue.toString();
                valid = !page.nextCursor.trimmed().isEmpty();
            }
        }
    }
    if (!valid) {
        page = {};
    }
    if (pValid) {
        *pValid = valid;
    }
    return page;
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

RestLibraryAuthoritativeState RestLibraryClient::parseAuthoritativeDocument(
        const QJsonDocument& document) {
    RestLibraryAuthoritativeState state;
    if (!document.isObject()) {
        return state;
    }

    const QJsonObject root = document.object();
    const QJsonObject authoritative = root.value(QStringLiteral("authoritative")).isObject()
            ? root.value(QStringLiteral("authoritative")).toObject()
            : root;
    state.raw = authoritative;
    state.sessionId = readString(authoritative, {"session_id"});
    state.revision = static_cast<int>(readDouble(authoritative, {"revision"}));
    state.pressureRevision =
            static_cast<int>(readDouble(authoritative, {"pressure_revision"}));
    if (authoritative.value(QStringLiteral("playback")).isObject()) {
        state.playback = authoritative.value(QStringLiteral("playback")).toObject();
        state.playbackRevision =
                static_cast<int>(readDouble(state.playback, {"revision"}));
    }
    if (authoritative.value(QStringLiteral("pressure_state")).isObject()) {
        state.pressureState = authoritative.value(QStringLiteral("pressure_state")).toObject();
    }
    if (authoritative.value(QStringLiteral("selected_candidate")).isDouble()) {
        state.selectedCandidateId =
                authoritative.value(QStringLiteral("selected_candidate")).toInt();
    }
    if (authoritative.value(QStringLiteral("playback_controller")).isObject()) {
        state.playbackController =
                authoritative.value(QStringLiteral("playback_controller")).toObject();
        state.playbackLease.instanceId =
                readString(state.playbackController, {"instance_id"});
        state.playbackLease.leaseId =
                readString(state.playbackController, {"lease_id"});
        state.playbackLease.generation = static_cast<int>(
                readDouble(state.playbackController, {"generation"}));
        state.playbackLease.active =
                state.playbackController.value(QStringLiteral("active")).toBool(false);
    }
    if (authoritative.value(QStringLiteral("blocked")).isObject()) {
        state.blocked = authoritative.value(QStringLiteral("blocked")).toObject();
    }
    if (authoritative.value(QStringLiteral("queue")).isArray()) {
        state.queue = authoritative.value(QStringLiteral("queue")).toArray();
    }
    if (authoritative.value(QStringLiteral("intents")).isArray()) {
        state.intents = authoritative.value(QStringLiteral("intents")).toArray();
    }

    const QJsonObject tracksById = root.value(QStringLiteral("tracks_by_id")).toObject();
    const QJsonValue candidatesValue = authoritative.value(QStringLiteral("candidates"));
    const QJsonArray candidates = candidatesValue.isObject()
            ? candidatesValue.toObject().value(QStringLiteral("candidates")).toArray()
            : candidatesValue.toArray();
    state.policyPath.candidates.reserve(candidates.size());
    for (const QJsonValue& value : candidates) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject candidate = value.toObject();
        const QString remoteId = readString(candidate, {"track_id", "id"});
        QJsonObject trackObject = candidate.value(QStringLiteral("track")).toObject();
        if (trackObject.isEmpty()) {
            trackObject = objectForTrackId(tracksById, remoteId);
        }
        RestLibraryTrack track = parseTrackObject(trackObject);
        if (track.remoteId.isEmpty()) {
            track.remoteId = remoteId;
        }
        if (track.title.isEmpty()) {
            track.title = readString(candidate, {"title", "label"});
        }
        if (track.artist.isEmpty()) {
            track.artist = readString(candidate, {"artist"});
        }
        track.score = readDouble(candidate, {"score"});
        track.quality = track.score > 0.0 ? track.score : readDouble(candidate, {"quality"});
        track.recommendationPosition =
                static_cast<int>(readDouble(candidate, {"position"}));
        track.planned = candidate.value(QStringLiteral("planned")).toBool(false);
        track.moveType = readString(candidate, {"resolved_move_type", "move_type"});
        track.transitionRisk = readDouble(candidate, {"transition_risk"});
        track.transitionFit = readDouble(candidate, {"transition_fit"});
        track.targetDistance = readDouble(candidate, {"target_distance"});
        track.targetImprovement = readDouble(candidate, {"target_improvement"});
        track.region = readString(candidate, {"region_id", "region"});
        track.reasonCodes = readStringArray(candidate, QStringLiteral("reason_codes"));
        if (track.color.isEmpty()) {
            track.color = readString(candidate, {"color", "colour"});
        }
        if (track.sourceLabel.isEmpty()) {
            track.sourceLabel = QStringLiteral("MixMan Authoritative");
        }
        if (!track.remoteId.isEmpty()) {
            state.policyPath.candidates.append(std::move(track));
        }
    }

    const QJsonValue pathValue = authoritative.value(QStringLiteral("path"));
    const QJsonArray steps = pathValue.isObject()
            ? pathValue.toObject().value(QStringLiteral("steps")).toArray()
            : pathValue.toArray();
    state.policyPath.path.reserve(steps.size());
    for (const QJsonValue& value : steps) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject stepObject = value.toObject();
        const QString remoteId = readString(stepObject, {"track_id", "id"});
        QJsonObject trackObject = stepObject.value(QStringLiteral("track")).toObject();
        if (trackObject.isEmpty()) {
            trackObject = objectForTrackId(tracksById, remoteId);
        }
        const RestLibraryTrack track = parseTrackObject(trackObject);
        RestLibraryPathStep step;
        step.remoteId = track.remoteId.isEmpty() ? remoteId : track.remoteId;
        step.title = track.title.isEmpty() ? readString(stepObject, {"title", "label"})
                                           : track.title;
        step.artist = track.artist.isEmpty() ? readString(stepObject, {"artist"})
                                             : track.artist;
        step.score = readDouble(stepObject, {"score"});
        step.position = static_cast<int>(readDouble(stepObject, {"position"}));
        step.moveType = readString(stepObject, {"resolved_move_type", "move_type"});
        step.color = track.color.isEmpty() ? readString(stepObject, {"color", "colour"})
                                           : track.color;
        step.region = readString(stepObject, {"region_id", "region"});
        if (!step.remoteId.isEmpty()) {
            state.policyPath.path.append(std::move(step));
        }
    }

    return state;
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
        diagnostics.indexError = diagnostics.lastError;
        return diagnostics;
    }
    const QJsonObject object = document.object();
    diagnostics.indexReady = object.value(QStringLiteral("ready")).toBool(false);
    diagnostics.indexCount = static_cast<int>(readDouble(object, {"count"}));
    diagnostics.indexDimension = static_cast<int>(readDouble(object, {"dim"}));
    return diagnostics;
}

RestLibrarySessionContract RestLibraryClient::parseSessionContractDocument(
        const QJsonDocument& document) {
    RestLibrarySessionContract contract;
    if (!document.isObject()) {
        contract.errorText = tr("MixMan config response was not a JSON object.");
        return contract;
    }
    const QJsonObject root = document.object();
    if (!root.value(QStringLiteral("session_contract")).isObject()) {
        contract.errorText = tr("MixMan config did not advertise a session contract.");
        return contract;
    }
    const QJsonObject object = root.value(QStringLiteral("session_contract")).toObject();
    contract.version = object.value(QStringLiteral("version")).toInt();
    if (contract.version != 3) {
        contract.errorText = tr("MixMan session contract v3 is required (server advertised v%1).")
                                     .arg(contract.version);
        return contract;
    }
    contract.basePath = object.value(QStringLiteral("base_path")).toString();
    if (contract.basePath != QStringLiteral("/api/v3")) {
        contract.errorText = tr("MixMan session contract did not advertise /api/v3.");
        return contract;
    }
    const QJsonArray surfaces = object.value(QStringLiteral("surfaces")).toArray();
    QJsonObject mixxxSurface;
    for (const QJsonValue& value : surfaces) {
        const QJsonObject surface = value.toObject();
        if (surface.value(QStringLiteral("application")).toString() == QStringLiteral("mixxx") &&
                surface.value(QStringLiteral("surface")).toString() == QStringLiteral("rest_library")) {
            mixxxSurface = surface;
            break;
        }
    }
    if (mixxxSurface.isEmpty()) {
        contract.errorText = tr("MixMan session contract does not advertise mixxx/rest_library.");
        return contract;
    }
    contract.requiredScope = mixxxSurface.value(QStringLiteral("required_scope")).toString();
    if (contract.requiredScope != QStringLiteral("session:playback:dj")) {
        contract.errorText = tr("MixMan session contract did not require the DJ playback scope.");
        return contract;
    }
    contract.capabilities = readStringArray(mixxxSurface, QStringLiteral("capabilities"));
    const QStringList requiredCapabilities{
            QStringLiteral("session.read"),
            QStringLiteral("session.recommendations.read"),
            QStringLiteral("session.intent.write"),
            QStringLiteral("session.intent.ack"),
            QStringLiteral("session.plan.write"),
            QStringLiteral("session.action.write"),
            QStringLiteral("playback.claim"),
            QStringLiteral("playback.publish"),
    };
    for (const QString& capability : requiredCapabilities) {
        if (!contract.capabilities.contains(capability)) {
            contract.errorText = tr("MixMan session contract is missing capability %1.")
                                         .arg(capability);
            return contract;
        }
    }
    if (contract.capabilities.size() != requiredCapabilities.size()) {
        contract.errorText = tr("MixMan session contract capability set did not exactly match mixxx/rest_library.");
        return contract;
    }
    if (contract.capabilities.contains(QStringLiteral("session.controller.read"))) {
        contract.errorText = tr("MixMan exposed controller-only session data to Mixxx.");
        return contract;
    }
    contract.leaseTtlSeconds = std::max(
            1,
            object.value(QStringLiteral("lease_ttl_seconds")).toInt(30));
    contract.leaseRenewIntervalSeconds = std::clamp(
            object.value(QStringLiteral("renew_interval_seconds")).toInt(10),
            1,
            std::max(1, contract.leaseTtlSeconds - 1));
    contract.pauseGraceSeconds = std::clamp(
            object.value(QStringLiteral("pause_grace_seconds")).toInt(15),
            1,
            std::max(1, contract.leaseTtlSeconds - 1));
    contract.heartbeatIntervalSeconds = std::max(
            1, object.value(QStringLiteral("heartbeat_interval_seconds")).toInt(30));
    contract.activeTimeoutSeconds = std::max(
            contract.heartbeatIntervalSeconds + 1,
            object.value(QStringLiteral("active_timeout_seconds")).toInt(90));
    contract.valid = true;
    return contract;
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
    session.authoritative = parseAuthoritativeDocument(document);
    if (session.authoritative.sessionId.isEmpty()) {
        session.authoritative.sessionId = session.id;
    }
    return session;
}

RestLibrarySessionRegistration RestLibraryClient::parseSessionRegistrationDocument(
        const QJsonDocument& document) {
    RestLibrarySessionRegistration registration;
    if (!document.isObject()) {
        return registration;
    }
    const QJsonObject root = document.object();
    registration.sessionId = readString(root, {"session_id"});
    registration.resumeToken = readString(root, {"resume_token"});
    registration.heartbeatIntervalSeconds = std::max(
            1, root.value(QStringLiteral("heartbeat_interval_seconds")).toInt(30));
    registration.activeTimeoutSeconds = std::max(
            registration.heartbeatIntervalSeconds + 1,
            root.value(QStringLiteral("active_timeout_seconds")).toInt(90));
    const QJsonObject instance = root.value(QStringLiteral("instance")).toObject();
    registration.instance.instanceId = readString(instance, {"instance_id"});
    registration.instance.status = readString(instance, {"status"});
    registration.instance.capabilities =
            readStringArray(instance, QStringLiteral("capabilities"));
    registration.instance.active = instance.value(QStringLiteral("active")).toBool(false);
    return registration;
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
    const double rating = readDouble(object, {"rating", "dj_rating"});
    if (rating <= 0.0) {
        return 0;
    }
    if (rating <= 5.0) {
        return static_cast<int>(std::round(rating));
    }
    return std::clamp(static_cast<int>(std::round(rating)), 0, 100);
}

} // namespace mixxx::library::rest
