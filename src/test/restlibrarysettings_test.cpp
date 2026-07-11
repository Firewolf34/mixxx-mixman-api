#include <gtest/gtest.h>

#include <QUuid>

#include "library/rest/restlibrarysettings.h"
#include "test/mixxxtest.h"

namespace {

namespace restConfig = mixxx::library::rest::config;
using mixxx::library::rest::RestLibrarySettings;

QString uniqueKeychainAccount() {
    return QStringLiteral("mixxx-test-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
}

} // namespace

class RestLibrarySettingsTest : public MixxxTest {
};

TEST_F(RestLibrarySettingsTest, ReadsConfiguredValues) {
    config()->setValue(restConfig::kEnabledKey, true);
    config()->setValue(restConfig::kBaseUrlKey, QStringLiteral("https://example.com/api"));
    config()->setValue(restConfig::kBearerTokenKeychainAccountKey, uniqueKeychainAccount());
    config()->setValue(restConfig::kLocalDevBearerTokenKey, QStringLiteral("test-token"));
    config()->setValue(restConfig::kUseMixManDefaultsKey, false);
    config()->setValue(restConfig::kTrackListPathKey, QStringLiteral("/tracks"));
    config()->setValue(restConfig::kTrackDetailPathTemplateKey, QStringLiteral("/tracks/%1"));
    config()->setValue(restConfig::kTrackLookupPathTemplateKey, QStringLiteral("/lookup"));
    config()->setValue(
            restConfig::kRecommendationPathTemplateKey,
            QStringLiteral("/tracks/%1/recommendations"));
    config()->setValue(restConfig::kAudioDownloadPathTemplateKey, QStringLiteral("/audio/%1"));
    config()->setValue(restConfig::kCacheEnabledKey, false);
    config()->setValue(restConfig::kCacheDirectoryKey, QStringLiteral("C:/mixxx/rest-cache"));
    config()->setValue(restConfig::kPageSizeKey, 25);
    config()->setValue(restConfig::kRecommendationLimitKey, 7);
    config()->setValue(restConfig::kMixManPathDepthKey, 8);
    config()->setValue(restConfig::kMixManPolicyPresetKey, QStringLiteral("explore"));
    config()->setValue(restConfig::kMixManTargetEnergyEnabledKey, true);
    config()->setValue(restConfig::kMixManTargetEnergyKey, 4);
    config()->setValue(restConfig::kMixManTargetColorEnabledKey, true);
    config()->setValue(restConfig::kMixManTargetColorKey, QStringLiteral("#ff6600"));
    config()->setValue(restConfig::kMixManTargetBpmEnabledKey, true);
    config()->setValue(restConfig::kMixManTargetBpmKey, 132);
    config()->setValue(restConfig::kMixManAdminApprovedOnlyKey, false);
    config()->setValue(restConfig::kCacheMaxMegabytesKey, 2048);
    config()->setValue(restConfig::kCacheMaxAgeDaysKey, 45);
    config()->setValue(restConfig::kMaxConcurrentDownloadsKey, 4);

    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(config());

    EXPECT_TRUE(settings.enabled);
    EXPECT_FALSE(settings.cacheEnabled);
    EXPECT_FALSE(settings.useMixManDefaults);
    EXPECT_EQ(settings.baseUrl, QUrl(QStringLiteral("https://example.com/api")));
    EXPECT_EQ(settings.bearerToken, QStringLiteral("test-token"));
    EXPECT_EQ(settings.trackListPath, QStringLiteral("/tracks"));
    EXPECT_EQ(settings.trackDetailPathTemplate, QStringLiteral("/tracks/%1"));
    EXPECT_EQ(settings.trackLookupPathTemplate, QStringLiteral("/lookup"));
    EXPECT_EQ(settings.recommendationPathTemplate, QStringLiteral("/tracks/%1/recommendations"));
    EXPECT_EQ(settings.audioDownloadPathTemplate, QStringLiteral("/audio/%1"));
    EXPECT_EQ(settings.cacheDirectoryPath, QStringLiteral("C:/mixxx/rest-cache"));
    EXPECT_EQ(settings.pageSize, 25);
    EXPECT_EQ(settings.recommendationLimit, 7);
    EXPECT_EQ(settings.mixManPathDepth, 8);
    EXPECT_EQ(settings.mixManPolicyPreset, QStringLiteral("explore"));
    EXPECT_TRUE(settings.mixManTargetEnergyEnabled);
    EXPECT_EQ(settings.mixManTargetEnergy, 4);
    EXPECT_DOUBLE_EQ(settings.mixManTargetEnergyNormalized(), 0.8);
    EXPECT_TRUE(settings.mixManTargetColorEnabled);
    EXPECT_EQ(settings.mixManTargetColor, QStringLiteral("#ff6600"));
    EXPECT_TRUE(settings.mixManTargetBpmEnabled);
    EXPECT_EQ(settings.mixManTargetBpm, 132);
    EXPECT_FALSE(settings.mixManAdminApprovedOnly);
    EXPECT_EQ(settings.cacheMaxMegabytes, 2048);
    EXPECT_EQ(settings.cacheMaxAgeDays, 45);
    EXPECT_EQ(settings.maxConcurrentDownloads, 4);
}

TEST_F(RestLibrarySettingsTest, UsesDefaultsAndFallbackCacheDirectory) {
    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(config());

    EXPECT_EQ(settings.enabled, restConfig::kDefaultEnabled);
    EXPECT_EQ(settings.cacheEnabled, restConfig::kDefaultCacheEnabled);
    EXPECT_EQ(settings.useMixManDefaults, restConfig::kDefaultUseMixManDefaults);
    EXPECT_EQ(settings.trackListPath, restConfig::mixManTrackListPath());
    EXPECT_EQ(settings.trackDetailPathTemplate, restConfig::mixManTrackDetailPathTemplate());
    EXPECT_EQ(settings.trackLookupPathTemplate, restConfig::mixManTrackLookupPathTemplate());
    EXPECT_EQ(settings.recommendationPathTemplate, restConfig::mixManRecommendationPathTemplate());
    EXPECT_EQ(settings.audioDownloadPathTemplate, restConfig::mixManAudioDownloadPathTemplate());
    EXPECT_EQ(settings.pageSize, restConfig::kDefaultPageSize);
    EXPECT_EQ(settings.recommendationLimit, restConfig::kDefaultRecommendationLimit);
    EXPECT_EQ(settings.mixManPathDepth, restConfig::kDefaultMixManPathDepth);
    EXPECT_EQ(settings.mixManPolicyPreset, QStringLiteral("dj_assist"));
    EXPECT_EQ(settings.mixManTargetEnergyEnabled, restConfig::kDefaultMixManTargetEnergyEnabled);
    EXPECT_EQ(settings.mixManTargetEnergy, restConfig::kDefaultMixManTargetEnergy);
    EXPECT_EQ(settings.mixManTargetColorEnabled, restConfig::kDefaultMixManTargetColorEnabled);
    EXPECT_EQ(settings.mixManTargetBpmEnabled, restConfig::kDefaultMixManTargetBpmEnabled);
    EXPECT_EQ(settings.mixManTargetBpm, restConfig::kDefaultMixManTargetBpm);
    EXPECT_EQ(settings.mixManAdminApprovedOnly, restConfig::kDefaultMixManAdminApprovedOnly);
    EXPECT_EQ(settings.cacheMaxMegabytes, restConfig::kDefaultCacheMaxMegabytes);
    EXPECT_EQ(settings.cacheMaxAgeDays, restConfig::kDefaultCacheMaxAgeDays);
    EXPECT_EQ(settings.maxConcurrentDownloads, restConfig::kDefaultMaxConcurrentDownloads);
    EXPECT_EQ(settings.cacheDirectoryPath, restConfig::defaultCacheDirectoryPath(config()));
}

TEST_F(RestLibrarySettingsTest, ClampsNumericValues) {
    config()->setValue(restConfig::kPageSizeKey, restConfig::kMaxPageSize + 1);
    config()->setValue(
            restConfig::kRecommendationLimitKey,
            restConfig::kMinRecommendationLimit - 1);
    config()->setValue(restConfig::kMixManPathDepthKey, restConfig::kMaxMixManPathDepth + 1);
    config()->setValue(
            restConfig::kMixManTargetEnergyKey,
            restConfig::kMinMixManTargetEnergy - 1);
    config()->setValue(
            restConfig::kMixManTargetBpmKey,
            restConfig::kMaxMixManTargetBpm + 1);
    config()->setValue(
            restConfig::kCacheMaxMegabytesKey,
            restConfig::kMaxCacheMaxMegabytes + 1);
    config()->setValue(restConfig::kCacheMaxAgeDaysKey, restConfig::kMinCacheMaxAgeDays - 1);
    config()->setValue(
            restConfig::kMaxConcurrentDownloadsKey,
            restConfig::kMaxMaxConcurrentDownloads + 1);

    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(config());

    EXPECT_EQ(settings.pageSize, restConfig::kMaxPageSize);
    EXPECT_EQ(settings.recommendationLimit, restConfig::kMinRecommendationLimit);
    EXPECT_EQ(settings.mixManPathDepth, restConfig::kMaxMixManPathDepth);
    EXPECT_EQ(settings.mixManTargetEnergy, restConfig::kMinMixManTargetEnergy);
    EXPECT_EQ(settings.mixManTargetBpm, restConfig::kMaxMixManTargetBpm);
    EXPECT_EQ(settings.cacheMaxMegabytes, restConfig::kMaxCacheMaxMegabytes);
    EXPECT_EQ(settings.cacheMaxAgeDays, restConfig::kMinCacheMaxAgeDays);
    EXPECT_EQ(settings.maxConcurrentDownloads, restConfig::kMaxMaxConcurrentDownloads);
}

TEST_F(RestLibrarySettingsTest, UsesLocalDevBearerTokenFallback) {
    config()->setValue(restConfig::kBearerTokenKeychainAccountKey, uniqueKeychainAccount());
    config()->setValue(restConfig::kLocalDevBearerTokenKey, QStringLiteral("fallback-token"));

    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(config());

    EXPECT_EQ(settings.bearerToken, QStringLiteral("fallback-token"));
}

TEST_F(RestLibrarySettingsTest, UrlWithRestPathPreservesBasePath) {
    EXPECT_EQ(
            restConfig::urlWithRestPath(
                    QUrl(QStringLiteral("https://example.com/api")),
                    QStringLiteral("/tracks"))
                    .toString(),
            QStringLiteral("https://example.com/api/tracks"));
    EXPECT_EQ(
            restConfig::urlWithRestPath(
                    QUrl(QStringLiteral("https://example.com/api/")),
                    QStringLiteral("tracks?limit=1"))
                    .toString(),
            QStringLiteral("https://example.com/api/tracks?limit=1"));
    EXPECT_EQ(
            restConfig::urlWithRestPath(
                    QUrl(QStringLiteral("https://example.com")),
                    QStringLiteral("/tracks"))
                    .toString(),
            QStringLiteral("https://example.com/tracks"));
    EXPECT_EQ(
            restConfig::urlWithRestPath(
                    QUrl(QStringLiteral("https://example.com/api")),
                    QStringLiteral("https://cdn.example.test/audio/1"))
                    .toString(),
            QStringLiteral("https://cdn.example.test/audio/1"));
}
