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
    config()->setValue(restConfig::kCacheMaxMegabytesKey, 2048);
    config()->setValue(restConfig::kCacheMaxAgeDaysKey, 45);
    config()->setValue(restConfig::kMaxConcurrentDownloadsKey, 4);

    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(config());

    EXPECT_TRUE(settings.enabled);
    EXPECT_FALSE(settings.cacheEnabled);
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
    EXPECT_EQ(settings.cacheMaxMegabytes, 2048);
    EXPECT_EQ(settings.cacheMaxAgeDays, 45);
    EXPECT_EQ(settings.maxConcurrentDownloads, 4);
}

TEST_F(RestLibrarySettingsTest, UsesDefaultsAndFallbackCacheDirectory) {
    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(config());

    EXPECT_EQ(settings.enabled, restConfig::kDefaultEnabled);
    EXPECT_EQ(settings.cacheEnabled, restConfig::kDefaultCacheEnabled);
    EXPECT_EQ(settings.pageSize, restConfig::kDefaultPageSize);
    EXPECT_EQ(settings.recommendationLimit, restConfig::kDefaultRecommendationLimit);
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
