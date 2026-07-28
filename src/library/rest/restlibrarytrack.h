#pragma once

#include <QDate>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace mixxx::library::rest {

enum class RestLibraryCacheState {
    Missing,
    Downloading,
    Ready,
    Failed,
    Stale,
};

struct RestLibraryTrack {
    QString remoteId;
    QString reviewId;
    QString title;
    QString artist;
    QString album;
    QString genre;
    QString composer;
    QString comment;
    QString keyText;
    QString trackNumber;
    QString label;
    QDate releaseDate;
    double bpm = 0.0;
    double durationSeconds = 0.0;
    int rating = 0;
    QString sourceLabel;
    QUrl sourceUrl;
    QUrl artworkUrl;
    QString audioFileExtension;
    QString cachedFilePath;
    QString cacheError;
    int cacheStatusCode = 0;
    int cacheNetworkError = 0;
    double quality = 0.0;
    double score = 0.0;
    double transitionFit = 0.0;
    double transitionRisk = 0.0;
    double targetDistance = 0.0;
    double targetImprovement = 0.0;
    int recommendationEventId = 0;
    int recommendationItemId = 0;
    int recommendationPosition = 0;
    bool planned = false;
    QString mode;
    QString fallbackMode;
    QString moveType;
    QString color;
    QString region;
    QStringList reasonCodes;
    RestLibraryCacheState cacheState = RestLibraryCacheState::Missing;
};

} // namespace mixxx::library::rest
Q_DECLARE_METATYPE(mixxx::library::rest::RestLibraryTrack)
Q_DECLARE_METATYPE(QList<mixxx::library::rest::RestLibraryTrack>)
