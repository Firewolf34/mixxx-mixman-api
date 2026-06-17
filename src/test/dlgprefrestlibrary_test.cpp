#include <gtest/gtest.h>

#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

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
    config()->setValue(restConfig::kTrackListPathKey, QStringLiteral("/old-tracks"));
    config()->setValue(restConfig::kTrackDetailPathTemplateKey, QStringLiteral("/old/%1"));
    config()->setValue(restConfig::kTrackLookupPathTemplateKey, QStringLiteral("/lookup"));
    config()->setValue(restConfig::kRecommendationPathTemplateKey, QStringLiteral("/old/%1/related"));
    config()->setValue(restConfig::kRecommendationLimitKey, 6);
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
    EXPECT_EQ(config()->getValueString(restConfig::kCacheDirectoryKey), QStringLiteral("D:/rest-cache"));
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

TEST_F(DlgPrefRestLibraryTest, InvalidEnabledSettingsBlockApply) {
    DlgPrefRestLibrary page(nullptr, config());

    auto* pEnabled = requireChild<QCheckBox>(&page, "checkBoxEnabled");
    auto* pBaseUrl = requireChild<QLineEdit>(&page, "lineEditBaseUrl");
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
    pTrackDetail->setText(QStringLiteral("/tracks/%1"));
    pRecommendations->setText(QStringLiteral("/tracks/%1/recommendations"));
    pAudioDownload->setText(QStringLiteral("/tracks/%1/audio"));

    EXPECT_TRUE(page.okayToClose());
}
