#include <gtest/gtest.h>

#include <QCheckBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>

#include "library/rest/restlibrarysettings.h"
#include "preferences/dialog/dlgprefrestlibrary.h"
#include "test/mixxxtest.h"

namespace {

namespace restConfig = mixxx::library::rest::config;

template<typename Widget>
Widget* requireChild(QWidget* pParent, const char* name) {
    Widget* pWidget = pParent->findChild<Widget*>(QString::fromUtf8(name));
    EXPECT_NE(pWidget, nullptr) << name;
    return pWidget;
}

} // namespace

class DlgPrefRestLibraryTest : public MixxxTest {
};

TEST_F(DlgPrefRestLibraryTest, LoadsAndAppliesSettings) {
    config()->setValue(restConfig::kEnabledKey, true);
    config()->setValue(restConfig::kBaseUrlKey, QStringLiteral("https://example.com/api"));
    config()->setValue(restConfig::kLocalDevBearerTokenKey, QStringLiteral("old-token"));
    config()->setValue(restConfig::kUseMixManDefaultsKey, false);
    config()->setValue(restConfig::kMixManSessionIdKey, QStringLiteral("old-room"));
    config()->setValue(restConfig::kTrackListPathKey, QStringLiteral("/old-tracks"));
    config()->setValue(restConfig::kTrackDetailPathTemplateKey, QStringLiteral("/old/%1"));
    config()->setValue(restConfig::kTrackLookupPathTemplateKey, QStringLiteral("/lookup"));
    config()->setValue(restConfig::kRecommendationPathTemplateKey, QStringLiteral("/old/%1/related"));
    config()->setValue(restConfig::kRecommendationLimitKey, 6);
    config()->setValue(restConfig::kMixManPathDepthKey, 7);
    config()->setValue(restConfig::kMixManAdminApprovedOnlyKey, false);
    config()->setValue(restConfig::kAudioDownloadPathTemplateKey, QStringLiteral("/old/%1/audio"));
    config()->setValue(restConfig::kCacheEnabledKey, true);
    config()->setValue(restConfig::kCacheDirectoryKey, QStringLiteral("C:/cache"));
    config()->setValue(restConfig::kPageSizeKey, 30);
    config()->setValue(restConfig::kCacheMaxMegabytesKey, 512);
    config()->setValue(restConfig::kCacheMaxAgeDaysKey, 10);
    config()->setValue(restConfig::kMaxConcurrentDownloadsKey, 3);

    DlgPrefRestLibrary page(nullptr, config());

    auto* pEnabled = requireChild<QCheckBox>(&page, "checkBoxEnabled");
    auto* pBaseUrl = requireChild<QLineEdit>(&page, "lineEditBaseUrl");
    auto* pToken = requireChild<QLineEdit>(&page, "lineEditBearerToken");
    auto* pUseMixManDefaults = requireChild<QCheckBox>(&page, "checkBoxUseMixManDefaults");
    auto* pSessionId = requireChild<QLineEdit>(&page, "lineEditMixManSessionId");
    auto* pMixManPathDepth = requireChild<QSpinBox>(&page, "spinBoxMixManPathDepth");
    auto* pMixManAdminApprovedOnly =
            requireChild<QCheckBox>(&page, "checkBoxMixManAdminApprovedOnly");
    auto* pTrackList = requireChild<QLineEdit>(&page, "lineEditTrackListPath");
    auto* pTrackDetail = requireChild<QLineEdit>(&page, "lineEditTrackDetailPathTemplate");
    auto* pTrackLookup = requireChild<QLineEdit>(&page, "lineEditTrackLookupPathTemplate");
    auto* pRecommendations = requireChild<QLineEdit>(&page, "lineEditRecommendationPathTemplate");
    auto* pAudioDownload = requireChild<QLineEdit>(&page, "lineEditAudioDownloadPathTemplate");
    auto* pCacheDirectory = requireChild<QLineEdit>(&page, "lineEditCacheDirectory");
    auto* pPageSize = requireChild<QSpinBox>(&page, "spinBoxPageSize");
    auto* pRecommendationLimit = requireChild<QSpinBox>(&page, "spinBoxRecommendationLimit");
    auto* pCacheMaxMegabytes = requireChild<QSpinBox>(&page, "spinBoxCacheMaxMegabytes");
    auto* pCacheMaxAgeDays = requireChild<QSpinBox>(&page, "spinBoxCacheMaxAgeDays");
    auto* pMaxConcurrentDownloads =
            requireChild<QSpinBox>(&page, "spinBoxMaxConcurrentDownloads");

    EXPECT_TRUE(pEnabled->isChecked());
    EXPECT_EQ(pBaseUrl->text(), QStringLiteral("https://example.com/api"));
    EXPECT_EQ(pToken->text(), QStringLiteral("old-token"));
    EXPECT_FALSE(pUseMixManDefaults->isChecked());
    EXPECT_EQ(pSessionId->text(), QStringLiteral("old-room"));
    EXPECT_EQ(pMixManPathDepth->value(), 7);
    EXPECT_FALSE(pMixManAdminApprovedOnly->isChecked());
    EXPECT_EQ(pTrackList->text(), QStringLiteral("/old-tracks"));
    EXPECT_EQ(pTrackDetail->text(), QStringLiteral("/old/%1"));
    EXPECT_EQ(pTrackLookup->text(), QStringLiteral("/lookup"));
    EXPECT_EQ(pRecommendations->text(), QStringLiteral("/old/%1/related"));
    EXPECT_EQ(pRecommendationLimit->value(), 6);
    EXPECT_EQ(pAudioDownload->text(), QStringLiteral("/old/%1/audio"));
    EXPECT_EQ(pCacheDirectory->text(), QStringLiteral("C:/cache"));
    EXPECT_EQ(pPageSize->value(), 30);
    EXPECT_EQ(pCacheMaxMegabytes->value(), 512);
    EXPECT_EQ(pCacheMaxAgeDays->value(), 10);
    EXPECT_EQ(pMaxConcurrentDownloads->value(), 3);

    pBaseUrl->setText(QStringLiteral("https://new.example.test"));
    pToken->setText(QStringLiteral("new-token"));
    pUseMixManDefaults->setChecked(true);
    pSessionId->setText(QStringLiteral("new-room"));
    pMixManPathDepth->setValue(5);
    pMixManAdminApprovedOnly->setChecked(true);
    pTrackList->setText(QStringLiteral("/tracks"));
    pTrackDetail->setText(QStringLiteral("/tracks/%1"));
    pTrackLookup->setText(QStringLiteral("/find?artist=%artist&title=%title"));
    pRecommendations->setText(QStringLiteral("/tracks/%1/recommendations"));
    pAudioDownload->setText(QStringLiteral("/tracks/%1/audio"));
    pCacheDirectory->setText(QStringLiteral("D:\\rest-cache"));
    pPageSize->setValue(40);
    pRecommendationLimit->setValue(8);
    pCacheMaxMegabytes->setValue(1024);
    pCacheMaxAgeDays->setValue(20);
    pMaxConcurrentDownloads->setValue(4);

    page.slotApply();

    EXPECT_EQ(config()->getValueString(restConfig::kBaseUrlKey), QStringLiteral("https://new.example.test"));
    EXPECT_EQ(config()->getValueString(restConfig::kLocalDevBearerTokenKey), QStringLiteral("new-token"));
    EXPECT_TRUE(config()->getValue(restConfig::kUseMixManDefaultsKey, false));
    EXPECT_EQ(config()->getValueString(restConfig::kMixManSessionIdKey),
            QStringLiteral("new-room"));
    EXPECT_EQ(config()->getValue(restConfig::kMixManPathDepthKey, 0), 5);
    EXPECT_TRUE(config()->getValue(restConfig::kMixManAdminApprovedOnlyKey, false));
    EXPECT_EQ(config()->getValueString(restConfig::kTrackListPathKey), QStringLiteral("/tracks"));
    EXPECT_EQ(config()->getValueString(restConfig::kTrackDetailPathTemplateKey), QStringLiteral("/tracks/%1"));
    EXPECT_EQ(
            config()->getValueString(restConfig::kTrackLookupPathTemplateKey),
            QStringLiteral("/find?artist=%artist&title=%title"));
    EXPECT_EQ(
            config()->getValueString(restConfig::kRecommendationPathTemplateKey),
            QStringLiteral("/tracks/%1/recommendations"));
    EXPECT_EQ(
            config()->getValueString(restConfig::kAudioDownloadPathTemplateKey),
            QStringLiteral("/tracks/%1/audio"));
    EXPECT_EQ(
            config()->getValueString(restConfig::kCacheDirectoryKey),
            QDir::fromNativeSeparators(QStringLiteral("D:\\rest-cache")));
    EXPECT_EQ(config()->getValue(restConfig::kPageSizeKey, 0), 40);
    EXPECT_EQ(config()->getValue(restConfig::kRecommendationLimitKey, 0), 8);
    EXPECT_EQ(config()->getValue(restConfig::kCacheMaxMegabytesKey, 0), 1024);
    EXPECT_EQ(config()->getValue(restConfig::kCacheMaxAgeDaysKey, 0), 20);
    EXPECT_EQ(config()->getValue(restConfig::kMaxConcurrentDownloadsKey, 0), 4);
}

TEST_F(DlgPrefRestLibraryTest, ResetToDefaultsRestoresDefaultValues) {
    DlgPrefRestLibrary page(nullptr, config());

    page.slotResetToDefaults();

    EXPECT_FALSE(requireChild<QCheckBox>(&page, "checkBoxEnabled")->isChecked());
    EXPECT_EQ(requireChild<QLineEdit>(&page, "lineEditBaseUrl")->text(), QString());
    EXPECT_EQ(
            requireChild<QSpinBox>(&page, "spinBoxPageSize")->value(),
            restConfig::kDefaultPageSize);
    EXPECT_EQ(
            requireChild<QSpinBox>(&page, "spinBoxRecommendationLimit")->value(),
            restConfig::kDefaultRecommendationLimit);
    EXPECT_TRUE(requireChild<QCheckBox>(&page, "checkBoxUseMixManDefaults")->isChecked());
    EXPECT_TRUE(requireChild<QLineEdit>(&page, "lineEditMixManSessionId")->text().isEmpty());
    EXPECT_EQ(
            requireChild<QSpinBox>(&page, "spinBoxMixManPathDepth")->value(),
            restConfig::kDefaultMixManPathDepth);
    EXPECT_TRUE(requireChild<QCheckBox>(&page, "checkBoxMixManAdminApprovedOnly")->isChecked());
    EXPECT_EQ(
            requireChild<QLineEdit>(&page, "lineEditCacheDirectory")->text(),
            restConfig::defaultCacheDirectoryPath(config()));
    EXPECT_EQ(
            requireChild<QSpinBox>(&page, "spinBoxCacheMaxMegabytes")->value(),
            restConfig::kDefaultCacheMaxMegabytes);

    page.slotApply();

    EXPECT_FALSE(config()->getValue(restConfig::kEnabledKey, true));
    EXPECT_EQ(config()->getValue(restConfig::kPageSizeKey, 0), restConfig::kDefaultPageSize);
    EXPECT_EQ(
            config()->getValue(restConfig::kRecommendationLimitKey, 0),
            restConfig::kDefaultRecommendationLimit);
    EXPECT_TRUE(config()->getValue(restConfig::kUseMixManDefaultsKey, false));
}

TEST_F(DlgPrefRestLibraryTest, CacheControlsFollowCacheEnabledCheckbox) {
    DlgPrefRestLibrary page(nullptr, config());

    auto* pCacheEnabled = requireChild<QCheckBox>(&page, "checkBoxCacheEnabled");
    auto* pCacheDirectory = requireChild<QLineEdit>(&page, "lineEditCacheDirectory");
    auto* pBrowse = requireChild<QPushButton>(&page, "pushButtonBrowseCacheDirectory");
    auto* pCacheMaxMegabytes = requireChild<QSpinBox>(&page, "spinBoxCacheMaxMegabytes");

    pCacheEnabled->setChecked(false);

    EXPECT_FALSE(pCacheDirectory->isEnabled());
    EXPECT_FALSE(pBrowse->isEnabled());
    EXPECT_FALSE(pCacheMaxMegabytes->isEnabled());

    pCacheEnabled->setChecked(true);

    EXPECT_TRUE(pCacheDirectory->isEnabled());
    EXPECT_TRUE(pBrowse->isEnabled());
    EXPECT_TRUE(pCacheMaxMegabytes->isEnabled());
}

TEST_F(DlgPrefRestLibraryTest, MixManDefaultsOnlyDisablePathControls) {
    DlgPrefRestLibrary page(nullptr, config());

    auto* pUseMixManDefaults = requireChild<QCheckBox>(&page, "checkBoxUseMixManDefaults");
    auto* pTrackList = requireChild<QLineEdit>(&page, "lineEditTrackListPath");
    auto* pTrackDetail = requireChild<QLineEdit>(&page, "lineEditTrackDetailPathTemplate");
    auto* pTrackLookup = requireChild<QLineEdit>(&page, "lineEditTrackLookupPathTemplate");
    auto* pRecommendations = requireChild<QLineEdit>(&page, "lineEditRecommendationPathTemplate");
    auto* pAudioDownload = requireChild<QLineEdit>(&page, "lineEditAudioDownloadPathTemplate");
    auto* pPageSize = requireChild<QSpinBox>(&page, "spinBoxPageSize");
    auto* pRecommendationLimit = requireChild<QSpinBox>(&page, "spinBoxRecommendationLimit");
    auto* pPageSizeLabel = requireChild<QLabel>(&page, "labelPageSize");
    auto* pRecommendationLimitLabel = requireChild<QLabel>(&page, "labelRecommendationLimit");

    pUseMixManDefaults->setChecked(true);

    EXPECT_FALSE(pTrackList->isEnabled());
    EXPECT_FALSE(pTrackDetail->isEnabled());
    EXPECT_FALSE(pTrackLookup->isEnabled());
    EXPECT_FALSE(pRecommendations->isEnabled());
    EXPECT_FALSE(pAudioDownload->isEnabled());
    EXPECT_TRUE(pPageSize->isEnabled());
    EXPECT_TRUE(pRecommendationLimit->isEnabled());
    EXPECT_TRUE(pPageSizeLabel->isEnabled());
    EXPECT_TRUE(pRecommendationLimitLabel->isEnabled());

    pUseMixManDefaults->setChecked(false);

    EXPECT_TRUE(pTrackList->isEnabled());
    EXPECT_TRUE(pTrackDetail->isEnabled());
    EXPECT_TRUE(pTrackLookup->isEnabled());
    EXPECT_TRUE(pRecommendations->isEnabled());
    EXPECT_TRUE(pAudioDownload->isEnabled());
}

TEST_F(DlgPrefRestLibraryTest, InvalidEnabledSettingsBlockApply) {
    DlgPrefRestLibrary page(nullptr, config());

    auto* pEnabled = requireChild<QCheckBox>(&page, "checkBoxEnabled");
    auto* pBaseUrl = requireChild<QLineEdit>(&page, "lineEditBaseUrl");
    auto* pUseMixManDefaults = requireChild<QCheckBox>(&page, "checkBoxUseMixManDefaults");
    auto* pTrackList = requireChild<QLineEdit>(&page, "lineEditTrackListPath");
    auto* pTrackDetail = requireChild<QLineEdit>(&page, "lineEditTrackDetailPathTemplate");
    auto* pRecommendations = requireChild<QLineEdit>(&page, "lineEditRecommendationPathTemplate");
    auto* pAudioDownload = requireChild<QLineEdit>(&page, "lineEditAudioDownloadPathTemplate");

    pEnabled->setChecked(true);
    pBaseUrl->setText(QStringLiteral("relative-url"));
    pTrackList->setText(QStringLiteral("/tracks"));
    pTrackDetail->setText(QStringLiteral("/tracks/missing-placeholder"));
    pRecommendations->setText(QStringLiteral("/related/missing-placeholder"));
    pAudioDownload->setText(QStringLiteral("/audio/missing-placeholder"));

    EXPECT_FALSE(page.okayToClose());

    page.slotApply();

    EXPECT_FALSE(config()->getValue(restConfig::kEnabledKey, false));

    pBaseUrl->setText(QStringLiteral("https://example.com"));
    EXPECT_TRUE(page.okayToClose());

    pUseMixManDefaults->setChecked(false);
    EXPECT_FALSE(page.okayToClose());

    pTrackDetail->setText(QStringLiteral("/tracks/%1"));
    pRecommendations->setText(QStringLiteral("/tracks/%1/recommendations"));
    pAudioDownload->setText(QStringLiteral("/tracks/%1/audio"));

    EXPECT_TRUE(page.okayToClose());
}

TEST_F(DlgPrefRestLibraryTest, TestConnectionRequiresValidInputAndShowsDetails) {
    DlgPrefRestLibrary page(nullptr, config());

    auto* pEnabled = requireChild<QCheckBox>(&page, "checkBoxEnabled");
    auto* pBaseUrl = requireChild<QLineEdit>(&page, "lineEditBaseUrl");
    auto* pButton = requireChild<QPushButton>(&page, "pushButtonTestConnection");
    auto* pCancelButton =
            requireChild<QPushButton>(&page, "pushButtonCancelConnectionTest");
    auto* pCreateSession =
            requireChild<QCheckBox>(&page, "checkBoxTestConnectionCreateSession");
    auto* pResults = requireChild<QTreeWidget>(&page, "treeWidgetConnectionTestResults");
    auto* pDetails =
            requireChild<QPlainTextEdit>(&page, "plainTextEditConnectionTestDetails");

    EXPECT_FALSE(pCreateSession->isChecked());
    EXPECT_FALSE(pCancelButton->isEnabled());

    pEnabled->setChecked(true);
    pBaseUrl->setText(QStringLiteral("relative-url"));

    pButton->click();

    EXPECT_TRUE(pButton->isEnabled());
    EXPECT_FALSE(pCancelButton->isEnabled());
    ASSERT_EQ(pResults->topLevelItemCount(), 1);
    EXPECT_EQ(pResults->topLevelItem(0)->text(0), QStringLiteral("Configuration"));
    EXPECT_EQ(pResults->topLevelItem(0)->text(1), QStringLiteral("FAIL"));
    EXPECT_TRUE(pDetails->toPlainText().contains(QStringLiteral("absolute REST Library base URL")));

    pBaseUrl->setText(QStringLiteral("https://example.com"));

    ASSERT_EQ(pResults->topLevelItemCount(), 2);
    EXPECT_EQ(pResults->topLevelItem(1)->text(0), QStringLiteral("Settings"));
    EXPECT_EQ(pResults->topLevelItem(1)->text(1), QStringLiteral("STALE"));
}
