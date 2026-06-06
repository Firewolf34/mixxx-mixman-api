#include "library/rest/restlibrarytablemodel.h"

#include <algorithm>
#include <cmath>

#include <QDir>
#include <QUrl>

#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "moc_restlibrarytablemodel.cpp"
#include "track/track.h"
#include "track/trackref.h"
#include "util/assert.h"

namespace mixxx::library::rest {

namespace {

QString cacheStateText(RestLibraryCacheState state) {
    switch (state) {
    case RestLibraryCacheState::Missing:
        return QObject::tr("Not cached");
    case RestLibraryCacheState::Downloading:
        return QObject::tr("Downloading");
    case RestLibraryCacheState::Ready:
        return QObject::tr("Ready");
    case RestLibraryCacheState::Failed:
        return QObject::tr("Failed");
    case RestLibraryCacheState::Stale:
        return QObject::tr("Stale");
    }
    DEBUG_ASSERT(!"unreachable");
    return {};
}

QString durationText(double seconds) {
    if (seconds <= 0.0) {
        return {};
    }
    const int roundedSeconds = static_cast<int>(std::round(seconds));
    const int minutes = roundedSeconds / 60;
    const int remainingSeconds = roundedSeconds % 60;
    return QStringLiteral("%1:%2")
            .arg(minutes)
            .arg(remainingSeconds, 2, 10, QLatin1Char('0'));
}

QStringList searchableFields(const RestLibraryTrack& track) {
    return {
            track.artist,
            track.title,
            track.album,
            track.genre,
            track.composer,
            track.comment,
            track.keyText,
            track.sourceLabel,
            track.remoteId};
}

} // namespace

RestLibraryTableModel::RestLibraryTableModel(
        QObject* parent,
        TrackCollectionManager* pTrackCollectionManager)
        : TrackModel(
                  pTrackCollectionManager->internalCollection()->database(),
                  "mixxx.db.model.restlibrary"),
          QAbstractTableModel(parent),
          m_pTrackCollectionManager(pTrackCollectionManager) {
    setDefaultSort(ColumnArtist, Qt::AscendingOrder);
}

void RestLibraryTableModel::setTracks(QList<RestLibraryTrack> tracks) {
    beginResetModel();
    m_tracks = std::move(tracks);
    rebuildVisibleRows();
    endResetModel();
}

void RestLibraryTableModel::setCacheLoadCapabilitiesEnabled(bool enabled) {
    m_cacheLoadCapabilitiesEnabled = enabled;
}

void RestLibraryTableModel::updateTrackCacheState(const RestLibraryCacheResult& result) {
    if (result.remoteId.isEmpty()) {
        return;
    }

    for (int i = 0; i < m_tracks.size(); ++i) {
        RestLibraryTrack& track = m_tracks[i];
        if (track.remoteId != result.remoteId) {
            continue;
        }
        track.cacheState = result.cacheState;
        track.cachedFilePath = result.cachedFilePath;
        track.cacheError = result.errorText;

        const int visibleRow = m_visibleRows.indexOf(i);
        if (visibleRow >= 0) {
            emit dataChanged(
                    index(visibleRow, 0),
                    index(visibleRow, ColumnCount - 1),
                    {Qt::DisplayRole, Qt::ToolTipRole});
        }
    }
}

int RestLibraryTableModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return m_visibleRows.size();
}

int RestLibraryTableModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return ColumnCount;
}

QVariant RestLibraryTableModel::data(const QModelIndex& index, int role) const {
    const RestLibraryTrack* pTrack = trackForIndex(index);
    if (!pTrack) {
        return {};
    }
    if (role == Qt::DisplayRole || role == Qt::EditRole || role == TrackModel::kDataExportRole) {
        return valueForColumn(*pTrack, index.column());
    }
    if (role == Qt::ToolTipRole && index.column() == ColumnCacheState) {
        if (!pTrack->cacheError.isEmpty()) {
            return pTrack->cacheError;
        }
        return pTrack->cachedFilePath.isEmpty()
                ? tr("Track must be cached locally before it can be loaded.")
                : QDir::toNativeSeparators(pTrack->cachedFilePath);
    }
    return {};
}

QVariant RestLibraryTableModel::headerData(
        int section,
        Qt::Orientation orientation,
        int role) const {
    if (orientation != Qt::Horizontal) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }

    const bool nameRole = role == Qt::DisplayRole || role == TrackModel::kHeaderNameRole;
    if (nameRole) {
        switch (section) {
        case ColumnCacheState:
            return tr("Cache");
        case ColumnArtist:
            return tr("Artist");
        case ColumnTitle:
            return tr("Title");
        case ColumnAlbum:
            return tr("Album");
        case ColumnGenre:
            return tr("Genre");
        case ColumnBpm:
            return tr("BPM");
        case ColumnKey:
            return tr("Key");
        case ColumnDuration:
            return tr("Duration");
        case ColumnRating:
            return tr("Rating");
        case ColumnSource:
            return tr("Source");
        case ColumnRemoteId:
            return tr("Remote ID");
        }
    }

    if (role == TrackModel::kHeaderWidthRole) {
        switch (section) {
        case ColumnCacheState:
            return 90;
        case ColumnBpm:
        case ColumnKey:
        case ColumnDuration:
        case ColumnRating:
            return 70;
        case ColumnRemoteId:
            return 110;
        default:
            return 140;
        }
    }

    return {};
}

Qt::ItemFlags RestLibraryTableModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void RestLibraryTableModel::sort(int column, Qt::SortOrder order) {
    if (column < 0 || column >= ColumnCount) {
        return;
    }
    beginResetModel();
    m_sortColumn = column;
    m_sortOrder = order;
    rebuildVisibleRows();
    endResetModel();
}

TrackPointer RestLibraryTableModel::getTrack(const QModelIndex& index) const {
    const QString location = getTrackLocation(index);
    if (location.isEmpty()) {
        return {};
    }
    return m_pTrackCollectionManager->getOrAddTrack(TrackRef::fromFilePath(location));
}

TrackPointer RestLibraryTableModel::getTrackByRef(const TrackRef& trackRef) const {
    Q_UNUSED(trackRef);
    return {};
}

QUrl RestLibraryTableModel::getTrackUrl(const QModelIndex& index) const {
    const RestLibraryTrack* pTrack = trackForIndex(index);
    if (!pTrack) {
        return {};
    }
    if (!pTrack->cachedFilePath.isEmpty()) {
        return QUrl::fromLocalFile(pTrack->cachedFilePath);
    }
    return pTrack->sourceUrl;
}

QString RestLibraryTableModel::getTrackLocation(const QModelIndex& index) const {
    const RestLibraryTrack* pTrack = trackForIndex(index);
    if (!pTrack ||
            pTrack->cacheState != RestLibraryCacheState::Ready ||
            pTrack->cachedFilePath.isEmpty()) {
        return {};
    }
    return QDir::fromNativeSeparators(pTrack->cachedFilePath);
}

TrackId RestLibraryTableModel::getTrackId(const QModelIndex& index) const {
    const TrackPointer pTrack = getTrack(index);
    if (!pTrack) {
        return {};
    }
    return pTrack->getId();
}

CoverInfo RestLibraryTableModel::getCoverInfo(const QModelIndex& index) const {
    const TrackPointer pTrack = getTrack(index);
    if (!pTrack) {
        return {};
    }
    return CoverInfo(pTrack->getCoverInfo(), pTrack->getLocation());
}

const QVector<int> RestLibraryTableModel::getTrackRows(TrackId trackId) const {
    Q_UNUSED(trackId);
    return {};
}

void RestLibraryTableModel::search(const QString& searchText) {
    beginResetModel();
    m_currentSearch = searchText;
    rebuildVisibleRows();
    endResetModel();
}

const QString RestLibraryTableModel::currentSearch() const {
    return m_currentSearch;
}

bool RestLibraryTableModel::isColumnInternal(int column) {
    return column == ColumnRemoteId;
}

bool RestLibraryTableModel::isColumnHiddenByDefault(int column) {
    return column == ColumnRemoteId || column == ColumnSource;
}

TrackModel::Capabilities RestLibraryTableModel::getCapabilities() const {
    Capabilities capabilities = Capability::Sorting;
    if (m_cacheLoadCapabilitiesEnabled) {
        capabilities |= Capability::LoadToDeck |
                Capability::LoadToPreviewDeck |
                Capability::LoadToSampler;
    }
    return capabilities;
}

TrackModel::SortColumnId RestLibraryTableModel::sortColumnIdFromColumnIndex(int index) const {
    switch (index) {
    case ColumnArtist:
        return SortColumnId::Artist;
    case ColumnTitle:
        return SortColumnId::Title;
    case ColumnAlbum:
        return SortColumnId::Album;
    case ColumnGenre:
        return SortColumnId::Genre;
    case ColumnBpm:
        return SortColumnId::Bpm;
    case ColumnKey:
        return SortColumnId::Key;
    case ColumnDuration:
        return SortColumnId::Duration;
    case ColumnRating:
        return SortColumnId::Rating;
    default:
        return SortColumnId::Invalid;
    }
}

int RestLibraryTableModel::columnIndexFromSortColumnId(SortColumnId sortColumn) const {
    switch (sortColumn) {
    case SortColumnId::Artist:
        return ColumnArtist;
    case SortColumnId::Title:
        return ColumnTitle;
    case SortColumnId::Album:
        return ColumnAlbum;
    case SortColumnId::Genre:
        return ColumnGenre;
    case SortColumnId::Bpm:
        return ColumnBpm;
    case SortColumnId::Key:
        return ColumnKey;
    case SortColumnId::Duration:
        return ColumnDuration;
    case SortColumnId::Rating:
        return ColumnRating;
    default:
        return -1;
    }
}

int RestLibraryTableModel::fieldIndex(const QString& fieldName) const {
    if (fieldName == QStringLiteral("artist")) {
        return ColumnArtist;
    }
    if (fieldName == QStringLiteral("title")) {
        return ColumnTitle;
    }
    if (fieldName == QStringLiteral("album")) {
        return ColumnAlbum;
    }
    if (fieldName == QStringLiteral("genre")) {
        return ColumnGenre;
    }
    if (fieldName == QStringLiteral("bpm")) {
        return ColumnBpm;
    }
    if (fieldName == QStringLiteral("key")) {
        return ColumnKey;
    }
    if (fieldName == QStringLiteral("duration")) {
        return ColumnDuration;
    }
    if (fieldName == QStringLiteral("rating")) {
        return ColumnRating;
    }
    if (fieldName == QStringLiteral("remote_id")) {
        return ColumnRemoteId;
    }
    return -1;
}

QString RestLibraryTableModel::modelKey(bool noSearch) const {
    QString key = QStringLiteral("rest-library");
    if (!noSearch && !m_currentSearch.isEmpty()) {
        key += QStringLiteral(":") + m_currentSearch;
    }
    return key;
}

bool RestLibraryTableModel::updateTrackGenre(Track* pTrack, const QString& genre) const {
    Q_UNUSED(pTrack);
    Q_UNUSED(genre);
    return false;
}

#if defined(__EXTRA_METADATA__)
bool RestLibraryTableModel::updateTrackMood(Track* pTrack, const QString& mood) const {
    Q_UNUSED(pTrack);
    Q_UNUSED(mood);
    return false;
}
#endif

const RestLibraryTrack* RestLibraryTableModel::trackForIndex(const QModelIndex& index) const {
    if (!index.isValid() ||
            index.row() < 0 ||
            index.row() >= m_visibleRows.size() ||
            index.column() < 0 ||
            index.column() >= ColumnCount) {
        return nullptr;
    }
    return &m_tracks.at(m_visibleRows.at(index.row()));
}

QVariant RestLibraryTableModel::valueForColumn(
        const RestLibraryTrack& track,
        int column) const {
    switch (column) {
    case ColumnCacheState:
        return cacheStateText(track.cacheState);
    case ColumnArtist:
        return track.artist;
    case ColumnTitle:
        return track.title;
    case ColumnAlbum:
        return track.album;
    case ColumnGenre:
        return track.genre;
    case ColumnBpm:
        return track.bpm > 0.0 ? QVariant(track.bpm) : QVariant();
    case ColumnKey:
        return track.keyText;
    case ColumnDuration:
        return durationText(track.durationSeconds);
    case ColumnRating:
        return track.rating > 0 ? QVariant(track.rating) : QVariant();
    case ColumnSource:
        return track.sourceLabel;
    case ColumnRemoteId:
        return track.remoteId;
    default:
        return {};
    }
}

void RestLibraryTableModel::rebuildVisibleRows() {
    m_visibleRows.clear();
    const QString searchText = m_currentSearch.trimmed();
    for (int i = 0; i < m_tracks.size(); ++i) {
        if (searchText.isEmpty()) {
            m_visibleRows.push_back(i);
            continue;
        }
        const QStringList fields = searchableFields(m_tracks.at(i));
        for (const QString& field : fields) {
            if (field.contains(searchText, Qt::CaseInsensitive)) {
                m_visibleRows.push_back(i);
                break;
            }
        }
    }

    std::sort(m_visibleRows.begin(), m_visibleRows.end(), [this](int lhs, int rhs) {
        const QVariant leftValue = valueForColumn(m_tracks.at(lhs), m_sortColumn);
        const QVariant rightValue = valueForColumn(m_tracks.at(rhs), m_sortColumn);
        const int compare = QString::localeAwareCompare(
                leftValue.toString(),
                rightValue.toString());
        if (m_sortOrder == Qt::AscendingOrder) {
            return compare < 0;
        }
        return compare > 0;
    });
}

} // namespace mixxx::library::rest
