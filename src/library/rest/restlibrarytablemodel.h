#pragma once

#include <QAbstractTableModel>
#include <QList>
#include <QVector>

#include "library/rest/restlibrarycachemanager.h"
#include "library/rest/restlibrarytrack.h"
#include "library/trackmodel.h"

class TrackCollectionManager;

namespace mixxx::library::rest {

class RestLibraryTableModel final : public QAbstractTableModel, public TrackModel {
    Q_OBJECT

  public:
    explicit RestLibraryTableModel(
            QObject* parent,
            TrackCollectionManager* pTrackCollectionManager);
    ~RestLibraryTableModel() override = default;

    void setTracks(QList<RestLibraryTrack> tracks);
    void setCacheLoadCapabilitiesEnabled(bool enabled);
    void updateTrackCacheState(const RestLibraryCacheResult& result);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(
            int section,
            Qt::Orientation orientation,
            int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    TrackPointer getTrack(const QModelIndex& index) const override;
    TrackPointer getTrackByRef(const TrackRef& trackRef) const override;
    QUrl getTrackUrl(const QModelIndex& index) const override;
    QString getTrackLocation(const QModelIndex& index) const override;
    TrackId getTrackId(const QModelIndex& index) const override;
    CoverInfo getCoverInfo(const QModelIndex& index) const override;
    const QVector<int> getTrackRows(TrackId trackId) const override;
    void search(const QString& searchText) override;
    const QString currentSearch() const override;
    bool isColumnInternal(int column) override;
    bool isColumnHiddenByDefault(int column) override;
    Capabilities getCapabilities() const override;
    SortColumnId sortColumnIdFromColumnIndex(int index) const override;
    int columnIndexFromSortColumnId(SortColumnId sortColumn) const override;
    int fieldIndex(const QString& fieldName) const override;
    QString modelKey(bool noSearch) const override;
    bool updateTrackGenre(Track* pTrack, const QString& genre) const override;
#if defined(__EXTRA_METADATA__)
    bool updateTrackMood(Track* pTrack, const QString& mood) const override;
#endif

  private:
    enum Column {
        ColumnCacheState = 0,
        ColumnArtist,
        ColumnTitle,
        ColumnAlbum,
        ColumnGenre,
        ColumnBpm,
        ColumnKey,
        ColumnDuration,
        ColumnRating,
        ColumnSource,
        ColumnRemoteId,
        ColumnCount,
    };

    const RestLibraryTrack* trackForIndex(const QModelIndex& index) const;
    QVariant valueForColumn(const RestLibraryTrack& track, int column) const;
    void rebuildVisibleRows();

    TrackCollectionManager* const m_pTrackCollectionManager;
    QList<RestLibraryTrack> m_tracks;
    QVector<int> m_visibleRows;
    QString m_currentSearch;
    int m_sortColumn = ColumnArtist;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
    bool m_cacheLoadCapabilitiesEnabled = false;
};

} // namespace mixxx::library::rest
