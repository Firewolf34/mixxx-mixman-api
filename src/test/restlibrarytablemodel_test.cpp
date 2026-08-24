#include <gtest/gtest.h>

#include <memory>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QIODevice>
#include <QPainter>
#include <QPersistentModelIndex>
#include <QStyleOptionViewItem>
#include <QTableView>
#include <QTemporaryDir>
#include <QVector>

#include "control/controlobject.h"
#include "library/dao/playlistdao.h"
#include "library/library_prefs.h"
#include "library/librarytablemodel.h"
#include "library/rest/restlibrarycachestatedelegate.h"
#include "library/rest/restlibrarytablemodel.h"
#include "library/tabledelegates/colordelegate.h"
#include "library/tabledelegates/percentagedelegate.h"
#include "test/librarytest.h"
#include "track/keyutils.h"
#include "util/color/rgbcolor.h"

namespace {

using mixxx::library::rest::RestLibraryCacheState;
using mixxx::library::rest::RestLibraryCacheStateDelegate;
using mixxx::library::rest::RestLibraryCacheResult;
using mixxx::library::rest::RestLibraryTableModel;
using mixxx::library::rest::RestLibraryTrack;

const QString kCacheIdentity = QStringLiteral("https://rest.test|test-account");

RestLibraryTrack newTrack(
        QString remoteId,
        QString artist,
        QString title,
        RestLibraryCacheState cacheState = RestLibraryCacheState::Missing) {
    RestLibraryTrack track;
    track.remoteId = std::move(remoteId);
    track.artist = std::move(artist);
    track.title = std::move(title);
    track.cacheState = cacheState;
    return track;
}

} // namespace

class RestLibraryTableModelTest : public LibraryTest {
};

TEST_F(RestLibraryTableModelTest, ExposesRowsAndKeepsLoadCapabilitiesDisabled) {
    RestLibraryTableModel model(nullptr, trackCollectionManager());
    model.setCacheIdentity(kCacheIdentity);
    model.setTracks({
            newTrack(QStringLiteral("1"), QStringLiteral("Beta"), QStringLiteral("Second")),
            newTrack(QStringLiteral("2"), QStringLiteral("Alpha"), QStringLiteral("First")),
    });

    EXPECT_EQ(model.rowCount(), 2);
    EXPECT_TRUE(model.hasCapabilities(TrackModel::Capability::Sorting));
    EXPECT_FALSE(model.hasCapabilities(TrackModel::Capability::LoadToDeck));
    EXPECT_FALSE(model.getTrack(model.index(0, 0)));
    EXPECT_TRUE(model.getTrackLocation(model.index(0, 0)).isEmpty());
}

TEST_F(RestLibraryTableModelTest, EnablesLoadCapabilitiesWhenCacheIsConfigured) {
    RestLibraryTableModel model(nullptr, trackCollectionManager());

    EXPECT_FALSE(model.hasCapabilities(TrackModel::Capability::LoadToDeck));
    EXPECT_FALSE(model.hasCapabilities(TrackModel::Capability::AddToAutoDJ));

    model.setCacheLoadCapabilitiesEnabled(true);

    EXPECT_TRUE(model.hasCapabilities(TrackModel::Capability::LoadToDeck));
    EXPECT_TRUE(model.hasCapabilities(TrackModel::Capability::LoadToPreviewDeck));
    EXPECT_TRUE(model.hasCapabilities(TrackModel::Capability::LoadToSampler));
    EXPECT_TRUE(model.hasCapabilities(TrackModel::Capability::AddToAutoDJ));
}

TEST_F(RestLibraryTableModelTest, ReadyRowsExposeLocalTrackLocation) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString filePath = QDir(tempDir.path()).filePath(QStringLiteral("cached.mp3"));
    QFile file(filePath);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("audio");
    file.close();

    RestLibraryTableModel model(nullptr, trackCollectionManager());
    model.setCacheIdentity(kCacheIdentity);
    RestLibraryTrack remoteTrack = newTrack(
            QStringLiteral("1"),
            QStringLiteral("Beta"),
            QStringLiteral("Second"));
    remoteTrack.bpm = 128.0;
    model.setTracks({remoteTrack});

    model.updateTrackCacheState({
            QStringLiteral("1"),
            RestLibraryCacheState::Ready,
            filePath,
            {},
            0,
            0,
            kCacheIdentity});

    EXPECT_EQ(
            model.getTrackLocation(model.index(0, 0)),
            QDir::fromNativeSeparators(filePath));
    EXPECT_TRUE(model.getTrack(model.index(0, 0)));
    EXPECT_TRUE(model.getTrackId(model.index(0, 0)).isValid());
}

TEST_F(RestLibraryTableModelTest, CacheArtifactsStayOutOfTracksAndReuseIdentity) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString filePath = QDir(tempDir.path()).filePath(QStringLiteral("cached.mp3"));
    QFile file(filePath);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("audio");
    file.close();

    RestLibraryTableModel model(nullptr, trackCollectionManager());
    model.setCacheIdentity(kCacheIdentity);
    model.setTracks({newTrack(
            QStringLiteral("remote-1"),
            QStringLiteral("Artist"),
            QStringLiteral("Cached"))});
    model.updateTrackCacheState({
            QStringLiteral("remote-1"),
            RestLibraryCacheState::Ready,
            filePath,
            {},
            0,
            0,
            kCacheIdentity});

    const TrackPointer pFirst = model.materializeTrack(QStringLiteral("remote-1"));
    RestLibraryTableModel reconstructedModel(nullptr, trackCollectionManager());
    reconstructedModel.setCacheIdentity(kCacheIdentity);
    reconstructedModel.setTracks({newTrack(
            QStringLiteral("remote-1"),
            QStringLiteral("Artist"),
            QStringLiteral("Cached"))});
    reconstructedModel.updateTrackCacheState({QStringLiteral("remote-1"),
            RestLibraryCacheState::Ready,
            filePath,
            {},
            0,
            0,
            kCacheIdentity});
    const TrackPointer pSecond =
            reconstructedModel.materializeTrack(QStringLiteral("remote-1"));
    ASSERT_TRUE(pFirst);
    ASSERT_TRUE(pSecond);
    EXPECT_EQ(pFirst->getId(), pSecond->getId());
    EXPECT_TRUE(model.isCacheArtifact(pFirst->getId()));

    PlaylistDAO& playlistDao = internalCollection()->getPlaylistDAO();
    const int historyId = playlistDao.createPlaylist(
            QStringLiteral("REST history test"), PlaylistDAO::PLHT_SET_LOG);
    ASSERT_GT(historyId, 0);
    ASSERT_TRUE(playlistDao.appendTrackToPlaylist(pFirst->getId(), historyId));
    EXPECT_EQ(playlistDao.getTrackIdsInPlaylistOrder(historyId),
            (QList<TrackId>{pFirst->getId()}));

    LibraryTableModel libraryModel(
            nullptr, trackCollectionManager(), "mixxx.db.model.library.rest-test");
    libraryModel.select();
    EXPECT_EQ(libraryModel.rowCount(), 0);
}

TEST_F(RestLibraryTableModelTest, ExplicitLocalMappingAvoidsCacheDuplicate) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString localPath = QDir(tempDir.path()).filePath(QStringLiteral("local.mp3"));
    QFile localFile(localPath);
    ASSERT_TRUE(localFile.open(QIODevice::WriteOnly));
    localFile.write("local audio");
    localFile.close();
    const TrackPointer pLocalTrack = getOrAddTrackByLocation(localPath);
    ASSERT_TRUE(pLocalTrack);

    RestLibraryTableModel model(nullptr, trackCollectionManager());
    model.setCacheIdentity(kCacheIdentity);
    model.setTracks({newTrack(
            QStringLiteral("remote-1"),
            QStringLiteral("Artist"),
            QStringLiteral("Remote"))});
    ASSERT_TRUE(model.rememberLocalMapping(QStringLiteral("remote-1"), pLocalTrack));

    RestLibraryTableModel reconstructedModel(nullptr, trackCollectionManager());
    reconstructedModel.setCacheIdentity(kCacheIdentity);
    reconstructedModel.setTracks({newTrack(
            QStringLiteral("remote-1"),
            QStringLiteral("Artist"),
            QStringLiteral("Remote"))});
    const TrackPointer pResolved =
            reconstructedModel.materializeTrack(QStringLiteral("remote-1"));
    ASSERT_TRUE(pResolved);
    EXPECT_EQ(pResolved->getId(), pLocalTrack->getId());
    EXPECT_FALSE(reconstructedModel.isCacheArtifact(pResolved->getId()));
    EXPECT_EQ(reconstructedModel.remoteIdForTrack(pLocalTrack),
            QStringLiteral("remote-1"));
    EXPECT_EQ(reconstructedModel.getTrackLocation(
                      reconstructedModel.index(0, 0)),
            QDir::fromNativeSeparators(localPath));

    LibraryTableModel libraryModel(
            nullptr, trackCollectionManager(), "mixxx.db.model.library.rest-local-test");
    libraryModel.select();
    EXPECT_EQ(libraryModel.rowCount(), 1);
}

TEST_F(RestLibraryTableModelTest, UnmappedLocalTrackRemainsWhileCacheArtifactIsHidden) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString localPath = QDir(tempDir.path()).filePath(QStringLiteral("local.mp3"));
    const QString cachePath = QDir(tempDir.path()).filePath(QStringLiteral("cached.mp3"));
    for (const QString& path : {localPath, cachePath}) {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("audio");
    }
    const TrackPointer pLocalTrack = getOrAddTrackByLocation(localPath);
    ASSERT_TRUE(pLocalTrack);

    RestLibraryTableModel model(nullptr, trackCollectionManager());
    model.setCacheIdentity(kCacheIdentity);
    model.setTracks({newTrack(
            QStringLiteral("remote-1"),
            QStringLiteral("Artist"),
            QStringLiteral("Remote"))});
    model.updateTrackCacheState({QStringLiteral("remote-1"),
            RestLibraryCacheState::Ready,
            cachePath,
            {},
            0,
            0,
            kCacheIdentity});

    const TrackPointer pCacheTrack = model.materializeTrack(QStringLiteral("remote-1"));
    ASSERT_TRUE(pCacheTrack);
    EXPECT_NE(pCacheTrack->getId(), pLocalTrack->getId());
    EXPECT_TRUE(model.isCacheArtifact(pCacheTrack->getId()));

    LibraryTableModel libraryModel(
            nullptr, trackCollectionManager(), "mixxx.db.model.library.rest-unmapped-test");
    libraryModel.select();
    EXPECT_EQ(libraryModel.rowCount(), 1);
}

TEST_F(RestLibraryTableModelTest, OverlappingRemoteIdsStayCredentialScoped) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString firstPath = QDir(tempDir.path()).filePath(QStringLiteral("first.mp3"));
    const QString secondPath = QDir(tempDir.path()).filePath(QStringLiteral("second.mp3"));
    for (const QString& path : {firstPath, secondPath}) {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("audio");
    }

    RestLibraryTableModel firstModel(nullptr, trackCollectionManager());
    firstModel.setCacheIdentity(QStringLiteral("https://rest.test|account-a"));
    firstModel.setTracks({newTrack(
            QStringLiteral("shared"), QStringLiteral("A"), QStringLiteral("First"))});
    firstModel.updateTrackCacheState({QStringLiteral("shared"),
            RestLibraryCacheState::Ready,
            firstPath,
            {},
            0,
            0,
            QStringLiteral("https://rest.test|account-a")});

    RestLibraryTableModel secondModel(nullptr, trackCollectionManager());
    secondModel.setCacheIdentity(QStringLiteral("https://rest.test|account-b"));
    secondModel.setTracks({newTrack(
            QStringLiteral("shared"), QStringLiteral("B"), QStringLiteral("Second"))});
    secondModel.updateTrackCacheState({QStringLiteral("shared"),
            RestLibraryCacheState::Ready,
            secondPath,
            {},
            0,
            0,
            QStringLiteral("https://rest.test|account-b")});

    const TrackPointer pFirst = firstModel.materializeTrack(QStringLiteral("shared"));
    const TrackPointer pSecond = secondModel.materializeTrack(QStringLiteral("shared"));
    ASSERT_TRUE(pFirst);
    ASSERT_TRUE(pSecond);
    EXPECT_NE(pFirst->getId(), pSecond->getId());
    EXPECT_EQ(firstModel.remoteIdForTrack(pFirst), QStringLiteral("shared"));
    EXPECT_TRUE(firstModel.remoteIdForTrack(pSecond).isEmpty());
}

TEST_F(RestLibraryTableModelTest, EvictedArtifactNeverReturnsToTracks) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString filePath = QDir(tempDir.path()).filePath(QStringLiteral("cached.mp3"));
    {
        QFile file(filePath);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("audio");
    }

    RestLibraryTableModel model(nullptr, trackCollectionManager());
    model.setCacheIdentity(kCacheIdentity);
    model.setTracks({newTrack(
            QStringLiteral("remote-1"),
            QStringLiteral("Artist"),
            QStringLiteral("Cached"))});
    model.updateTrackCacheState({QStringLiteral("remote-1"),
            RestLibraryCacheState::Ready,
            filePath,
            {},
            0,
            0,
            kCacheIdentity});
    const TrackPointer pTrack = model.materializeTrack(QStringLiteral("remote-1"));
    ASSERT_TRUE(pTrack);
    ASSERT_TRUE(QFile::remove(filePath));
    model.updateTrackCacheState({QStringLiteral("remote-1"),
            RestLibraryCacheState::Stale,
            {},
            {},
            0,
            0,
            kCacheIdentity});

    LibraryTableModel libraryModel(
            nullptr, trackCollectionManager(), "mixxx.db.model.library.rest-eviction-test");
    libraryModel.select();
    EXPECT_EQ(libraryModel.rowCount(), 0);
    EXPECT_TRUE(model.isCacheArtifact(pTrack->getId()));
}

TEST_F(RestLibraryTableModelTest, RedownloadWithNewExtensionKeepsBothArtifactsHidden) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString mp3Path = QDir(tempDir.path()).filePath(QStringLiteral("cached.mp3"));
    {
        QFile file(mp3Path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("mp3 audio");
    }

    RestLibraryTableModel model(nullptr, trackCollectionManager());
    model.setCacheIdentity(kCacheIdentity);
    model.setTracks({newTrack(
            QStringLiteral("remote-1"),
            QStringLiteral("Artist"),
            QStringLiteral("Cached"))});
    model.updateTrackCacheState({QStringLiteral("remote-1"),
            RestLibraryCacheState::Ready,
            mp3Path,
            {},
            0,
            0,
            kCacheIdentity});
    const TrackPointer pMp3Track = model.materializeTrack(QStringLiteral("remote-1"));
    ASSERT_TRUE(pMp3Track);
    ASSERT_TRUE(QFile::remove(mp3Path));

    const QString flacPath = QDir(tempDir.path()).filePath(QStringLiteral("cached.flac"));
    {
        QFile file(flacPath);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("flac audio");
    }
    model.updateTrackCacheState({QStringLiteral("remote-1"),
            RestLibraryCacheState::Ready,
            flacPath,
            {},
            0,
            0,
            kCacheIdentity});
    const TrackPointer pFlacTrack = model.materializeTrack(QStringLiteral("remote-1"));
    ASSERT_TRUE(pFlacTrack);
    EXPECT_NE(pMp3Track->getId(), pFlacTrack->getId());
    EXPECT_TRUE(model.isCacheArtifact(pMp3Track->getId()));
    EXPECT_TRUE(model.isCacheArtifact(pFlacTrack->getId()));

    LibraryTableModel libraryModel(
            nullptr, trackCollectionManager(), "mixxx.db.model.library.rest-redownload-test");
    libraryModel.select();
    EXPECT_EQ(libraryModel.rowCount(), 0);
}

TEST_F(RestLibraryTableModelTest, UncachedRowsDoNotExposeTrackIdsForAutoDJ) {
    RestLibraryTableModel model(nullptr, trackCollectionManager());
    model.setCacheLoadCapabilitiesEnabled(true);
    model.setTracks({newTrack(
            QStringLiteral("1"),
            QStringLiteral("Beta"),
            QStringLiteral("Second"))});

    EXPECT_TRUE(model.hasCapabilities(TrackModel::Capability::AddToAutoDJ));
    EXPECT_FALSE(model.getTrackId(model.index(0, 0)).isValid());
}

TEST_F(RestLibraryTableModelTest, CacheFailureTooltipIncludesStatusContext) {
    RestLibraryTableModel model(nullptr, trackCollectionManager());
    model.setTracks({newTrack(
            QStringLiteral("1"),
            QStringLiteral("Beta"),
            QStringLiteral("Second"))});

    model.updateTrackCacheState({
            QStringLiteral("1"),
            RestLibraryCacheState::Failed,
            {},
            QStringLiteral("Authentication failed"),
            401,
            0,
            {}});

    const QString tooltip = model.data(model.index(0, 0), Qt::ToolTipRole).toString();
    EXPECT_TRUE(tooltip.startsWith(QStringLiteral("Failed\n")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("Authentication failed")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("HTTP 401")));
}

TEST_F(RestLibraryTableModelTest, CacheStatesUseCompactAccessiblePresentation) {
    RestLibraryTableModel model(nullptr, trackCollectionManager());
    model.setTracks({
            newTrack(QStringLiteral("1"),
                    QStringLiteral("Artist"),
                    QStringLiteral("Missing"),
                    RestLibraryCacheState::Missing),
            newTrack(QStringLiteral("2"),
                    QStringLiteral("Artist"),
                    QStringLiteral("Downloading"),
                    RestLibraryCacheState::Downloading),
            newTrack(QStringLiteral("3"),
                    QStringLiteral("Artist"),
                    QStringLiteral("Ready"),
                    RestLibraryCacheState::Ready),
            newTrack(QStringLiteral("4"),
                    QStringLiteral("Artist"),
                    QStringLiteral("Failed"),
                    RestLibraryCacheState::Failed),
            newTrack(QStringLiteral("5"),
                    QStringLiteral("Artist"),
                    QStringLiteral("Stale"),
                    RestLibraryCacheState::Stale),
    });

    const int cacheColumn = model.fieldIndex(QStringLiteral("cache"));
    ASSERT_EQ(cacheColumn, 0);
    EXPECT_EQ(
            model.headerData(cacheColumn, Qt::Horizontal, TrackModel::kHeaderWidthRole).toInt(),
            36);
    const QStringList stateNames{
            QStringLiteral("Not cached"),
            QStringLiteral("Downloading"),
            QStringLiteral("Ready"),
            QStringLiteral("Failed"),
            QStringLiteral("Stale"),
    };
    for (int row = 0; row < stateNames.size(); ++row) {
        const QModelIndex cacheIndex = model.index(row, cacheColumn);
        EXPECT_FALSE(model.data(cacheIndex, Qt::DisplayRole).isValid());
        EXPECT_EQ(model.data(cacheIndex, Qt::EditRole).toInt(), row);
        EXPECT_EQ(
                model.data(cacheIndex, Qt::AccessibleTextRole).toString(),
                stateNames.at(row));
        EXPECT_EQ(
                model.data(cacheIndex, TrackModel::kDataExportRole).toString(),
                stateNames.at(row));
        const QString tooltip = model.data(cacheIndex, Qt::ToolTipRole).toString();
        EXPECT_TRUE(tooltip.startsWith(stateNames.at(row)));
        EXPECT_EQ(
                model.data(cacheIndex, Qt::AccessibleDescriptionRole).toString(),
                tooltip);
    }

    QTableView tableView;
    std::unique_ptr<QAbstractItemDelegate> delegate(
            model.delegateForColumn(cacheColumn, &tableView));
    EXPECT_NE(
            dynamic_cast<RestLibraryCacheStateDelegate*>(delegate.get()),
            nullptr);

    QList<QImage> renderedStates;
    for (int row = 0; row < stateNames.size(); ++row) {
        QImage image(24, 24, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        QStyleOptionViewItem option;
        option.rect = image.rect();
        option.palette = tableView.palette();
        delegate->paint(&painter, option, model.index(row, cacheColumn));
        painter.end();
        renderedStates.append(std::move(image));
    }
    for (int left = 0; left < renderedStates.size(); ++left) {
        for (int right = left + 1; right < renderedStates.size(); ++right) {
            EXPECT_FALSE(renderedStates.at(left) == renderedStates.at(right));
        }
    }
}

TEST_F(RestLibraryTableModelTest, CacheStateUpdatesInPlace) {
    RestLibraryTableModel model(nullptr, trackCollectionManager());
    model.setTracks({newTrack(
            QStringLiteral("1"), QStringLiteral("Artist"), QStringLiteral("Track"))});
    const QPersistentModelIndex cacheIndex(model.index(0, 0));
    bool modelWasReset = false;
    bool cacheCellChanged = false;
    QVector<int> changedRoles;
    QObject::connect(&model, &QAbstractItemModel::modelReset, [&modelWasReset] {
        modelWasReset = true;
    });
    QObject::connect(&model,
            &QAbstractItemModel::dataChanged,
            [&cacheCellChanged, &changedRoles](
                    const QModelIndex& topLeft,
                    const QModelIndex& bottomRight,
                    const QVector<int>& roles) {
                cacheCellChanged = topLeft == bottomRight && topLeft.column() == 0;
                changedRoles = roles;
            });

    model.updateTrackCacheState({
            QStringLiteral("1"),
            RestLibraryCacheState::Failed,
            {},
            QStringLiteral("Authentication failed"),
            401,
            7,
            {}});

    EXPECT_TRUE(cacheIndex.isValid());
    EXPECT_FALSE(modelWasReset);
    EXPECT_TRUE(cacheCellChanged);
    EXPECT_TRUE(changedRoles.contains(Qt::EditRole));
    EXPECT_TRUE(changedRoles.contains(Qt::AccessibleTextRole));
    EXPECT_EQ(model.data(cacheIndex, Qt::EditRole).toInt(),
            static_cast<int>(RestLibraryCacheState::Failed));
    EXPECT_EQ(model.data(cacheIndex, Qt::AccessibleTextRole).toString(),
            QStringLiteral("Failed"));
    const QString tooltip = model.data(cacheIndex, Qt::ToolTipRole).toString();
    EXPECT_TRUE(tooltip.contains(QStringLiteral("Authentication failed")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("HTTP 401")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("Network error 7")));
}

TEST_F(RestLibraryTableModelTest, SortsCacheStatesThroughStableSortId) {
    RestLibraryTableModel model(nullptr, trackCollectionManager());
    model.setTracks({
            newTrack(QStringLiteral("stale"),
                    QStringLiteral("Artist"),
                    QStringLiteral("Stale"),
                    RestLibraryCacheState::Stale),
            newTrack(QStringLiteral("ready"),
                    QStringLiteral("Artist"),
                    QStringLiteral("Ready"),
                    RestLibraryCacheState::Ready),
            newTrack(QStringLiteral("missing"),
                    QStringLiteral("Artist"),
                    QStringLiteral("Missing"),
                    RestLibraryCacheState::Missing),
    });

    const int cacheColumn = model.fieldIndex(QStringLiteral("cache_state"));
    EXPECT_EQ(model.sortColumnIdFromColumnIndex(cacheColumn),
            TrackModel::SortColumnId::CacheState);
    EXPECT_EQ(model.columnIndexFromSortColumnId(TrackModel::SortColumnId::CacheState),
            cacheColumn);
    model.sort(cacheColumn, Qt::AscendingOrder);
    EXPECT_EQ(model.remoteIdForIndex(model.index(0, 0)), QStringLiteral("missing"));
    EXPECT_EQ(model.remoteIdForIndex(model.index(1, 0)), QStringLiteral("ready"));
    EXPECT_EQ(model.remoteIdForIndex(model.index(2, 0)), QStringLiteral("stale"));
    model.sort(cacheColumn, Qt::DescendingOrder);
    EXPECT_EQ(model.remoteIdForIndex(model.index(0, 0)), QStringLiteral("stale"));
    EXPECT_EQ(model.remoteIdForIndex(model.index(1, 0)), QStringLiteral("ready"));
    EXPECT_EQ(model.remoteIdForIndex(model.index(2, 0)), QStringLiteral("missing"));
}

TEST_F(RestLibraryTableModelTest, CountsCatalogCacheStates) {
    RestLibraryTableModel model(nullptr, trackCollectionManager());
    model.setTracks({
            newTrack(QStringLiteral("1"),
                    QStringLiteral("Ada"),
                    QStringLiteral("Ready"),
                    RestLibraryCacheState::Ready),
            newTrack(QStringLiteral("2"),
                    QStringLiteral("Bea"),
                    QStringLiteral("Downloading"),
                    RestLibraryCacheState::Downloading),
            newTrack(QStringLiteral("3"),
                    QStringLiteral("Cam"),
                    QStringLiteral("Failed"),
                    RestLibraryCacheState::Failed),
            newTrack(QStringLiteral("4"),
                    QStringLiteral("Dee"),
                    QStringLiteral("Missing")),
    });

    EXPECT_EQ(model.cacheStateCount(RestLibraryCacheState::Ready), 1);
    EXPECT_EQ(model.cacheStateCount(RestLibraryCacheState::Downloading), 1);
    EXPECT_EQ(model.cacheStateCount(RestLibraryCacheState::Failed), 1);
    EXPECT_EQ(model.cacheStateCount(RestLibraryCacheState::Missing), 1);
    EXPECT_EQ(model.cacheStateCount(RestLibraryCacheState::Stale), 0);
}

TEST_F(RestLibraryTableModelTest, SearchFiltersVisibleRows) {
    RestLibraryTableModel model(nullptr, trackCollectionManager());
    model.setTracks({
            newTrack(QStringLiteral("1"), QStringLiteral("Beta"), QStringLiteral("Second")),
            newTrack(QStringLiteral("2"), QStringLiteral("Alpha"), QStringLiteral("First")),
    });

    model.search(QStringLiteral("alpha"));

    ASSERT_EQ(model.rowCount(), 1);
    EXPECT_EQ(
            model.data(model.index(0, model.fieldIndex(QStringLiteral("artist")))).toString(),
            QStringLiteral("Alpha"));
}

TEST_F(RestLibraryTableModelTest, CatalogUsesMixxxSearchOperatorsAndNumericSort) {
    RestLibraryTableModel model(
            nullptr,
            trackCollectionManager(),
            RestLibraryTableModel::Mode::Catalog);
    RestLibraryTrack slow = newTrack(
            QStringLiteral("1"), QStringLiteral("Beta"), QStringLiteral("Second"));
    slow.bpm = 90.0;
    slow.durationSeconds = 180.0;
    slow.genre = QStringLiteral("House");
    RestLibraryTrack fast = newTrack(
            QStringLiteral("2"), QStringLiteral("Alpha"), QStringLiteral("First"));
    fast.bpm = 132.0;
    fast.durationSeconds = 180.0;
    fast.genre = QStringLiteral("Techno");
    model.setTracks({slow, fast});

    model.search(QStringLiteral("artist:alpha OR bpm:90"));
    EXPECT_EQ(model.rowCount(), 2);

    model.search(QStringLiteral("genre:house"));
    ASSERT_EQ(model.rowCount(), 1);
    EXPECT_EQ(model.remoteIdForIndex(model.index(0, 0)), QStringLiteral("1"));

    model.search({});
    const int bpmColumn = model.fieldIndex(QStringLiteral("bpm"));
    model.sort(bpmColumn, Qt::AscendingOrder);
    ASSERT_EQ(model.rowCount(), 2);
    EXPECT_DOUBLE_EQ(model.data(model.index(0, bpmColumn)).toDouble(), 90.0);
    EXPECT_DOUBLE_EQ(model.data(model.index(1, bpmColumn)).toDouble(), 132.0);
    EXPECT_EQ(model.visibleRowForRemoteId(QStringLiteral("1")), 0);
    EXPECT_EQ(model.visibleRowForRemoteId(QStringLiteral("2")), 1);
    EXPECT_EQ(model.visibleRowForRemoteId(QStringLiteral("missing")), -1);
}

TEST_F(RestLibraryTableModelTest, CatalogSupportsExplicitRemoteSearchFields) {
    RestLibraryTableModel model(
            nullptr,
            trackCollectionManager(),
            RestLibraryTableModel::Mode::Catalog);
    RestLibraryTrack ready = newTrack(
            QStringLiteral("remote-17"),
            QStringLiteral("Alpha"),
            QStringLiteral("First"),
            RestLibraryCacheState::Ready);
    ready.sourceLabel = QStringLiteral("MixMan Archive");
    RestLibraryTrack missing = newTrack(
            QStringLiteral("remote-18"),
            QStringLiteral("Beta"),
            QStringLiteral("Second"),
            RestLibraryCacheState::Missing);
    missing.sourceLabel = QStringLiteral("Guest Library");
    model.setTracks({ready, missing});

    model.search(QStringLiteral("remote_id:=remote-17"));
    ASSERT_EQ(model.rowCount(), 1);
    EXPECT_EQ(model.remoteIdForIndex(model.index(0, 0)), QStringLiteral("remote-17"));

    model.search(QStringLiteral("source:\"mixman archive\""));
    ASSERT_EQ(model.rowCount(), 1);
    EXPECT_EQ(model.remoteIdForIndex(model.index(0, 0)), QStringLiteral("remote-17"));

    model.search(QStringLiteral("cache:missing OR -source:guest"));
    EXPECT_EQ(model.rowCount(), 2);

    const QList<RestLibraryCacheState> states{
            RestLibraryCacheState::Missing,
            RestLibraryCacheState::Downloading,
            RestLibraryCacheState::Ready,
            RestLibraryCacheState::Failed,
            RestLibraryCacheState::Stale};
    const QStringList stateNames{
            QStringLiteral("missing"),
            QStringLiteral("downloading"),
            QStringLiteral("ready"),
            QStringLiteral("failed"),
            QStringLiteral("stale")};
    for (int i = 0; i < states.size(); ++i) {
        RestLibraryCacheResult result;
        result.remoteId = QStringLiteral("remote-17");
        result.cacheState = states.at(i);
        model.updateTrackCacheState(result);
        model.search(QStringLiteral("cache:=%1").arg(stateNames.at(i)));
        ASSERT_EQ(model.rowCount(), 1);
        EXPECT_EQ(model.remoteIdForIndex(model.index(0, 0)), QStringLiteral("remote-17"));
    }
}

TEST_F(RestLibraryTableModelTest, CatalogCacheSearchUpdatesWhenStateChanges) {
    RestLibraryTableModel model(
            nullptr,
            trackCollectionManager(),
            RestLibraryTableModel::Mode::Catalog);
    model.setTracks({newTrack(
            QStringLiteral("1"),
            QStringLiteral("Alpha"),
            QStringLiteral("First"),
            RestLibraryCacheState::Downloading)});
    model.search(QStringLiteral("cache:ready"));
    EXPECT_EQ(model.rowCount(), 0);

    RestLibraryCacheResult result;
    result.remoteId = QStringLiteral("1");
    result.cacheState = RestLibraryCacheState::Ready;
    model.updateTrackCacheState(result);

    ASSERT_EQ(model.rowCount(), 1);
    EXPECT_EQ(model.remoteIdForIndex(model.index(0, 0)), QStringLiteral("1"));
}

TEST_F(RestLibraryTableModelTest, CatalogLeavesLocalOnlySearchFieldsUnavailable) {
    RestLibraryTableModel model(
            nullptr,
            trackCollectionManager(),
            RestLibraryTableModel::Mode::Catalog);
    model.setTracks({newTrack(
            QStringLiteral("1"), QStringLiteral("Alpha"), QStringLiteral("First"))});

    const QStringList unsupportedQueries{
            QStringLiteral("location:remote"),
            QStringLiteral("lo:remote"),
            QStringLiteral("directory:music"),
            QStringLiteral("dir:music"),
            QStringLiteral("crate:dance"),
            QStringLiteral("c:dance"),
            QStringLiteral("id:1"),
            QStringLiteral("added:today"),
            QStringLiteral("dateadded:today"),
            QStringLiteral("datetime_added:today"),
            QStringLiteral("date_added:today")};
    for (const QString& query : unsupportedQueries) {
        model.search(query);
        EXPECT_EQ(model.rowCount(), 0) << query.toStdString();
    }

    model.search(QStringLiteral("-location:remote"));
    EXPECT_EQ(model.rowCount(), 1);
}

TEST_F(RestLibraryTableModelTest, NormalizedValuesKeepZeroDistinctFromMissing) {
    RestLibraryTableModel model(nullptr, trackCollectionManager());
    RestLibraryTrack zero = newTrack(
            QStringLiteral("zero"), QStringLiteral("Ada"), QStringLiteral("Zero"));
    zero.favour = 0.0;
    zero.energy = 1.0;
    model.setTracks({
            zero,
            newTrack(QStringLiteral("missing"), QStringLiteral("Bea"), QStringLiteral("Missing")),
    });

    const int favourColumn = model.fieldIndex(QStringLiteral("favour"));
    const int energyColumn = model.fieldIndex(QStringLiteral("energy"));
    EXPECT_EQ(model.data(model.index(0, favourColumn)).toString(), QStringLiteral("0%"));
    EXPECT_DOUBLE_EQ(
            model.data(model.index(0, favourColumn), TrackModel::kDataExportRole).toDouble(),
            0.0);
    EXPECT_EQ(model.data(model.index(0, energyColumn)).toString(), QStringLiteral("100%"));
    EXPECT_EQ(model.data(model.index(1, favourColumn)).toString(), QStringLiteral("\u2014"));
    EXPECT_FALSE(model.data(model.index(1, favourColumn), Qt::EditRole).isValid());

    QTableView tableView;
    std::unique_ptr<QAbstractItemDelegate> favourDelegate(
            model.delegateForColumn(favourColumn, &tableView));
    std::unique_ptr<QAbstractItemDelegate> energyDelegate(
            model.delegateForColumn(energyColumn, &tableView));
    EXPECT_NE(dynamic_cast<PercentageDelegate*>(favourDelegate.get()), nullptr);
    EXPECT_NE(dynamic_cast<PercentageDelegate*>(energyDelegate.get()), nullptr);
}

TEST_F(RestLibraryTableModelTest, SortsNormalizedValuesAndKeepsMissingLast) {
    RestLibraryTableModel model(nullptr, trackCollectionManager());
    RestLibraryTrack low = newTrack(
            QStringLiteral("low"), QStringLiteral("Ada"), QStringLiteral("Low"));
    low.favour = 0.0;
    low.energy = 0.25;
    RestLibraryTrack high = newTrack(
            QStringLiteral("high"), QStringLiteral("Bea"), QStringLiteral("High"));
    high.favour = 0.75;
    high.energy = 1.0;
    model.setTracks({
            high,
            newTrack(QStringLiteral("missing"), QStringLiteral("Cam"), QStringLiteral("Missing")),
            low,
    });

    const int favourColumn = model.fieldIndex(QStringLiteral("favour"));
    EXPECT_EQ(
            model.sortColumnIdFromColumnIndex(favourColumn), TrackModel::SortColumnId::Favour);
    EXPECT_EQ(
            model.columnIndexFromSortColumnId(TrackModel::SortColumnId::Favour), favourColumn);
    model.sort(favourColumn, Qt::AscendingOrder);
    EXPECT_EQ(model.remoteIdForIndex(model.index(0, 0)), QStringLiteral("low"));
    EXPECT_EQ(model.remoteIdForIndex(model.index(1, 0)), QStringLiteral("high"));
    EXPECT_EQ(model.remoteIdForIndex(model.index(2, 0)), QStringLiteral("missing"));
    model.sort(favourColumn, Qt::DescendingOrder);
    EXPECT_EQ(model.remoteIdForIndex(model.index(0, 0)), QStringLiteral("high"));
    EXPECT_EQ(model.remoteIdForIndex(model.index(1, 0)), QStringLiteral("low"));
    EXPECT_EQ(model.remoteIdForIndex(model.index(2, 0)), QStringLiteral("missing"));

    const int energyColumn = model.fieldIndex(QStringLiteral("energy"));
    EXPECT_EQ(
            model.sortColumnIdFromColumnIndex(energyColumn), TrackModel::SortColumnId::Energy);
    EXPECT_EQ(
            model.columnIndexFromSortColumnId(TrackModel::SortColumnId::Energy), energyColumn);
}

TEST_F(RestLibraryTableModelTest, SortsKeysByConfiguredCircleOfFifthsOrder) {
    ControlObject::set(
            mixxx::library::prefs::kKeyNotationConfigKey,
            static_cast<double>(KeyUtils::KeyNotation::Lancelot));
    RestLibraryTableModel model(nullptr, trackCollectionManager());
    RestLibraryTrack eightA = newTrack(
            QStringLiteral("8a"), QStringLiteral("Ada"), QStringLiteral("Eight A"));
    eightA.keyText = QStringLiteral("8A");
    RestLibraryTrack nineA = newTrack(
            QStringLiteral("9a"), QStringLiteral("Bea"), QStringLiteral("Nine A"));
    nineA.keyText = QStringLiteral("9A");
    model.setTracks({
            nineA,
            newTrack(QStringLiteral("missing"), QStringLiteral("Cam"), QStringLiteral("Missing")),
            eightA,
    });

    const int keyColumn = model.fieldIndex(QStringLiteral("key"));
    model.sort(keyColumn, Qt::AscendingOrder);
    EXPECT_EQ(model.remoteIdForIndex(model.index(0, 0)), QStringLiteral("8a"));
    EXPECT_EQ(model.remoteIdForIndex(model.index(1, 0)), QStringLiteral("9a"));
    EXPECT_EQ(model.remoteIdForIndex(model.index(2, 0)), QStringLiteral("missing"));
}

TEST_F(RestLibraryTableModelTest, ColorColumnUsesSharedAccessiblePresentation) {
    for (const auto mode : {RestLibraryTableModel::Mode::Recommendations,
                 RestLibraryTableModel::Mode::Catalog}) {
        RestLibraryTableModel model(nullptr, trackCollectionManager(), mode);
        RestLibraryTrack valid = newTrack(
                QStringLiteral("valid"), QStringLiteral("Ada"), QStringLiteral("Valid"));
        valid.color = QStringLiteral("#FF6600");
        RestLibraryTrack missing = newTrack(
                QStringLiteral("missing"), QStringLiteral("Bea"), QStringLiteral("Missing"));
        RestLibraryTrack invalid = newTrack(
                QStringLiteral("invalid"), QStringLiteral("Cam"), QStringLiteral("Invalid"));
        invalid.color = QStringLiteral("not-a-color");
        model.setTracks({valid, missing, invalid});

        const int colorColumn = model.fieldIndex(QStringLiteral("color"));
        ASSERT_GE(colorColumn, 0);
        EXPECT_EQ(model.fieldIndex(QStringLiteral("colour")), colorColumn);
        EXPECT_FALSE(model.isColumnInternal(colorColumn));
        EXPECT_FALSE(model.isColumnHiddenByDefault(colorColumn));
        EXPECT_EQ(model.headerData(colorColumn, Qt::Horizontal).toString(),
                QStringLiteral("Color"));
        EXPECT_EQ(model.headerData(
                          colorColumn,
                          Qt::Horizontal,
                          TrackModel::kHeaderWidthRole)
                          .toInt(),
                44);
        EXPECT_EQ(model.sortColumnIdFromColumnIndex(colorColumn),
                TrackModel::SortColumnId::Color);
        EXPECT_EQ(model.columnIndexFromSortColumnId(TrackModel::SortColumnId::Color),
                colorColumn);

        const QModelIndex validIndex = model.index(0, colorColumn);
        const auto expectedColor = mixxx::RgbColor::fromQString(QStringLiteral("#ff6600"));
        ASSERT_TRUE(expectedColor.has_value());
        EXPECT_EQ(model.data(validIndex, Qt::DisplayRole).toUInt(),
                static_cast<QRgb>(*expectedColor));
        EXPECT_EQ(model.data(validIndex, TrackModel::kDataExportRole).toString(),
                QStringLiteral("#ff6600"));
        EXPECT_EQ(model.data(validIndex, Qt::AccessibleTextRole).toString(),
                QStringLiteral("Color: #ff6600"));
        EXPECT_EQ(model.data(validIndex, Qt::ToolTipRole).toString(),
                QStringLiteral("Color: #ff6600"));

        const QModelIndex missingIndex = model.index(1, colorColumn);
        EXPECT_FALSE(model.data(missingIndex, Qt::DisplayRole).isValid());
        EXPECT_EQ(model.data(missingIndex, Qt::AccessibleTextRole).toString(),
                QStringLiteral("Color: not provided"));
        const QModelIndex invalidIndex = model.index(2, colorColumn);
        EXPECT_FALSE(model.data(invalidIndex, Qt::DisplayRole).isValid());
        EXPECT_EQ(model.data(invalidIndex, Qt::AccessibleTextRole).toString(),
                QStringLiteral("Color: invalid value not-a-color"));

        QTableView tableView;
        std::unique_ptr<QAbstractItemDelegate> delegate(
                model.delegateForColumn(colorColumn, &tableView));
        EXPECT_NE(dynamic_cast<ColorDelegate*>(delegate.get()), nullptr);
    }
}

TEST_F(RestLibraryTableModelTest, SortsColorsAndKeepsUnavailableValuesLast) {
    RestLibraryTableModel model(nullptr, trackCollectionManager());
    RestLibraryTrack red = newTrack(
            QStringLiteral("red"), QStringLiteral("Red"), QStringLiteral("Red"));
    red.color = QStringLiteral("#ff0000");
    RestLibraryTrack blue = newTrack(
            QStringLiteral("blue"), QStringLiteral("Blue"), QStringLiteral("Blue"));
    blue.color = QStringLiteral("#0000ff");
    RestLibraryTrack green = newTrack(
            QStringLiteral("green"), QStringLiteral("Green"), QStringLiteral("Green"));
    green.color = QStringLiteral("#00ff00");
    RestLibraryTrack invalid = newTrack(
            QStringLiteral("invalid"), QStringLiteral("Invalid"), QStringLiteral("Invalid"));
    invalid.color = QStringLiteral("invalid");
    model.setTracks({red, invalid, green, blue});

    const int colorColumn = model.fieldIndex(QStringLiteral("color"));
    model.sort(colorColumn, Qt::AscendingOrder);
    EXPECT_EQ(model.remoteIdForIndex(model.index(0, 0)), QStringLiteral("blue"));
    EXPECT_EQ(model.remoteIdForIndex(model.index(1, 0)), QStringLiteral("green"));
    EXPECT_EQ(model.remoteIdForIndex(model.index(2, 0)), QStringLiteral("red"));
    EXPECT_EQ(model.remoteIdForIndex(model.index(3, 0)), QStringLiteral("invalid"));

    model.sort(colorColumn, Qt::DescendingOrder);
    EXPECT_EQ(model.remoteIdForIndex(model.index(0, 0)), QStringLiteral("red"));
    EXPECT_EQ(model.remoteIdForIndex(model.index(1, 0)), QStringLiteral("green"));
    EXPECT_EQ(model.remoteIdForIndex(model.index(2, 0)), QStringLiteral("blue"));
    EXPECT_EQ(model.remoteIdForIndex(model.index(3, 0)), QStringLiteral("invalid"));
}

TEST_F(RestLibraryTableModelTest, RecommendationRankIsStableAccessibleAndRestorable) {
    RestLibraryTableModel model(nullptr, trackCollectionManager());
    RestLibraryTrack missing = newTrack(
            QStringLiteral("missing"), QStringLiteral("Delta"), QStringLiteral("Missing"));
    RestLibraryTrack second = newTrack(
            QStringLiteral("second"), QStringLiteral("Charlie"), QStringLiteral("Second"));
    second.recommendationPosition = 2;
    RestLibraryTrack primary = newTrack(
            QStringLiteral("primary"), QStringLiteral("Zulu"), QStringLiteral("Primary"));
    primary.recommendationPosition = 1;
    primary.favour = 0.8;
    RestLibraryTrack duplicate = newTrack(
            QStringLiteral("duplicate"), QStringLiteral("Alpha"), QStringLiteral("Duplicate"));
    duplicate.recommendationPosition = 1;
    model.setTracks({missing, second, primary, duplicate});

    const int rankColumn = model.fieldIndex(QStringLiteral("recommendation_rank"));
    ASSERT_GE(rankColumn, 0);
    EXPECT_EQ(model.fieldIndex(QStringLiteral("position")), rankColumn);
    EXPECT_EQ(model.defaultSortColumn(), rankColumn);
    EXPECT_EQ(model.modelKey(true), QStringLiteral("rest-library-recommendations-v2"));
    EXPECT_EQ(model.sortColumnIdFromColumnIndex(rankColumn),
            TrackModel::SortColumnId::Position);
    EXPECT_EQ(model.columnIndexFromSortColumnId(TrackModel::SortColumnId::Position),
            rankColumn);
    EXPECT_FALSE(model.isColumnInternal(rankColumn));
    EXPECT_FALSE(model.isColumnHiddenByDefault(rankColumn));

    EXPECT_EQ(model.remoteIdForIndex(model.index(0, 0)), QStringLiteral("primary"));
    EXPECT_EQ(model.remoteIdForIndex(model.index(1, 0)), QStringLiteral("duplicate"));
    EXPECT_EQ(model.remoteIdForIndex(model.index(2, 0)), QStringLiteral("second"));
    EXPECT_EQ(model.remoteIdForIndex(model.index(3, 0)), QStringLiteral("missing"));
    EXPECT_EQ(model.data(model.index(0, rankColumn)).toString(), QStringLiteral("Top"));
    EXPECT_EQ(model.data(model.index(1, rankColumn)).toInt(), 2);
    EXPECT_EQ(model.data(model.index(0, rankColumn), Qt::AccessibleTextRole).toString(),
            QStringLiteral("Primary recommendation"));
    EXPECT_EQ(model.data(model.index(1, rankColumn), Qt::AccessibleTextRole).toString(),
            QStringLiteral("Alternate recommendation, rank 2"));
    EXPECT_TRUE(model.data(model.index(0, model.fieldIndex(QStringLiteral("artist"))),
                              Qt::FontRole)
                        .value<QFont>()
                        .bold());
    EXPECT_FALSE(model.data(model.index(1, model.fieldIndex(QStringLiteral("artist"))),
                               Qt::FontRole)
                         .isValid());

    const int favourColumn = model.fieldIndex(QStringLiteral("favour"));
    QTableView tableView;
    std::unique_ptr<QAbstractItemDelegate> favourDelegate(
            model.delegateForColumn(favourColumn, &tableView));
    auto* pPercentageDelegate =
            dynamic_cast<PercentageDelegate*>(favourDelegate.get());
    ASSERT_NE(pPercentageDelegate, nullptr);
    QImage image(80, 24, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    QStyleOptionViewItem option;
    option.rect = image.rect();
    option.palette = tableView.palette();
    option.font = model.data(model.index(0, favourColumn), Qt::FontRole).value<QFont>();
    pPercentageDelegate->paintItem(
            &painter, option, model.index(0, favourColumn));
    EXPECT_TRUE(painter.font().bold());
    painter.end();

    const int artistColumn = model.fieldIndex(QStringLiteral("artist"));
    model.sort(artistColumn, Qt::AscendingOrder);
    EXPECT_EQ(model.remoteIdForIndex(model.index(0, 0)), QStringLiteral("duplicate"));
    EXPECT_EQ(model.data(model.index(0, rankColumn)).toInt(), 2);
    EXPECT_EQ(model.remoteIdForIndex(model.index(3, 0)), QStringLiteral("primary"));
    EXPECT_EQ(model.data(model.index(3, rankColumn)).toString(), QStringLiteral("Top"));
    EXPECT_TRUE(model.data(model.index(3, artistColumn), Qt::FontRole)
                        .value<QFont>()
                        .bold());

    RestLibraryCacheResult cacheResult;
    cacheResult.remoteId = QStringLiteral("primary");
    cacheResult.cacheState = RestLibraryCacheState::Ready;
    model.updateTrackCacheState(cacheResult);
    EXPECT_EQ(model.remoteIdForIndex(model.index(0, 0)), QStringLiteral("duplicate"));
    EXPECT_EQ(model.data(model.index(3, rankColumn)).toString(), QStringLiteral("Top"));

    model.sort(rankColumn, Qt::AscendingOrder);
    EXPECT_EQ(model.remoteIdForIndex(model.index(0, 0)), QStringLiteral("primary"));
    model.setTracks({primary, duplicate, missing, second});
    EXPECT_EQ(model.remoteIdForIndex(model.index(0, 0)), QStringLiteral("primary"));
    EXPECT_EQ(model.remoteIdForIndex(model.index(1, 0)), QStringLiteral("duplicate"));
    EXPECT_EQ(model.remoteIdForIndex(model.index(2, 0)), QStringLiteral("second"));
    EXPECT_EQ(model.remoteIdForIndex(model.index(3, 0)), QStringLiteral("missing"));
    model.search(QStringLiteral("Duplicate"));
    ASSERT_EQ(model.rowCount(), 1);
    EXPECT_EQ(model.data(model.index(0, rankColumn)).toInt(), 2);
}

TEST_F(RestLibraryTableModelTest, CatalogKeepsRecommendationRankInternal) {
    RestLibraryTableModel model(
            nullptr,
            trackCollectionManager(),
            RestLibraryTableModel::Mode::Catalog);
    const int rankColumn = model.fieldIndex(QStringLiteral("recommendation_rank"));
    ASSERT_GE(rankColumn, 0);
    EXPECT_TRUE(model.isColumnInternal(rankColumn));
    EXPECT_EQ(model.sortColumnIdFromColumnIndex(rankColumn),
            TrackModel::SortColumnId::Invalid);
    EXPECT_EQ(model.columnIndexFromSortColumnId(TrackModel::SortColumnId::Position), -1);
    EXPECT_EQ(model.modelKey(true), QStringLiteral("rest-library-browser"));
}

TEST_F(RestLibraryTableModelTest, RestModesKeepPersistedHeaderStateIndependent) {
    RestLibraryTableModel recommendations(nullptr, trackCollectionManager());
    RestLibraryTableModel catalog(
            nullptr,
            trackCollectionManager(),
            RestLibraryTableModel::Mode::Catalog);

    EXPECT_TRUE(catalog.setModelSetting(
            QStringLiteral("header_state_pb"), QStringLiteral("catalog-state")));
    EXPECT_TRUE(recommendations.getModelSetting(
                                       QStringLiteral("header_state_pb"))
                        .isNull());
    EXPECT_TRUE(recommendations.setModelSetting(
            QStringLiteral("header_state_pb"), QStringLiteral("recommendations-state")));
    EXPECT_EQ(catalog.getModelSetting(QStringLiteral("header_state_pb")),
            QStringLiteral("catalog-state"));
    EXPECT_EQ(recommendations.getModelSetting(QStringLiteral("header_state_pb")),
            QStringLiteral("recommendations-state"));
}
