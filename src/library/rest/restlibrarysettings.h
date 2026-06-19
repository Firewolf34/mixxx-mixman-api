#pragma once

#include <QString>
#include <QUrl>

#include "preferences/usersettings.h"

namespace mixxx::library::rest {

namespace config {

inline const ConfigKey kEnabledKey(QStringLiteral("[RestLibrary]"), QStringLiteral("Enabled"));
inline const ConfigKey kBaseUrlKey(QStringLiteral("[RestLibrary]"), QStringLiteral("BaseUrl"));
inline const ConfigKey kBearerTokenKeychainAccountKey(
        QStringLiteral("[RestLibrary]"),
        QStringLiteral("BearerTokenKeychainAccount"));
inline const ConfigKey kLocalDevBearerTokenKey(
        QStringLiteral("[RestLibrary]"),
        QStringLiteral("LocalDevBearerToken"));
inline const ConfigKey kTrackListPathKey(
        QStringLiteral("[RestLibrary]"),
        QStringLiteral("TrackListPath"));
inline const ConfigKey kTrackDetailPathTemplateKey(
        QStringLiteral("[RestLibrary]"),
        QStringLiteral("TrackDetailPathTemplate"));
inline const ConfigKey kTrackLookupPathTemplateKey(
        QStringLiteral("[RestLibrary]"),
        QStringLiteral("TrackLookupPathTemplate"));
inline const ConfigKey kRecommendationPathTemplateKey(
        QStringLiteral("[RestLibrary]"),
        QStringLiteral("RecommendationPathTemplate"));
inline const ConfigKey kAudioDownloadPathTemplateKey(
        QStringLiteral("[RestLibrary]"),
        QStringLiteral("AudioDownloadPathTemplate"));
inline const ConfigKey kCacheEnabledKey(
        QStringLiteral("[RestLibrary]"),
        QStringLiteral("CacheEnabled"));
inline const ConfigKey kCacheDirectoryKey(
        QStringLiteral("[RestLibrary]"),
        QStringLiteral("CacheDirectory"));
inline const ConfigKey kCacheMaxMegabytesKey(
        QStringLiteral("[RestLibrary]"),
        QStringLiteral("CacheMaxMegabytes"));
inline const ConfigKey kCacheMaxAgeDaysKey(
        QStringLiteral("[RestLibrary]"),
        QStringLiteral("CacheMaxAgeDays"));
inline const ConfigKey kMaxConcurrentDownloadsKey(
        QStringLiteral("[RestLibrary]"),
        QStringLiteral("MaxConcurrentDownloads"));
inline const ConfigKey kPageSizeKey(QStringLiteral("[RestLibrary]"), QStringLiteral("PageSize"));
inline const ConfigKey kRecommendationLimitKey(
        QStringLiteral("[RestLibrary]"),
        QStringLiteral("RecommendationLimit"));
inline const ConfigKey kUseMixManDefaultsKey(
        QStringLiteral("[RestLibrary]"),
        QStringLiteral("UseMixManDefaults"));
inline const ConfigKey kMixManPolicyPresetKey(
        QStringLiteral("[RestLibrary]"),
        QStringLiteral("MixManPolicyPreset"));
inline const ConfigKey kMixManTargetEnergyKey(
        QStringLiteral("[RestLibrary]"),
        QStringLiteral("MixManTargetEnergy"));
inline const ConfigKey kMixManTargetEnergyEnabledKey(
        QStringLiteral("[RestLibrary]"),
        QStringLiteral("MixManTargetEnergyEnabled"));
inline const ConfigKey kMixManTargetColorKey(
        QStringLiteral("[RestLibrary]"),
        QStringLiteral("MixManTargetColor"));
inline const ConfigKey kMixManTargetColorEnabledKey(
        QStringLiteral("[RestLibrary]"),
        QStringLiteral("MixManTargetColorEnabled"));
inline const ConfigKey kMixManTargetBpmKey(
        QStringLiteral("[RestLibrary]"),
        QStringLiteral("MixManTargetBpm"));
inline const ConfigKey kMixManTargetBpmEnabledKey(
        QStringLiteral("[RestLibrary]"),
        QStringLiteral("MixManTargetBpmEnabled"));
inline const ConfigKey kMixManAdminApprovedOnlyKey(
        QStringLiteral("[RestLibrary]"),
        QStringLiteral("MixManAdminApprovedOnly"));
inline const ConfigKey kMixManPathDepthKey(
        QStringLiteral("[RestLibrary]"),
        QStringLiteral("MixManPathDepth"));

constexpr bool kDefaultEnabled = false;
constexpr bool kDefaultCacheEnabled = true;
constexpr bool kDefaultUseMixManDefaults = true;
constexpr bool kDefaultMixManTargetEnergyEnabled = false;
constexpr bool kDefaultMixManTargetColorEnabled = false;
constexpr bool kDefaultMixManTargetBpmEnabled = false;
constexpr bool kDefaultMixManAdminApprovedOnly = true;
constexpr int kDefaultPageSize = 50;
constexpr int kMinPageSize = 1;
constexpr int kMaxPageSize = 200;
constexpr int kDefaultRecommendationLimit = 10;
constexpr int kMinRecommendationLimit = 1;
constexpr int kMaxRecommendationLimit = 20;
constexpr int kDefaultMixManPathDepth = 5;
constexpr int kMinMixManPathDepth = 1;
constexpr int kMaxMixManPathDepth = 20;
constexpr int kDefaultMixManTargetEnergy = 3;
constexpr int kMinMixManTargetEnergy = 0;
constexpr int kMaxMixManTargetEnergy = 5;
constexpr int kDefaultMixManTargetBpm = 124;
constexpr int kMinMixManTargetBpm = 40;
constexpr int kMaxMixManTargetBpm = 240;
constexpr int kDefaultCacheMaxMegabytes = 1024;
constexpr int kMinCacheMaxMegabytes = 64;
constexpr int kMaxCacheMaxMegabytes = 1024 * 100;
constexpr int kDefaultCacheMaxAgeDays = 30;
constexpr int kMinCacheMaxAgeDays = 1;
constexpr int kMaxCacheMaxAgeDays = 365;
constexpr int kDefaultMaxConcurrentDownloads = 2;
constexpr int kMinMaxConcurrentDownloads = 1;
constexpr int kMaxMaxConcurrentDownloads = 8;

QString defaultCacheDirectoryPath(const UserSettingsPointer& pConfig);
QString mixManTrackListPath();
QString mixManTrackDetailPathTemplate();
QString mixManTrackLookupPathTemplate();
QString mixManRecommendationPathTemplate();
QString mixManAudioDownloadPathTemplate();
QString mixManHealthPath();
QString mixManConfigPath();
QString mixManIndexStatusPath();
QString mixManPolicyPresetsPath();
QString mixManSessionsPath();
QString mixManSessionSnapshotPath(const QString& sessionId);
QString mixManSessionIntentPath(const QString& sessionId);
QString mixManSessionHeartbeatPath(const QString& sessionId);
QString mixManSessionPlaybackPath(const QString& sessionId);
QString mixManSessionControlClaimPath(const QString& sessionId);
QString mixManSessionCandidateSelectPath(const QString& sessionId, const QString& trackId);

} // namespace config

class RestLibrarySettings final {
  public:
    static RestLibrarySettings fromConfig(const UserSettingsPointer& pConfig);

    bool isConfigured() const;
    bool enabled = config::kDefaultEnabled;
    bool cacheEnabled = config::kDefaultCacheEnabled;
    bool useMixManDefaults = config::kDefaultUseMixManDefaults;
    bool mixManTargetEnergyEnabled = config::kDefaultMixManTargetEnergyEnabled;
    bool mixManTargetColorEnabled = config::kDefaultMixManTargetColorEnabled;
    bool mixManTargetBpmEnabled = config::kDefaultMixManTargetBpmEnabled;
    bool mixManAdminApprovedOnly = config::kDefaultMixManAdminApprovedOnly;
    QUrl baseUrl;
    QString bearerToken;
    QString trackListPath;
    QString trackDetailPathTemplate;
    QString trackLookupPathTemplate;
    QString recommendationPathTemplate;
    QString audioDownloadPathTemplate;
    QString cacheDirectoryPath;
    QString mixManPolicyPreset;
    QString mixManTargetColor;
    int pageSize = config::kDefaultPageSize;
    int recommendationLimit = config::kDefaultRecommendationLimit;
    int mixManPathDepth = config::kDefaultMixManPathDepth;
    int mixManTargetEnergy = config::kDefaultMixManTargetEnergy;
    int mixManTargetBpm = config::kDefaultMixManTargetBpm;
    int cacheMaxMegabytes = config::kDefaultCacheMaxMegabytes;
    int cacheMaxAgeDays = config::kDefaultCacheMaxAgeDays;
    int maxConcurrentDownloads = config::kDefaultMaxConcurrentDownloads;

    bool hasAudioDownloadConfigured() const;
    bool hasTrackLookupConfigured() const;
    bool hasRecommendationsConfigured() const;
    double mixManTargetEnergyNormalized() const;
};

} // namespace mixxx::library::rest
