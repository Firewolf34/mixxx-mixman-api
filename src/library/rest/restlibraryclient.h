#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QList>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVector>

#include "library/rest/restlibrarymixman.h"
#include "library/rest/restlibrarysettings.h"
#include "library/rest/restlibrarytrack.h"
#include "track/track_decl.h"

class QNetworkAccessManager;
class QNetworkRequest;

namespace mixxx::library::rest {

class RestLibraryClient final : public QObject {
    Q_OBJECT

  public:
    explicit RestLibraryClient(
            QNetworkAccessManager* pNetworkAccessManager,
            QObject* parent = nullptr);

    void fetchTracks(const RestLibrarySettings& settings);
    void fetchTrackCatalogPage(
            const RestLibrarySettings& settings,
            const QString& cursor = {});
    void cancelTrackCatalogRequest();
    int bufferedMetadataReplyCountForTesting() const {
        return m_metadataResponseBodies.size();
    }
    void lookupTrack(const RestLibrarySettings& settings, const TrackPointer& pTrack);
    void fetchRecommendations(
            const RestLibrarySettings& settings,
            const QString& remoteId);
    void fetchMixManDiagnostics(const RestLibrarySettings& settings);
    void fetchMixManPolicyPresets(const RestLibrarySettings& settings);
    void fetchMixManPolicyPath(
            const RestLibrarySettings& settings,
            const QString& remoteId,
            const QString& sessionId = {},
            const QString& previousTrackId = {},
            const QStringList& recentTrackIds = {});
    void createMixManSession(
            const RestLibrarySettings& settings,
            const QString& sessionId,
            const QJsonObject& metadata = {});
    void registerMixManSessionInstance(
            const RestLibrarySettings& settings,
            const QString& sessionId,
            const RestLibrarySessionCredentials& credentials = {},
            const QJsonObject& metadata = {});
    QNetworkReply* disconnectMixManSessionInstance(
            const RestLibrarySettings& settings,
            const QString& sessionId,
            const QString& instanceId);
    void fetchMixManSession(
            const RestLibrarySettings& settings,
            const QString& sessionId,
            const QString& instanceId);
    void publishMixManSessionPlayback(
            const RestLibrarySettings& settings,
            const QString& sessionId,
            const RestLibrarySessionPlayback& playback,
            quint64 mutationSequence = 0);
    void publishMixManSessionSnapshot(
            const RestLibrarySettings& settings,
            const QString& sessionId,
            const RestLibrarySessionSnapshot& snapshot,
            quint64 mutationSequence = 0);
    void updateMixManSessionIntent(
            const RestLibrarySettings& settings,
            const QString& sessionId,
            const RestLibrarySessionIntent& intent);
    void sendMixManSessionHeartbeat(
            const RestLibrarySettings& settings,
            const QString& sessionId,
            const QString& instanceId,
            const QJsonObject& metadata = {});
    void claimMixManPlaybackControl(
            const RestLibrarySettings& settings,
            const QString& sessionId,
            const QString& instanceId,
            int ttlSeconds,
            const QJsonObject& metadata = {},
            quint64 mutationSequence = 0);
    void renewMixManPlaybackControl(
            const RestLibrarySettings& settings,
            const QString& sessionId,
            const RestLibraryPlaybackLease& lease,
            quint64 mutationSequence = 0);
    void releaseMixManPlaybackControl(
            const RestLibrarySettings& settings,
            const QString& sessionId,
            const RestLibraryPlaybackLease& lease,
            quint64 mutationSequence = 0);
    void selectMixManSessionCandidate(
            const RestLibrarySettings& settings,
            const QString& sessionId,
            const QString& trackId,
            const RestLibraryPlaybackLease& lease,
            const QJsonObject& metadata = {},
            quint64 mutationSequence = 0);
    void publishMixManPolicyRefreshAction(
            const RestLibrarySettings& settings,
            const QString& sessionId,
            const QString& instanceId,
            const QJsonObject& metadata = {},
            quint64 mutationSequence = 0);
    void testMixManConnection(
            const RestLibrarySettings& settings,
            const QString& clientId = {},
            bool createSession = false);
    void cancelMixManConnectionTest();
    void invalidateMixManRequests();

    static QList<RestLibraryTrack> parseTrackListDocumentForTesting(
            const QJsonDocument& document);
    static RestLibraryTrack parseTrackObjectForTesting(
            const QJsonObject& object);
    static RestLibraryCatalogPage parseTrackCatalogPageForTesting(
            const QJsonDocument& document,
            bool* pValid = nullptr);
    static RestLibraryPolicyPath parsePolicyPathDocumentForTesting(
            const QJsonDocument& document);
    static QList<RestLibraryPolicyPreset> parsePolicyPresetsDocumentForTesting(
            const QJsonDocument& document);
    static RestLibraryDiagnostics parseIndexStatusDocumentForTesting(
            const QJsonDocument& document);
    static RestLibrarySession parseSessionDocumentForTesting(
            const QJsonDocument& document);
    static RestLibraryAuthoritativeState parseAuthoritativeDocumentForTesting(
            const QJsonDocument& document);
    static RestLibrarySessionContract parseSessionContractDocumentForTesting(
            const QJsonDocument& document);

  signals:
    void tracksFetched(const QList<mixxx::library::rest::RestLibraryTrack>& tracks);
    void trackCatalogPageFetched(
            const mixxx::library::rest::RestLibraryCatalogPage& page);
    void trackCatalogFetchFailed(const QString& message);
    void trackLookupSucceeded(const QString& remoteId);
    void trackLookupMissed(const QString& message);
    void recommendationsFetched(
            const QList<mixxx::library::rest::RestLibraryTrack>& tracks);
    void diagnosticsUpdated(
            const mixxx::library::rest::RestLibraryDiagnostics& diagnostics);
    void policyPresetsFetched(
            const QList<mixxx::library::rest::RestLibraryPolicyPreset>& presets);
    void mixManPolicyPathFetched(
            const mixxx::library::rest::RestLibraryPolicyPath& policyPath);
    void mixManSessionCreated(
            const mixxx::library::rest::RestLibrarySession& session);
    void mixManSessionInstanceRegistered(
            const mixxx::library::rest::RestLibrarySessionRegistration& registration);
    void mixManSessionFetched(
            const mixxx::library::rest::RestLibrarySession& session);
    void mixManSessionWriteStatusUpdated(
            const mixxx::library::rest::RestLibrarySessionWriteStatus& status);
    void mixManSessionContractVerified(
            const mixxx::library::rest::RestLibrarySessionContract& contract);
    void requestDiagnosticUpdated(
            const mixxx::library::rest::RestLibraryRequestDiagnostic& diagnostic);
    void connectionTestFinished(bool success);
    void fetchFailed(const QString& message);

  private slots:
    void slotTrackListFinished();
    void slotTrackCatalogFinished();
    void slotTrackDetailFinished();
    void slotHealthFinished();
    void slotIndexStatusFinished();
    void slotPolicyPresetsFinished();
    void slotPolicyPathFinished();
    void slotSessionContractFinished();
    void slotSessionCreateFinished();
    void slotSessionRegisterFinished();
    void slotSessionFetchFinished();
    void slotSessionWriteFinished();
    void slotConnectionTestHealthFinished();
    void slotConnectionTestConfigFinished();
    void slotConnectionTestIndexFinished();
    void slotConnectionTestTracksFinished();
    void slotConnectionTestSessionFinished();
    void slotConnectionTestRegisterFinished();
    void slotConnectionTestStateFinished();
    void slotConnectionTestClaimFinished();
    void slotConnectionTestReleaseFinished();
    void slotConnectionTestDisconnectFinished();

  private:
    struct PendingDetail {
        QPointer<QNetworkReply> reply;
        QString remoteId;
    };

    enum class RequestPurpose {
        Tracks,
        TrackLookup,
        Recommendations,
    };

    struct RequestContext {
        RestLibrarySettings settings;
        RequestPurpose purpose = RequestPurpose::Tracks;
        int generation = 0;
        QString stage;
        QString method;
        QString remoteId;
    };

    struct TrackRequestBatch {
        RestLibrarySettings settings;
        RequestPurpose purpose = RequestPurpose::Tracks;
        QList<RestLibraryTrack> pendingTracks;
        QVector<PendingDetail> pendingDetails;
        int finishedDetailCount = 0;
        bool detailBatchFailed = false;
    };

    QNetworkRequest newRequest(const QString& path, int limit) const;
    QNetworkRequest newRequest(
            const RestLibrarySettings& settings,
            const QString& path,
            int limit) const;
    QNetworkRequest newJsonRequest(const QString& path) const;
    QNetworkRequest newDetailRequest(
            const RestLibrarySettings& settings,
            const QString& remoteId) const;
    QNetworkReply* startConnectionTestGet(
            const QString& path,
            int limit,
            const QString& stage);
    QNetworkReply* startConnectionTestPost(
            const QString& path,
            const QJsonObject& payload,
            const QString& stage);
    void finishConnectionTestStep(
            QNetworkReply* pReply,
            const QString& failureSummary,
            bool* pSuccess);
    void emitReplyDiagnostic(
            const QNetworkReply& reply,
            const QByteArray& responseBody,
            const QString& stage,
            const QString& method,
            const QString& fallbackSummary,
            bool success);
    RestLibraryRequestDiagnostic diagnosticForReply(
            const QNetworkReply& reply,
            const QByteArray& responseBody,
            const QString& stage,
            const QString& method,
            const QString& fallbackSummary,
            bool success) const;
    QString diagnosticSummary(
            const RestLibraryRequestDiagnostic& diagnostic,
            const QString& fallbackSummary) const;
    void emitConfigurationDiagnostic(const QString& summary);
    void monitorMetadataReply(QNetworkReply* pReply);
    void consumeMetadataReply(QNetworkReply* pReply, bool abortIfOversized);
    QByteArray takeMetadataReplyBody(QNetworkReply* pReply);
    void forgetConnectionTestReply(QNetworkReply* pReply);
    void startDetailRequests(int requestGeneration, const QStringList& remoteIds);
    void finishDetailBatchIfComplete(int requestGeneration);
    void clearPendingDetails();
    void emitTracksForPurpose(
            RequestPurpose purpose,
            const QList<RestLibraryTrack>& tracks);
    void emitFailureForPurpose(RequestPurpose purpose, const QString& message);
    void requestMixManPlaybackControl(
            const RestLibrarySettings& settings,
            const QString& sessionId,
            const RestLibraryPlaybackLease& lease,
            const QString& action,
            const QString& operation,
            const QJsonObject& metadata,
            quint64 mutationSequence);
    void startMixManInstanceRegistration(
            const QString& sessionId,
            const RestLibrarySessionCredentials& credentials,
            const QJsonObject& metadata);
    void startMixManSessionCreate(
            const QString& sessionId,
            const QJsonObject& metadata);
    void startConnectionTestDisconnect();

    static QList<RestLibraryTrack> parseTrackListDocument(
            const QJsonDocument& document,
            QStringList* pRemoteIds);
    static RestLibraryTrack parseTrackObject(const QJsonObject& object);
    static RestLibraryCatalogPage parseTrackCatalogPage(
            const QJsonDocument& document,
            bool* pValid);
    static RestLibraryPolicyPath parsePolicyPathDocument(const QJsonDocument& document);
    static QList<RestLibraryPolicyPreset> parsePolicyPresetsDocument(
            const QJsonDocument& document);
    static RestLibraryDiagnostics parseIndexStatusDocument(const QJsonDocument& document);
    static RestLibrarySessionContract parseSessionContractDocument(
            const QJsonDocument& document);
    static RestLibrarySession parseSessionDocument(const QJsonDocument& document);
    static RestLibrarySessionRegistration parseSessionRegistrationDocument(
            const QJsonDocument& document);
    static RestLibraryAuthoritativeState parseAuthoritativeDocument(
            const QJsonDocument& document);
    static QString readString(
            const QJsonObject& object,
            std::initializer_list<QString> keys);
    static double readDouble(
            const QJsonObject& object,
            std::initializer_list<QString> keys);
    static int readRating(const QJsonObject& object);

    QPointer<QNetworkAccessManager> m_pNetworkAccessManager;
    QPointer<QNetworkReply> m_pTrackCatalogReply;
    RestLibrarySettings m_settings;
    QHash<QNetworkReply*, RequestContext> m_requestContexts;
    QHash<QNetworkReply*, QByteArray> m_metadataResponseBodies;
    QHash<int, TrackRequestBatch> m_trackBatches;
    int m_trackListRequestGeneration = 0;
    int m_trackCatalogRequestGeneration = 0;
    int m_policyPathRequestGeneration = 0;
    int m_mixManDiagnosticsRequestGeneration = 0;
    int m_policyPresetsRequestGeneration = 0;
    int m_sessionRequestGeneration = 0;
    int m_sessionAuthoritativeGeneration = 0;
    int m_connectionTestRequestGeneration = 0;
    QString m_pendingSessionId;
    QJsonObject m_pendingSessionMetadata;
    RestLibrarySessionCredentials m_pendingResumeCredentials;
    bool m_pendingFreshRegistrationRetried = false;
    bool m_pendingCreateAttempted = false;
    bool m_connectionTestFailed = false;
    RestLibrarySettings m_connectionTestSettings;
    QString m_connectionTestClientId;
    QString m_connectionTestSessionId;
    QString m_connectionTestInstanceId;
    RestLibraryPlaybackLease m_connectionTestLease;
    bool m_connectionTestCreateSession = false;
    QVector<QPointer<QNetworkReply>> m_connectionTestReplies;
};

} // namespace mixxx::library::rest
