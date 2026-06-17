#pragma once

#include <QDateTime>
#include <QList>
#include <QMap>
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

} // namespace mixxx::library::rest

Q_DECLARE_METATYPE(mixxx::library::rest::RestLibraryDiagnostics)
Q_DECLARE_METATYPE(mixxx::library::rest::RestLibraryPolicyPreset)
Q_DECLARE_METATYPE(QList<mixxx::library::rest::RestLibraryPolicyPreset>)
Q_DECLARE_METATYPE(mixxx::library::rest::RestLibraryPathStep)
Q_DECLARE_METATYPE(QList<mixxx::library::rest::RestLibraryPathStep>)
Q_DECLARE_METATYPE(mixxx::library::rest::RestLibraryPolicyPath)
