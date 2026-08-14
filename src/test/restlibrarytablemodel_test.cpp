#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QTemporaryDir>

#include "library/rest/restlibrarytablemodel.h"
#include "test/librarytest.h"

namespace {

using mixxx::library::rest::RestLibraryCacheState;
using mixxx::library::rest::RestLibraryTableModel;
using mixxx::library::rest::RestLibraryTrack;

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
            {}});

    EXPECT_EQ(
            model.getTrackLocation(model.index(0, 0)),
            QDir::fromNativeSeparators(filePath));
    EXPECT_TRUE(model.getTrack(model.index(0, 0)));
    EXPECT_TRUE(model.getTrackId(model.index(0, 0)).isValid());
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
    EXPECT_TRUE(tooltip.contains(QStringLiteral("Authentication failed")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("HTTP 401")));
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

TEST_F(RestLibraryTableModelTest, CatalogLeavesLocalOnlySearchFieldsUnavailable) {
    RestLibraryTableModel model(
            nullptr,
            trackCollectionManager(),
            RestLibraryTableModel::Mode::Catalog);
    model.setTracks({newTrack(
            QStringLiteral("1"), QStringLiteral("Alpha"), QStringLiteral("First"))});

    model.search(QStringLiteral("location:remote"));

    EXPECT_EQ(model.rowCount(), 0);
}
