#include <gtest/gtest.h>

#include <type_traits>

#include <QMetaMethod>

#include "library/libraryview.h"
#include "library/rest/dlgrestlibrary.h"

namespace {

bool hasSignal(const QMetaObject& metaObject, const char* signature) {
    return metaObject.indexOfSignal(QMetaObject::normalizedSignature(signature).constData()) >= 0;
}

} // namespace

TEST(DlgRestLibraryTest, ExposesLibraryViewContractAndSignals) {
    static_assert(std::is_base_of_v<LibraryView, mixxx::library::rest::DlgRestLibrary>);

    const QMetaObject& metaObject = mixxx::library::rest::DlgRestLibrary::staticMetaObject;
    EXPECT_TRUE(hasSignal(metaObject, "refreshRequested()"));
    EXPECT_TRUE(hasSignal(metaObject, "followCurrentTrackChanged(bool)"));
    EXPECT_TRUE(hasSignal(metaObject, "policyPresetChanged(QString)"));
    EXPECT_TRUE(hasSignal(metaObject, "targetEnergyChanged(bool,int)"));
    EXPECT_TRUE(hasSignal(metaObject, "targetColorChanged(bool,QString)"));
    EXPECT_TRUE(hasSignal(metaObject, "targetBpmChanged(bool,int)"));
    EXPECT_TRUE(hasSignal(metaObject, "rerollRequested()"));
    EXPECT_TRUE(hasSignal(metaObject, "autoDJToggleRequested(bool)"));
    EXPECT_TRUE(hasSignal(metaObject, "autoDJFadeNowRequested()"));
    EXPECT_TRUE(hasSignal(metaObject, "autoDJSkipNextRequested()"));
    EXPECT_TRUE(hasSignal(metaObject, "loadTrack(TrackPointer)"));
    EXPECT_TRUE(hasSignal(metaObject, "trackSelected(TrackPointer)"));
}
