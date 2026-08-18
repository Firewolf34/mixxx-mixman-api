#include "library/rest/restlibrarytablemodel.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <QDir>
#include <QTableView>
#include <QUrl>

#include "control/controlobject.h"
#include "library/library_prefs.h"
#include "library/tabledelegates/percentagedelegate.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "library/searchquery.h"
#include "library/searchqueryparser.h"
#include "moc_restlibrarytablemodel.cpp"
#include "track/keyutils.h"
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

QString percentageText(const std::optional<double>& value) {
    if (!value.has_value()) {
        return QStringLiteral("\u2014");
    }
    return QStringLiteral("%1%").arg(qRound(*value * 100.0));
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
            track.moveType,
            track.color,
            track.region,
            track.reasonCodes.join(QLatin1Char(' ')),
            track.remoteId};
}

} // namespace

RestLibraryTableModel::RestLibraryTableModel(
        QObject* parent,
        TrackCollectionManager* pTrackCollectionManager,
        Mode mode)
        : QAbstractTableModel(parent),
          TrackModel(
                  pTrackCollectionManager->internalCollection()->database(),
                  "mixxx.db.model.restlibrary"),
          m_pTrackCollectionManager(pTrackCollectionManager),
          m_mode(mode) {
    if (m_mode == Mode::Catalog) {
        m_pSearchQueryParser = std::make_unique<SearchQueryParser>(
                pTrackCollectionManager->internalCollection(),
                QStringList{
                        QStringLiteral("artist"),
                        QStringLiteral("album_artist"),
                        QStringLiteral("album"),
                        QStringLiteral("title"),
                        QStringLiteral("genre"),
                        QStringLiteral("composer"),
                        QStringLiteral("comment"),
                        QStringLiteral("tracknumber"),
                        QStringLiteral("key"),
                        QStringLiteral("bpm"),
                        QStringLiteral("duration"),
                        QStringLiteral("rating"),
                        QStringLiteral("filetype")});
    }
    setDefaultSort(ColumnArtist, Qt::AscendingOrder);
}

RestLibraryTableModel::~RestLibraryTableModel() = default;

int RestLibraryTableModel::cacheStateCount(RestLibraryCacheState state) const {
    return static_cast<int>(std::count_if(
            m_tracks.cbegin(),
            m_tracks.cend(),
            [state](const RestLibraryTrack& track) {
                return track.cacheState == state;
            }));
}

void RestLibraryTableModel::setTracks(QList<RestLibraryTrack> tracks) {
    beginResetModel();
    m_tracks = std::move(tracks);
    m_searchTracks.clear();
    if (m_mode == Mode::Catalog) {
        for (const RestLibraryTrack& remoteTrack : std::as_const(m_tracks)) {
            TrackPointer pTrack = Track::newTemporary();
            pTrack->setArtist(remoteTrack.artist);
            pTrack->setTitle(remoteTrack.title);
            pTrack->setAlbum(remoteTrack.album);
            pTrack->updateGenre(remoteTrack.genre);
            pTrack->setComposer(remoteTrack.composer);
            pTrack->setComment(remoteTrack.comment);
            pTrack->setTrackNumber(remoteTrack.trackNumber);
            pTrack->setKeyText(remoteTrack.keyText);
            pTrack->setAudioProperties(
                    mixxx::audio::ChannelCount::stereo(),
                    mixxx::audio::SampleRate(44100),
                    mixxx::audio::Bitrate(),
                    mixxx::Duration::fromSeconds(remoteTrack.durationSeconds));
            pTrack->trySetBpm(remoteTrack.bpm);
            pTrack->setRating(remoteTrack.rating);
            pTrack->resetPlayCounter(remoteTrack.playCount);
            pTrack->setYear(remoteTrack.releaseDate.isValid()
                            ? remoteTrack.releaseDate.toString(Qt::ISODate)
                            : QString());
            pTrack->setType(remoteTrack.audioFileExtension);
            m_searchTracks.insert(remoteTrack.remoteId, std::move(pTrack));
        }
        m_pSearchQuery = m_pSearchQueryParser->parseQuery(m_currentSearch, {});
    }
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
        track.cacheStatusCode = result.statusCode;
        track.cacheNetworkError = result.networkError;

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
    const bool normalizedColumn =
            index.column() == ColumnFavour || index.column() == ColumnEnergy;
    if (normalizedColumn && role == Qt::DisplayRole) {
        return percentageText(index.column() == ColumnFavour ? pTrack->favour : pTrack->energy);
    }
    if (normalizedColumn && role == Qt::ToolTipRole) {
        const std::optional<double>& value =
                index.column() == ColumnFavour ? pTrack->favour : pTrack->energy;
        const QString label = index.column() == ColumnFavour ? tr("Favour") : tr("Energy");
        return value.has_value()
                ? tr("%1: %2 (raw %3)")
                          .arg(label, percentageText(value), QString::number(*value, 'f', 3))
                : tr("%1: not provided").arg(label);
    }
    if (role == Qt::DisplayRole || role == Qt::EditRole ||
            role == TrackModel::kDataExportRole) {
        return valueForColumn(*pTrack, index.column());
    }
    if (role == Qt::ToolTipRole && index.column() == ColumnCacheState) {
        if (!pTrack->cacheError.isEmpty()) {
            QStringList parts;
            parts.append(pTrack->cacheError);
            if (pTrack->cacheStatusCode > 0) {
                parts.append(tr("HTTP %1").arg(pTrack->cacheStatusCode));
            }
            if (pTrack->cacheNetworkError != 0) {
                parts.append(tr("Network error %1").arg(pTrack->cacheNetworkError));
            }
            return parts.join(QLatin1Char('\n'));
        }
        return pTrack->cachedFilePath.isEmpty()
                ? tr("Track must be cached locally before it can be loaded.")
                : QDir::toNativeSeparators(pTrack->cachedFilePath);
    }
    if (role == Qt::ToolTipRole && index.column() == ColumnQuality) {
        QStringList details;
        if (pTrack->score > 0.0) {
            details.append(tr("Score: %1").arg(pTrack->score, 0, 'f', 2));
        }
        if (pTrack->transitionFit > 0.0) {
            details.append(tr("Transition fit: %1").arg(pTrack->transitionFit, 0, 'f', 2));
        }
        if (pTrack->targetDistance > 0.0) {
            details.append(tr("Target distance: %1").arg(pTrack->targetDistance, 0, 'f', 2));
        }
        if (!pTrack->reasonCodes.isEmpty()) {
            details.append(pTrack->reasonCodes.join(QStringLiteral(", ")));
        }
        return details.join(QLatin1Char('\n'));
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
        case ColumnQuality:
            return tr("Quality");
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
        case ColumnComposer:
            return tr("Composer");
        case ColumnComment:
            return tr("Comment");
        case ColumnTrackNumber:
            return tr("Track #");
        case ColumnYear:
            return tr("Year");
        case ColumnType:
            return tr("Type");
        case ColumnPlayCount:
            return tr("Played");
        case ColumnFavour:
            return tr("Favour");
        case ColumnEnergy:
            return tr("Energy");
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
        case ColumnQuality:
            return 80;
        case ColumnBpm:
        case ColumnKey:
        case ColumnDuration:
        case ColumnRating:
        case ColumnPlayCount:
        case ColumnFavour:
        case ColumnEnergy:
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

QAbstractItemDelegate* RestLibraryTableModel::delegateForColumn(
        int column,
        QObject* pParent) {
    if (column != ColumnFavour && column != ColumnEnergy) {
        return nullptr;
    }
    auto* pTableView = qobject_cast<QTableView*>(pParent);
    VERIFY_OR_DEBUG_ASSERT(pTableView) {
        return nullptr;
    }
    return new PercentageDelegate(pTableView);
}

TrackPointer RestLibraryTableModel::getTrack(const QModelIndex& index) const {
    const RestLibraryTrack* pRemoteTrack = trackForIndex(index);
    if (!pRemoteTrack) {
        return {};
    }
    return materializeTrack(pRemoteTrack->remoteId);
}

TrackPointer RestLibraryTableModel::materializeTrack(const QString& remoteId) const {
    const RestLibraryTrack remoteTrack = trackForRemoteId(remoteId);
    if (remoteTrack.remoteId.isEmpty() ||
            remoteTrack.cacheState != RestLibraryCacheState::Ready ||
            remoteTrack.cachedFilePath.isEmpty()) {
        return {};
    }
    const QString location = QDir::fromNativeSeparators(remoteTrack.cachedFilePath);
    bool alreadyInLibrary = false;
    TrackPointer pTrack = m_pTrackCollectionManager->getOrAddTrack(
            TrackRef::fromFilePath(location), &alreadyInLibrary);
    if (pTrack && !alreadyInLibrary) {
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
        m_pTrackCollectionManager->saveTrack(pTrack);
    }
    return pTrack;
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
    if (m_pSearchQueryParser) {
        m_pSearchQuery = m_pSearchQueryParser->parseQuery(searchText, {});
    }
    rebuildVisibleRows();
    endResetModel();
}

const QString RestLibraryTableModel::currentSearch() const {
    return m_currentSearch;
}

bool RestLibraryTableModel::isColumnInternal(int column) {
    return column == ColumnRemoteId ||
            (m_mode == Mode::Catalog && column == ColumnQuality);
}

bool RestLibraryTableModel::isColumnHiddenByDefault(int column) {
    return column == ColumnRemoteId || column == ColumnSource ||
            column == ColumnComposer || column == ColumnComment ||
            column == ColumnTrackNumber || column == ColumnYear ||
            column == ColumnType || column == ColumnPlayCount ||
            column == ColumnFavour || column == ColumnEnergy ||
            (m_mode == Mode::Catalog && column == ColumnQuality);
}

TrackModel::Capabilities RestLibraryTableModel::getCapabilities() const {
    Capabilities capabilities = Capability::Sorting;
    if (m_cacheLoadCapabilitiesEnabled) {
        capabilities |= Capability::LoadToDeck |
                Capability::LoadToPreviewDeck |
                Capability::LoadToSampler |
                Capability::AddToAutoDJ;
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
    case ColumnComposer:
        return SortColumnId::Composer;
    case ColumnComment:
        return SortColumnId::Comment;
    case ColumnTrackNumber:
        return SortColumnId::TrackNumber;
    case ColumnYear:
        return SortColumnId::Year;
    case ColumnType:
        return SortColumnId::FileType;
    case ColumnPlayCount:
        return SortColumnId::TimesPlayed;
    case ColumnFavour:
        return SortColumnId::Favour;
    case ColumnEnergy:
        return SortColumnId::Energy;
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
    case SortColumnId::Composer:
        return ColumnComposer;
    case SortColumnId::Comment:
        return ColumnComment;
    case SortColumnId::TrackNumber:
        return ColumnTrackNumber;
    case SortColumnId::Year:
        return ColumnYear;
    case SortColumnId::FileType:
        return ColumnType;
    case SortColumnId::TimesPlayed:
        return ColumnPlayCount;
    case SortColumnId::Favour:
        return ColumnFavour;
    case SortColumnId::Energy:
        return ColumnEnergy;
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
    if (fieldName == QStringLiteral("quality")) {
        return ColumnQuality;
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
    if (fieldName == QStringLiteral("composer")) {
        return ColumnComposer;
    }
    if (fieldName == QStringLiteral("comment")) {
        return ColumnComment;
    }
    if (fieldName == QStringLiteral("tracknumber")) {
        return ColumnTrackNumber;
    }
    if (fieldName == QStringLiteral("year")) {
        return ColumnYear;
    }
    if (fieldName == QStringLiteral("filetype")) {
        return ColumnType;
    }
    if (fieldName == QStringLiteral("timesplayed")) {
        return ColumnPlayCount;
    }
    if (fieldName == QStringLiteral("favour")) {
        return ColumnFavour;
    }
    if (fieldName == QStringLiteral("energy")) {
        return ColumnEnergy;
    }
    if (fieldName == QStringLiteral("remote_id")) {
        return ColumnRemoteId;
    }
    return -1;
}

QString RestLibraryTableModel::modelKey(bool noSearch) const {
    QString key = m_mode == Mode::Catalog
            ? QStringLiteral("rest-library-browser")
            : QStringLiteral("rest-library-recommendations");
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

QString RestLibraryTableModel::remoteIdForIndex(const QModelIndex& index) const {
    const RestLibraryTrack* pTrack = trackForIndex(index);
    return pTrack ? pTrack->remoteId : QString();
}

int RestLibraryTableModel::visibleRowForRemoteId(const QString& remoteId) const {
    if (remoteId.isEmpty()) {
        return -1;
    }
    for (int visibleRow = 0; visibleRow < m_visibleRows.size(); ++visibleRow) {
        if (m_tracks.at(m_visibleRows.at(visibleRow)).remoteId == remoteId) {
            return visibleRow;
        }
    }
    return -1;
}

RestLibraryTrack RestLibraryTableModel::trackForRemoteId(const QString& remoteId) const {
    for (const RestLibraryTrack& track : m_tracks) {
        if (track.remoteId == remoteId) {
            return track;
        }
    }
    return {};
}

QVariant RestLibraryTableModel::valueForColumn(
        const RestLibraryTrack& track,
        int column) const {
    switch (column) {
    case ColumnCacheState:
        return cacheStateText(track.cacheState);
    case ColumnQuality:
        return track.quality > 0.0 ? QVariant(QString::number(track.quality, 'f', 2))
                                   : QVariant();
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
    case ColumnComposer:
        return track.composer;
    case ColumnComment:
        return track.comment;
    case ColumnTrackNumber:
        return track.trackNumber;
    case ColumnYear:
        return track.releaseDate.isValid()
                ? QVariant(track.releaseDate.toString(Qt::ISODate))
                : QVariant();
    case ColumnType:
        return track.audioFileExtension;
    case ColumnPlayCount:
        return track.playCount > 0 ? QVariant(track.playCount) : QVariant();
    case ColumnFavour:
        return track.favour.has_value() ? QVariant(*track.favour) : QVariant();
    case ColumnEnergy:
        return track.energy.has_value() ? QVariant(*track.energy) : QVariant();
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
        if (m_mode == Mode::Catalog && m_pSearchQuery) {
            const TrackPointer pTrack = m_searchTracks.value(m_tracks.at(i).remoteId);
            if (pTrack && m_pSearchQuery->match(pTrack)) {
                m_visibleRows.push_back(i);
            }
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

    const auto keyNotation = KeyUtils::keyNotationFromNumericValue(
            ControlObject::get(mixxx::library::prefs::kKeyNotationConfigKey));
    std::sort(m_visibleRows.begin(), m_visibleRows.end(), [this, keyNotation](int lhs, int rhs) {
        const RestLibraryTrack& leftTrack = m_tracks.at(lhs);
        const RestLibraryTrack& rightTrack = m_tracks.at(rhs);
        int compare = 0;
        bool leftMissing = false;
        bool rightMissing = false;
        switch (m_sortColumn) {
        case ColumnQuality:
            compare = leftTrack.quality < rightTrack.quality
                    ? -1
                    : leftTrack.quality > rightTrack.quality ? 1 : 0;
            break;
        case ColumnBpm:
            compare = leftTrack.bpm < rightTrack.bpm
                    ? -1
                    : leftTrack.bpm > rightTrack.bpm ? 1 : 0;
            break;
        case ColumnDuration:
            compare = leftTrack.durationSeconds < rightTrack.durationSeconds
                    ? -1
                    : leftTrack.durationSeconds > rightTrack.durationSeconds ? 1 : 0;
            break;
        case ColumnRating:
            compare = leftTrack.rating - rightTrack.rating;
            break;
        case ColumnPlayCount:
            compare = leftTrack.playCount - rightTrack.playCount;
            break;
        case ColumnKey: {
            const auto leftKey = KeyUtils::guessKeyFromText(leftTrack.keyText);
            const auto rightKey = KeyUtils::guessKeyFromText(rightTrack.keyText);
            leftMissing = leftKey == mixxx::track::io::key::INVALID;
            rightMissing = rightKey == mixxx::track::io::key::INVALID;
            if (!leftMissing && !rightMissing) {
                compare = KeyUtils::keyToCircleOfFifthsOrder(leftKey, keyNotation) -
                        KeyUtils::keyToCircleOfFifthsOrder(rightKey, keyNotation);
            }
            break;
        }
        case ColumnFavour:
            leftMissing = !leftTrack.favour.has_value();
            rightMissing = !rightTrack.favour.has_value();
            if (!leftMissing && !rightMissing) {
                compare = *leftTrack.favour < *rightTrack.favour
                        ? -1
                        : *leftTrack.favour > *rightTrack.favour ? 1 : 0;
            }
            break;
        case ColumnEnergy:
            leftMissing = !leftTrack.energy.has_value();
            rightMissing = !rightTrack.energy.has_value();
            if (!leftMissing && !rightMissing) {
                compare = *leftTrack.energy < *rightTrack.energy
                        ? -1
                        : *leftTrack.energy > *rightTrack.energy ? 1 : 0;
            }
            break;
        case ColumnCacheState:
            compare = static_cast<int>(leftTrack.cacheState) -
                    static_cast<int>(rightTrack.cacheState);
            break;
        default:
            compare = QString::localeAwareCompare(
                    valueForColumn(leftTrack, m_sortColumn).toString(),
                    valueForColumn(rightTrack, m_sortColumn).toString());
            break;
        }
        if (leftMissing != rightMissing) {
            return !leftMissing;
        }
        if (compare == 0) {
            compare = QString::localeAwareCompare(leftTrack.remoteId, rightTrack.remoteId);
        }
        if (m_sortOrder == Qt::AscendingOrder) {
            return compare < 0;
        }
        return compare > 0;
    });
}

} // namespace mixxx::library::rest
