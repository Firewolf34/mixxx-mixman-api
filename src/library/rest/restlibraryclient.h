#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
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
    void lookupTrack(const RestLibrarySettings& settings, const TrackPointer& pTrack);
    void fetchRecommendations(
            const RestLibrarySettings& settings,
            const QString& remoteId);
    void fetchMixManDiagnostics(const RestLibrarySettings& settings);
    void fetchMixManPolicyPresets(const RestLibrarySettings& settings);
    void fetchMixManPolicyPath(
            const RestLibrarySettings& settings,
            const QString& remoteId);

    static QList<RestLibraryTrack> parseTrackListDocumentForTesting(
            const QJsonDocument& document);
    static RestLibraryTrack parseTrackObjectForTesting(
            const QJsonObject& object);
    static RestLibraryPolicyPath parsePolicyPathDocumentForTesting(
            const QJsonDocument& document);
    static QList<RestLibraryPolicyPreset> parsePolicyPresetsDocumentForTesting(
            const QJsonDocument& document);
    static RestLibraryDiagnostics parseIndexStatusDocumentForTesting(
            const QJsonDocument& document);

  signals:
    void tracksFetched(const QList<mixxx::library::rest::RestLibraryTrack>& tracks);
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
    void fetchFailed(const QString& message);

  private slots:
    void slotTrackListFinished();
    void slotTrackDetailFinished();
    void slotHealthFinished();
    void slotIndexStatusFinished();
    void slotPolicyPresetsFinished();
    void slotPolicyPathFinished();

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

    QNetworkRequest newRequest(const QString& path, int limit) const;
    QNetworkRequest newDetailRequest(const QString& remoteId) const;
    void startDetailRequests(const QStringList& remoteIds);
    void finishDetailBatchIfComplete();
    void clearPendingDetails();
    void emitTracksForCurrentPurpose(const QList<RestLibraryTrack>& tracks);
    void emitFailureForCurrentPurpose(const QString& message);

    static QList<RestLibraryTrack> parseTrackListDocument(
            const QJsonDocument& document,
            QStringList* pRemoteIds);
    static RestLibraryTrack parseTrackObject(const QJsonObject& object);
    static RestLibraryPolicyPath parsePolicyPathDocument(const QJsonDocument& document);
    static QList<RestLibraryPolicyPreset> parsePolicyPresetsDocument(
            const QJsonDocument& document);
    static RestLibraryDiagnostics parseIndexStatusDocument(const QJsonDocument& document);
    static QString readString(
            const QJsonObject& object,
            std::initializer_list<QString> keys);
    static double readDouble(
            const QJsonObject& object,
            std::initializer_list<QString> keys);
    static int readRating(const QJsonObject& object);

    QPointer<QNetworkAccessManager> m_pNetworkAccessManager;
    RestLibrarySettings m_settings;
    RequestPurpose m_requestPurpose = RequestPurpose::Tracks;
    QList<RestLibraryTrack> m_pendingTracks;
    QVector<PendingDetail> m_pendingDetails;
    int m_finishedDetailCount = 0;
    bool m_detailBatchFailed = false;
};

} // namespace mixxx::library::rest
