#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QList>
#include <QMap>
#include <QJsonObject>
#include <QString>

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
    bool success = false;
    int statusCode = 0;
    QString errorText;
};

struct RestLibrarySessionContract {
    bool valid = false;
    int version = 0;
    int leaseTtlSeconds = 30;
    int leaseRenewIntervalSeconds = 10;
    int pauseGraceSeconds = 15;
    QString errorText;
};

struct RestLibrarySessionSnapshot {
    QString clientId;
    QString surface;
    QString source;
    QString currentTrackId;
    QString cue;
    QString playbackState;
    QJsonObject snapshot;
    QJsonObject metadata;
};

struct RestLibrarySessionPlayback {
    QString clientId;
    QString surface;
    QString source;
    QString currentTrackId;
    QString cue;
    QString playbackState;
    QJsonObject currentTrack;
    QJsonObject metadata;
};

struct RestLibrarySessionIntent {
    QString clientId;
    QString source;
    QString surface;
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
    QJsonObject selectedCandidate;
    QJsonObject playbackController;
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
Q_DECLARE_METATYPE(mixxx::library::rest::RestLibraryAuthoritativeState)
Q_DECLARE_METATYPE(mixxx::library::rest::RestLibraryPolicyPreset)
Q_DECLARE_METATYPE(QList<mixxx::library::rest::RestLibraryPolicyPreset>)
Q_DECLARE_METATYPE(mixxx::library::rest::RestLibraryPathStep)
Q_DECLARE_METATYPE(QList<mixxx::library::rest::RestLibraryPathStep>)
Q_DECLARE_METATYPE(mixxx::library::rest::RestLibraryPolicyPath)
