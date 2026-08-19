#include "library/rest/restlibrarytrackstore.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>

#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "track/track.h"
#include "track/trackref.h"
#include "util/logger.h"

namespace mixxx::library::rest {

namespace {

const Logger kLogger("RestLibraryTrackStore");

QString normalizedLocation(const QString& location) {
    return QDir::cleanPath(QDir::fromNativeSeparators(location));
}

bool isUsableTrack(const TrackPointer& pTrack) {
    return pTrack && pTrack->getId().isValid() &&
            QFileInfo::exists(pTrack->getLocation());
}

} // namespace

RestLibraryTrackStore::RestLibraryTrackStore(
        TrackCollectionManager* pTrackCollectionManager)
        : m_pTrackCollectionManager(pTrackCollectionManager) {
}

TrackPointer RestLibraryTrackStore::materializeTrack(
        const RestLibraryTrack& remoteTrack,
        const QString& cacheIdentity) const {
    if (!m_pTrackCollectionManager || cacheIdentity.trimmed().isEmpty() ||
            remoteTrack.remoteId.trimmed().isEmpty()) {
        return {};
    }

    const TrackPointer pMappedTrack = mappedTrack(cacheIdentity, remoteTrack.remoteId);
    if (isUsableTrack(pMappedTrack) && !isCacheArtifact(pMappedTrack->getId())) {
        return pMappedTrack;
    }

    if (remoteTrack.cacheState != RestLibraryCacheState::Ready ||
            remoteTrack.cachedFilePath.trimmed().isEmpty()) {
        return {};
    }
    const QString location = normalizedLocation(remoteTrack.cachedFilePath);
    TrackPointer pTrack = cacheArtifactForPath(
            cacheIdentity,
            remoteTrack.remoteId,
            location);
    bool registeredArtifact = false;
    if (!pTrack) {
        bool alreadyInLibrary = false;
        pTrack = m_pTrackCollectionManager->getOrAddTrack(
                TrackRef::fromFilePath(location), &alreadyInLibrary);
        if (!pTrack || !pTrack->getId().isValid() ||
                !registerCacheArtifact(
                        cacheIdentity,
                        remoteTrack.remoteId,
                        pTrack->getId())) {
            return {};
        }
        registeredArtifact = true;
    }

    if (registeredArtifact) {
        if (!m_pTrackCollectionManager->unhideTracks({pTrack->getId()})) {
            kLogger.warning() << "Failed to make a REST cache artifact available";
            return {};
        }
        applyRemoteMetadata(remoteTrack, pTrack);
        m_pTrackCollectionManager->saveTrack(pTrack);
    }

    // Keep an explicit mapping to an ordinary local track even if its file is
    // temporarily unavailable. The cache artifact is only a playback fallback
    // in that case, not a replacement for the server-confirmed identity.
    if (!pMappedTrack || isCacheArtifact(pMappedTrack->getId())) {
        rememberPreferredTrack(cacheIdentity, remoteTrack.remoteId, pTrack->getId());
    }
    return pTrack;
}

TrackPointer RestLibraryTrackStore::mappedTrack(
        const QString& cacheIdentity,
        const QString& remoteId) const {
    if (!m_pTrackCollectionManager || cacheIdentity.trimmed().isEmpty() ||
            remoteId.trimmed().isEmpty()) {
        return {};
    }
    QSqlQuery query(m_pTrackCollectionManager->internalCollection()->database());
    query.prepare(QStringLiteral(
            "SELECT track_id FROM rest_library_track_mappings "
            "WHERE cache_identity=:cache_identity AND remote_id=:remote_id"));
    query.bindValue(QStringLiteral(":cache_identity"), cacheIdentity);
    query.bindValue(QStringLiteral(":remote_id"), remoteId);
    if (!query.exec()) {
        kLogger.warning() << "Failed to resolve a REST track mapping" << query.lastError();
        return {};
    }
    if (!query.next()) {
        return {};
    }
    return m_pTrackCollectionManager->getTrackById(TrackId(query.value(0)));
}

bool RestLibraryTrackStore::rememberLocalMapping(
        const QString& cacheIdentity,
        const QString& remoteId,
        const TrackPointer& pTrack) const {
    if (!isUsableTrack(pTrack) || isCacheArtifact(pTrack->getId())) {
        return false;
    }
    return rememberPreferredTrack(cacheIdentity, remoteId, pTrack->getId());
}

QString RestLibraryTrackStore::remoteIdForTrack(
        const QString& cacheIdentity,
        const TrackPointer& pTrack) const {
    if (!m_pTrackCollectionManager || cacheIdentity.trimmed().isEmpty() ||
            !pTrack || !pTrack->getId().isValid()) {
        return {};
    }
    QSqlQuery query(m_pTrackCollectionManager->internalCollection()->database());
    query.prepare(QStringLiteral(
            "SELECT remote_id FROM rest_library_track_mappings "
            "WHERE cache_identity=:cache_identity AND track_id=:track_id "
            "UNION ALL "
            "SELECT remote_id FROM rest_library_cache_tracks "
            "WHERE cache_identity=:cache_identity AND track_id=:track_id "
            "LIMIT 1"));
    query.bindValue(QStringLiteral(":cache_identity"), cacheIdentity);
    query.bindValue(QStringLiteral(":track_id"), pTrack->getId().toVariant());
    if (!query.exec()) {
        kLogger.warning() << "Failed to reverse a REST track mapping" << query.lastError();
        return {};
    }
    return query.next() ? query.value(0).toString() : QString();
}

bool RestLibraryTrackStore::isCacheArtifact(TrackId trackId) const {
    if (!m_pTrackCollectionManager || !trackId.isValid()) {
        return false;
    }
    QSqlQuery query(m_pTrackCollectionManager->internalCollection()->database());
    query.prepare(QStringLiteral(
            "SELECT 1 FROM rest_library_cache_tracks WHERE track_id=:track_id"));
    query.bindValue(QStringLiteral(":track_id"), trackId.toVariant());
    if (!query.exec()) {
        kLogger.warning() << "Failed to classify a REST cache track" << query.lastError();
        return false;
    }
    return query.next();
}

TrackPointer RestLibraryTrackStore::cacheArtifactForPath(
        const QString& cacheIdentity,
        const QString& remoteId,
        const QString& filePath) const {
    QSqlQuery query(m_pTrackCollectionManager->internalCollection()->database());
    query.prepare(QStringLiteral(
            "SELECT track_id FROM rest_library_cache_tracks "
            "WHERE cache_identity=:cache_identity AND remote_id=:remote_id "
            "ORDER BY track_id DESC"));
    query.bindValue(QStringLiteral(":cache_identity"), cacheIdentity);
    query.bindValue(QStringLiteral(":remote_id"), remoteId);
    if (!query.exec()) {
        kLogger.warning() << "Failed to resolve a REST cache artifact" << query.lastError();
        return {};
    }
    while (query.next()) {
        const TrackPointer pTrack =
                m_pTrackCollectionManager->getTrackById(TrackId(query.value(0)));
        if (pTrack && normalizedLocation(pTrack->getLocation()) == filePath &&
                QFileInfo::exists(filePath)) {
            return pTrack;
        }
    }
    return {};
}

bool RestLibraryTrackStore::registerCacheArtifact(
        const QString& cacheIdentity,
        const QString& remoteId,
        TrackId trackId) const {
    QSqlQuery query(m_pTrackCollectionManager->internalCollection()->database());
    query.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO rest_library_cache_tracks "
            "(track_id, cache_identity, remote_id) "
            "VALUES (:track_id, :cache_identity, :remote_id)"));
    query.bindValue(QStringLiteral(":track_id"), trackId.toVariant());
    query.bindValue(QStringLiteral(":cache_identity"), cacheIdentity);
    query.bindValue(QStringLiteral(":remote_id"), remoteId);
    if (!query.exec()) {
        kLogger.warning() << "Failed to register a REST cache artifact" << query.lastError();
        return false;
    }

    // A track location must never be silently reclassified across credential
    // contexts. INSERT OR IGNORE handles a repeated registration, then this
    // check distinguishes that from a conflicting existing registry row.
    query.prepare(QStringLiteral(
            "SELECT 1 FROM rest_library_cache_tracks "
            "WHERE track_id=:track_id AND cache_identity=:cache_identity "
            "AND remote_id=:remote_id"));
    query.bindValue(QStringLiteral(":track_id"), trackId.toVariant());
    query.bindValue(QStringLiteral(":cache_identity"), cacheIdentity);
    query.bindValue(QStringLiteral(":remote_id"), remoteId);
    if (!query.exec()) {
        kLogger.warning() << "Failed to verify a REST cache artifact" << query.lastError();
        return false;
    }
    return query.next();
}

bool RestLibraryTrackStore::rememberPreferredTrack(
        const QString& cacheIdentity,
        const QString& remoteId,
        TrackId trackId) const {
    if (cacheIdentity.trimmed().isEmpty() || remoteId.trimmed().isEmpty() ||
            !trackId.isValid()) {
        return false;
    }
    QSqlQuery query(m_pTrackCollectionManager->internalCollection()->database());
    query.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO rest_library_track_mappings "
            "(cache_identity, remote_id, track_id) "
            "VALUES (:cache_identity, :remote_id, :track_id)"));
    query.bindValue(QStringLiteral(":cache_identity"), cacheIdentity);
    query.bindValue(QStringLiteral(":remote_id"), remoteId);
    query.bindValue(QStringLiteral(":track_id"), trackId.toVariant());
    if (!query.exec()) {
        kLogger.warning() << "Failed to remember a REST track mapping" << query.lastError();
        return false;
    }
    return true;
}

void RestLibraryTrackStore::applyRemoteMetadata(
        const RestLibraryTrack& remoteTrack,
        const TrackPointer& pTrack) const {
    pTrack->setArtist(remoteTrack.artist);
    pTrack->setTitle(remoteTrack.title);
    pTrack->setAlbum(remoteTrack.album);
    pTrack->updateGenre(remoteTrack.genre);
    pTrack->setComposer(remoteTrack.composer);
    pTrack->setComment(remoteTrack.comment);
    pTrack->setTrackNumber(remoteTrack.trackNumber);
    pTrack->setKeyText(remoteTrack.keyText);
    if (pTrack->getSampleRate().isValid()) {
        pTrack->trySetBpm(remoteTrack.bpm);
    }
    pTrack->setDuration(remoteTrack.durationSeconds);
    pTrack->setRating(remoteTrack.rating);
    pTrack->setYear(remoteTrack.releaseDate.isValid()
                    ? remoteTrack.releaseDate.toString(Qt::ISODate)
                    : QString());
    pTrack->setType(remoteTrack.audioFileExtension);
}

} // namespace mixxx::library::rest
