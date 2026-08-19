#pragma once

#include <QString>

#include "library/rest/restlibrarytrack.h"
#include "track/track_decl.h"

class TrackCollectionManager;

namespace mixxx::library::rest {

class RestLibraryTrackStore final {
  public:
    explicit RestLibraryTrackStore(
            TrackCollectionManager* pTrackCollectionManager);

    TrackPointer materializeTrack(
            const RestLibraryTrack& remoteTrack,
            const QString& cacheIdentity) const;
    TrackPointer mappedTrack(
            const QString& cacheIdentity,
            const QString& remoteId) const;
    bool rememberLocalMapping(
            const QString& cacheIdentity,
            const QString& remoteId,
            const TrackPointer& pTrack) const;
    QString remoteIdForTrack(
            const QString& cacheIdentity,
            const TrackPointer& pTrack) const;
    bool isCacheArtifact(TrackId trackId) const;

  private:
    TrackPointer cacheArtifactForPath(
            const QString& cacheIdentity,
            const QString& remoteId,
            const QString& filePath) const;
    bool registerCacheArtifact(
            const QString& cacheIdentity,
            const QString& remoteId,
            TrackId trackId) const;
    bool rememberPreferredTrack(
            const QString& cacheIdentity,
            const QString& remoteId,
            TrackId trackId) const;
    void applyRemoteMetadata(
            const RestLibraryTrack& remoteTrack,
            const TrackPointer& pTrack) const;

    TrackCollectionManager* const m_pTrackCollectionManager;
};

} // namespace mixxx::library::rest
