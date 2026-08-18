#include <gtest/gtest.h>

#include <limits>

#include <QHash>

#include "library/rest/restlibrarysettings.h"
#include "test/mixxxtest.h"

namespace {

namespace restConfig = mixxx::library::rest::config;
using mixxx::library::rest::RestLibrarySettings;
using mixxx::library::rest::RestLibraryCredentialStore;

class FakeCredentialStore final : public RestLibraryCredentialStore {
  public:
    QString read(const QString& account) override {
        return secrets.value(account);
    }
    bool write(const QString& account, const QString& secret) override {
        if (failWrites) {
            return false;
        }
        secrets.insert(account, secret);
        return true;
    }
    bool remove(const QString& account) override {
        secrets.remove(account);
        return true;
    }

    QHash<QString, QString> secrets;
    bool failWrites = false;
};

} // namespace

class RestLibrarySettingsTest : public MixxxTest {
  protected:
    RestLibrarySettings readSettings() {
        return RestLibrarySettings::fromConfig(config(), &credentialStore);
    }

    FakeCredentialStore credentialStore;
};

TEST_F(RestLibrarySettingsTest, ReadsConfiguredValues) {
    config()->setValue(restConfig::kEnabledKey, true);
    config()->setValue(restConfig::kBaseUrlKey, QStringLiteral("https://example.com/api"));
    config()->setValue(restConfig::kBearerTokenKeychainAccountKey, QStringLiteral("test-account"));
    config()->setValue(restConfig::kLocalDevBearerTokenKey, QStringLiteral("test-token"));
    config()->setValue(restConfig::kUseMixManDefaultsKey, false);
    config()->setValue(restConfig::kMixManSessionIdKey, QStringLiteral("stable-room"));
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
    config()->setValue(restConfig::kMaxCatalogPagesKey, 600);
    config()->setValue(restConfig::kMaxCatalogTracksKey, 25000);
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

    const RestLibrarySettings settings = readSettings();

    EXPECT_TRUE(settings.enabled);
    EXPECT_FALSE(settings.cacheEnabled);
    EXPECT_FALSE(settings.useMixManDefaults);
    EXPECT_EQ(settings.mixManSessionId, QStringLiteral("stable-room"));
    EXPECT_EQ(settings.baseUrl, QUrl(QStringLiteral("https://example.com/api")));
    EXPECT_EQ(settings.bearerToken, QStringLiteral("test-token"));
    EXPECT_EQ(settings.trackListPath, QStringLiteral("/tracks"));
    EXPECT_EQ(settings.trackDetailPathTemplate, QStringLiteral("/tracks/%1"));
    EXPECT_EQ(settings.trackLookupPathTemplate, QStringLiteral("/lookup"));
    EXPECT_EQ(settings.recommendationPathTemplate, QStringLiteral("/tracks/%1/recommendations"));
    EXPECT_EQ(settings.audioDownloadPathTemplate, QStringLiteral("/audio/%1"));
    EXPECT_EQ(settings.cacheDirectoryPath, QStringLiteral("C:/mixxx/rest-cache"));
    EXPECT_EQ(settings.pageSize, 25);
    EXPECT_EQ(settings.maxCatalogPages, 600);
    EXPECT_EQ(settings.maxCatalogTracks, 25000);
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
    const RestLibrarySettings settings = readSettings();

    EXPECT_EQ(settings.enabled, restConfig::kDefaultEnabled);
    EXPECT_EQ(settings.cacheEnabled, restConfig::kDefaultCacheEnabled);
    EXPECT_EQ(settings.useMixManDefaults, restConfig::kDefaultUseMixManDefaults);
    EXPECT_TRUE(settings.mixManSessionId.isEmpty());
    EXPECT_EQ(settings.trackListPath, restConfig::mixManTrackListPath());
    EXPECT_EQ(settings.trackDetailPathTemplate, restConfig::mixManTrackDetailPathTemplate());
    EXPECT_EQ(settings.trackLookupPathTemplate, restConfig::mixManTrackLookupPathTemplate());
    EXPECT_EQ(settings.recommendationPathTemplate, restConfig::mixManRecommendationPathTemplate());
    EXPECT_EQ(settings.audioDownloadPathTemplate, restConfig::mixManAudioDownloadPathTemplate());
    EXPECT_EQ(settings.pageSize, restConfig::kDefaultPageSize);
    EXPECT_EQ(settings.maxCatalogPages, restConfig::kDefaultMaxCatalogPages);
    EXPECT_EQ(settings.maxCatalogTracks, restConfig::kDefaultMaxCatalogTracks);
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

TEST_F(RestLibrarySettingsTest, GeneratesValidStableMixManSessionId) {
    const QString id = mixxx::library::rest::generateMixManSessionId();
    EXPECT_TRUE(id.startsWith(QStringLiteral("mixxx-")));
    EXPECT_LE(id.size(), 80);
    EXPECT_FALSE(id.contains(QLatin1Char('{')));
}

TEST_F(RestLibrarySettingsTest, ClampsNumericValues) {
    config()->setValue(restConfig::kPageSizeKey, restConfig::kMaxPageSize + 1);
    config()->setValue(restConfig::kMaxCatalogPagesKey, restConfig::kMinCatalogLimit - 1);
    config()->setValue(restConfig::kMaxCatalogTracksKey, std::numeric_limits<int>::max());
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

    const RestLibrarySettings settings = readSettings();

    EXPECT_EQ(settings.pageSize, restConfig::kMaxPageSize);
    EXPECT_EQ(settings.maxCatalogPages, restConfig::kMinCatalogLimit);
    EXPECT_EQ(settings.maxCatalogTracks, std::numeric_limits<int>::max());
    EXPECT_EQ(settings.recommendationLimit, restConfig::kMinRecommendationLimit);
    EXPECT_EQ(settings.mixManPathDepth, restConfig::kMaxMixManPathDepth);
    EXPECT_EQ(settings.mixManTargetEnergy, restConfig::kMinMixManTargetEnergy);
    EXPECT_EQ(settings.mixManTargetBpm, restConfig::kMaxMixManTargetBpm);
    EXPECT_EQ(settings.cacheMaxMegabytes, restConfig::kMaxCacheMaxMegabytes);
    EXPECT_EQ(settings.cacheMaxAgeDays, restConfig::kMinCacheMaxAgeDays);
    EXPECT_EQ(settings.maxConcurrentDownloads, restConfig::kMaxMaxConcurrentDownloads);
}

TEST_F(RestLibrarySettingsTest, MigratesAndClearsPlaintextBearerToken) {
    config()->setValue(restConfig::kBearerTokenKeychainAccountKey, QStringLiteral("migration"));
    config()->setValue(restConfig::kLocalDevBearerTokenKey, QStringLiteral("fallback-token"));

    const RestLibrarySettings settings = readSettings();

    EXPECT_EQ(settings.bearerToken, QStringLiteral("fallback-token"));
    EXPECT_EQ(credentialStore.secrets.value(settings.bearerTokenKeychainAccount),
            QStringLiteral("fallback-token"));
    EXPECT_EQ(config()->getValueString(restConfig::kBearerTokenKeychainAccountKey),
            settings.bearerTokenKeychainAccount);
    EXPECT_FALSE(config()->exists(restConfig::kLocalDevBearerTokenKey));
}

TEST_F(RestLibrarySettingsTest, FailedPlaintextMigrationFailsClosed) {
    credentialStore.failWrites = true;
    config()->setValue(restConfig::kBearerTokenKeychainAccountKey, QStringLiteral("migration"));
    config()->setValue(restConfig::kLocalDevBearerTokenKey, QStringLiteral("discard-me"));

    const RestLibrarySettings settings = readSettings();

    EXPECT_TRUE(settings.bearerToken.isEmpty());
    EXPECT_FALSE(config()->exists(restConfig::kLocalDevBearerTokenKey));
}

TEST_F(RestLibrarySettingsTest, IgnoresBearerAccountFromAnotherServer) {
    config()->setValue(restConfig::kBaseUrlKey, QStringLiteral("https://new.example.test"));
    config()->setValue(
            restConfig::kBearerTokenKeychainAccountKey,
            QStringLiteral("bearer-for-old-server"));
    credentialStore.secrets.insert(
            QStringLiteral("bearer-for-old-server"),
            QStringLiteral("must-not-cross-origins"));

    const RestLibrarySettings settings = readSettings();

    EXPECT_TRUE(settings.bearerToken.isEmpty());
    EXPECT_EQ(settings.bearerTokenKeychainAccount,
            mixxx::library::rest::bearerTokenAccountForUrl(settings.baseUrl));
    EXPECT_EQ(config()->getValueString(restConfig::kBearerTokenKeychainAccountKey),
            settings.bearerTokenKeychainAccount);
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
                    .isValid(),
            false);
}

TEST_F(RestLibrarySettingsTest, BearerTransportRequiresHttpsOrLoopback) {
    RestLibrarySettings settings;
    settings.bearerToken = QStringLiteral("secret");
    settings.baseUrl = QUrl(QStringLiteral("http://mixman.lan/api"));
    EXPECT_FALSE(settings.hasAllowedBearerTransport());

    settings.baseUrl = QUrl(QStringLiteral("http://127.0.0.1:8000/api"));
    EXPECT_TRUE(settings.hasAllowedBearerTransport());

    settings.baseUrl = QUrl(QStringLiteral("https://mixman.example/api"));
    EXPECT_TRUE(settings.hasAllowedBearerTransport());
    EXPECT_TRUE(settings.maySendBearerTokenTo(
            QUrl(QStringLiteral("https://mixman.example/tracks"))));
    EXPECT_FALSE(settings.maySendBearerTokenTo(
            QUrl(QStringLiteral("https://cdn.example/tracks"))));
}

TEST_F(RestLibrarySettingsTest, TokenlessTrustedLanHttpRemainsAllowed) {
    RestLibrarySettings settings;
    settings.enabled = true;
    settings.baseUrl = QUrl(QStringLiteral("http://mixman.lan/api"));
    settings.trackListPath = QStringLiteral("/tracks");

    EXPECT_TRUE(settings.isConfigured());
    EXPECT_FALSE(settings.maySendBearerTokenTo(
            QUrl(QStringLiteral("http://mixman.lan/api/tracks"))));
}

TEST_F(RestLibrarySettingsTest, CredentialContextNamespaceIsStableAndNonSecret) {
    RestLibrarySettings settings;
    EXPECT_EQ(settings.credentialContextNamespace(), QStringLiteral("tokenless"));

    settings.bearerToken = QStringLiteral("account-a-secret-token");
    const QString accountA = settings.credentialContextNamespace();
    EXPECT_EQ(accountA, settings.credentialContextNamespace());
    EXPECT_TRUE(accountA.startsWith(QStringLiteral("bearer-sha256:")));
    EXPECT_FALSE(accountA.contains(settings.bearerToken));

    settings.bearerToken = QStringLiteral("account-b-secret-token");
    EXPECT_NE(accountA, settings.credentialContextNamespace());

    settings.bearerToken.clear();
    EXPECT_EQ(settings.credentialContextNamespace(), QStringLiteral("tokenless"));
}
