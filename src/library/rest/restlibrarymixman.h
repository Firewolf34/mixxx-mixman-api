#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QList>
#include <QMap>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "library/rest/restlibrarytrack.h"

namespace mixxx::library::rest {

struct RestLibraryDiagnostics {
    bool healthKnown = false;
    bool healthOk = false;
    bool indexKnown = false;
    bool indexReady = false;
    int indexCount = 0;
    int indexDimension = 0;
    int lastStatusCode = 0;
    int lastLatencyMillis = 0;
    QString lastError;
    QString healthError;
    QString indexError;
};

struct RestLibraryRequestDiagnostic {
    QString stage;
    QString method;
    QString url;
    bool success = false;
    int statusCode = 0;
    int networkError = 0;
    int elapsedMillis = 0;
    QString summary;
    QString errorText;
    QString responseSnippet;
};

struct RestLibrarySessionWriteStatus {
    QString operation;
    quint64 mutationSequence = 0;
    bool success = false;
    int statusCode = 0;
    QString errorReason;
    QString errorText;
};

struct RestLibrarySessionContract {
    bool valid = false;
    int version = 0;
    QString basePath;
    QString requiredScope;
    QStringList capabilities;
    int leaseTtlSeconds = 30;
    int leaseRenewIntervalSeconds = 10;
    int pauseGraceSeconds = 15;
    int heartbeatIntervalSeconds = 30;
    int activeTimeoutSeconds = 90;
    QString errorText;
};

struct RestLibrarySessionCredentials {
    QString instanceId;
    QString resumeToken;

    bool isComplete() const {
        return !instanceId.trimmed().isEmpty() && !resumeToken.trimmed().isEmpty();
    }
};

struct RestLibrarySessionInstance {
    QString instanceId;
    QString status;
    QStringList capabilities;
    bool active = false;
};

struct RestLibrarySessionRegistration {
    QString sessionId;
    RestLibrarySessionInstance instance;
    QString resumeToken;
    int heartbeatIntervalSeconds = 30;
    int activeTimeoutSeconds = 90;

    bool isValid() const {
        return !sessionId.trimmed().isEmpty() &&
                !instance.instanceId.trimmed().isEmpty() &&
                !resumeToken.trimmed().isEmpty();
    }
};

struct RestLibraryPlaybackLease {
    QString instanceId;
    QString leaseId;
    int generation = 0;
    bool active = false;

    bool isValidFor(const QString& expectedInstanceId) const {
        return active && instanceId == expectedInstanceId &&
                !leaseId.trimmed().isEmpty() && generation > 0;
    }
};

struct RestLibrarySessionSnapshot {
    RestLibraryPlaybackLease lease;
    QString currentTrackId;
    QString cue;
    QString playbackState;
    QJsonObject snapshot;
    QJsonObject metadata;
};

struct RestLibrarySessionPlayback {
    RestLibraryPlaybackLease lease;
    QString currentTrackId;
    QString cue;
    QString playbackState;
    QJsonObject currentTrack;
    QJsonObject metadata;
};

struct RestLibrarySessionIntent {
    QString instanceId;
    QString status = QStringLiteral("active");
    QString policyPreset;
    QString targetColor;
    bool targetColorEnabled = false;
    bool targetEnergyEnabled = false;
    double targetEnergy = 0.0;
    QJsonObject metadata;
};

struct RestLibraryPolicyPreset {
    QString key;
    QString label;
    QString description;
};

struct RestLibraryPathStep {
    QString remoteId;
    QString title;
    QString artist;
    double score = 0.0;
    int position = 0;
    QString moveType;
    QString color;
    QString region;
};

struct RestLibraryPolicyPath {
    QList<RestLibraryTrack> candidates;
    QList<RestLibraryPathStep> path;
    QString policyPreset;
    QString resolvedMoveType;
    int recommendationEventId = 0;
    double selectedBranchScore = 0.0;
};

struct RestLibraryAuthoritativeState {
    QString sessionId;
    int revision = 0;
    int playbackRevision = 0;
    int pressureRevision = 0;
    RestLibraryPolicyPath policyPath;
    QJsonObject playback;
    QJsonObject pressureState;
    int selectedCandidateId = 0;
    QJsonObject playbackController;
    RestLibraryPlaybackLease playbackLease;
    QJsonObject blocked;
    QJsonArray queue;
    QJsonArray intents;
    QJsonObject raw;
};

struct RestLibrarySession {
    QString id;
    QString displayName;
    QString status;
    QJsonObject snapshot;
    QJsonObject intent;
    QJsonObject policyEvent;
    QJsonArray clients;
    QJsonArray recentEvents;
    RestLibraryAuthoritativeState authoritative;
};

} // namespace mixxx::library::rest

Q_DECLARE_METATYPE(mixxx::library::rest::RestLibraryDiagnostics)
Q_DECLARE_METATYPE(mixxx::library::rest::RestLibraryRequestDiagnostic)
Q_DECLARE_METATYPE(mixxx::library::rest::RestLibrarySession)
Q_DECLARE_METATYPE(mixxx::library::rest::RestLibrarySessionWriteStatus)
Q_DECLARE_METATYPE(mixxx::library::rest::RestLibrarySessionContract)
Q_DECLARE_METATYPE(mixxx::library::rest::RestLibrarySessionRegistration)
Q_DECLARE_METATYPE(mixxx::library::rest::RestLibraryAuthoritativeState)
Q_DECLARE_METATYPE(mixxx::library::rest::RestLibraryPolicyPreset)
Q_DECLARE_METATYPE(QList<mixxx::library::rest::RestLibraryPolicyPreset>)
Q_DECLARE_METATYPE(mixxx::library::rest::RestLibraryPathStep)
Q_DECLARE_METATYPE(QList<mixxx::library::rest::RestLibraryPathStep>)
Q_DECLARE_METATYPE(mixxx::library::rest::RestLibraryPolicyPath)
