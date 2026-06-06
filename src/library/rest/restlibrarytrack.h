#pragma once

#include <QDate>
#include <QList>
#include <QMetaType>
#include <QString>
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
    RestLibraryCacheState cacheState = RestLibraryCacheState::Missing;
};

} // namespace mixxx::library::rest
Q_DECLARE_METATYPE(mixxx::library::rest::RestLibraryTrack)
Q_DECLARE_METATYPE(QList<mixxx::library::rest::RestLibraryTrack>)
