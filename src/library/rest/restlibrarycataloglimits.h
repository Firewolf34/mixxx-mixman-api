#pragma once

#include <algorithm>

#include <QList>
#include <QSet>

#include "library/rest/restlibrarytrack.h"

namespace mixxx::library::rest {

class RestLibraryCatalogLimits final {
  public:
    enum class Result {
        Allowed,
        PageLimitReached,
        TrackLimitReached,
    };

    void reset(int maxPages, int maxTracks) {
        m_maxPages = std::max(1, maxPages);
        m_maxTracks = std::max(1, maxTracks);
        m_requestedPageCount = 0;
    }

    Result requestNextPage() {
        if (m_requestedPageCount >= m_maxPages) {
            return Result::PageLimitReached;
        }
        ++m_requestedPageCount;
        return Result::Allowed;
    }

    Result acceptPage(
            const QList<RestLibraryTrack>& tracks,
            const QSet<QString>& existingRemoteIds) const {
        QSet<QString> pageRemoteIds;
        for (const RestLibraryTrack& track : tracks) {
            if (!track.remoteId.isEmpty() &&
                    !existingRemoteIds.contains(track.remoteId)) {
                pageRemoteIds.insert(track.remoteId);
            }
        }
        if (existingRemoteIds.size() > m_maxTracks ||
                pageRemoteIds.size() > m_maxTracks - existingRemoteIds.size()) {
            return Result::TrackLimitReached;
        }
        return Result::Allowed;
    }

    int maxPages() const {
        return m_maxPages;
    }

    int maxTracks() const {
        return m_maxTracks;
    }

  private:
    int m_maxPages = 1;
    int m_maxTracks = 1;
    int m_requestedPageCount = 0;
};

} // namespace mixxx::library::rest
