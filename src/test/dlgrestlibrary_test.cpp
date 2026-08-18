#include <gtest/gtest.h>

#include <type_traits>

#include <QGridLayout>
#include <QMetaMethod>
#include <QSizePolicy>
#include <QWidget>

#include "library/libraryview.h"
#include "library/rest/dlgrestlibrary.h"
#include "library/rest/ui_dlgrestlibrary.h"

namespace {

bool hasSignal(const QMetaObject& metaObject, const char* signature) {
    return metaObject.indexOfSignal(QMetaObject::normalizedSignature(signature).constData()) >= 0;
}

int layoutRow(QGridLayout* pLayout, QWidget* pWidget) {
    const int index = pLayout->indexOf(pWidget);
    if (index < 0) {
        return -1;
    }
    int row = -1;
    int column = -1;
    int rowSpan = -1;
    int columnSpan = -1;
    pLayout->getItemPosition(index, &row, &column, &rowSpan, &columnSpan);
    return row;
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

TEST(DlgRestLibraryTest, PolicyControlsUseAccessibleTwoRowNarrowLayout) {
    QWidget root;
    Ui::DlgRestLibrary ui;
    ui.setupUi(&root);

    auto* pLayout = qobject_cast<QGridLayout*>(ui.MixManControls->layout());
    ASSERT_NE(pLayout, nullptr);
    EXPECT_EQ(pLayout->rowCount(), 2);
    EXPECT_EQ(layoutRow(pLayout, ui.comboBoxPolicyPreset), 0);
    EXPECT_EQ(layoutRow(pLayout, ui.pushButtonReroll), 0);
    EXPECT_EQ(layoutRow(pLayout, ui.checkBoxTargetEnergy), 1);
    EXPECT_EQ(layoutRow(pLayout, ui.horizontalSliderTargetEnergy), 1);
    EXPECT_EQ(layoutRow(pLayout, ui.labelTargetEnergyValue), 1);
    EXPECT_EQ(layoutRow(pLayout, ui.checkBoxTargetColor), 1);
    EXPECT_EQ(layoutRow(pLayout, ui.pushButtonTargetColor), 1);
    EXPECT_EQ(layoutRow(pLayout, ui.checkBoxTargetBpm), 1);
    EXPECT_EQ(layoutRow(pLayout, ui.spinBoxTargetBpm), 1);

    EXPECT_EQ(ui.horizontalSliderTargetEnergy->minimum(), 0);
    EXPECT_EQ(ui.horizontalSliderTargetEnergy->maximum(), 5);
    EXPECT_EQ(ui.horizontalSliderTargetEnergy->tickInterval(), 1);
    EXPECT_GE(ui.horizontalSliderTargetEnergy->minimumWidth(), 120);
    EXPECT_EQ(ui.horizontalSliderTargetEnergy->sizePolicy().horizontalPolicy(),
            QSizePolicy::Expanding);
    EXPECT_FALSE(ui.horizontalSliderTargetEnergy->accessibleName().isEmpty());
    EXPECT_FALSE(ui.horizontalSliderTargetEnergy->toolTip().isEmpty());
    EXPECT_EQ(ui.labelTargetEnergyValue->text(), QStringLiteral("0"));

    EXPECT_EQ(ui.pushButtonRefresh->text(), QStringLiteral("Sync"));
    EXPECT_FALSE(ui.pushButtonRefresh->accessibleName().isEmpty());
    EXPECT_FALSE(ui.pushButtonRefresh->toolTip().isEmpty());
    EXPECT_EQ(ui.pushButtonReroll->text(), QStringLiteral("New suggestions"));
    EXPECT_FALSE(ui.pushButtonReroll->accessibleName().isEmpty());
    EXPECT_FALSE(ui.pushButtonReroll->toolTip().isEmpty());
    EXPECT_FALSE(ui.pushButtonTargetColor->accessibleName().isEmpty());
    EXPECT_FALSE(ui.spinBoxTargetBpm->accessibleName().isEmpty());
}
