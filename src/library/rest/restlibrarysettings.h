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

constexpr bool kDefaultEnabled = false;
constexpr bool kDefaultCacheEnabled = true;
constexpr int kDefaultPageSize = 50;
constexpr int kMinPageSize = 1;
constexpr int kMaxPageSize = 200;
constexpr int kDefaultRecommendationLimit = 5;
constexpr int kMinRecommendationLimit = 1;
constexpr int kMaxRecommendationLimit = 20;
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

} // namespace config

class RestLibrarySettings final {
  public:
    static RestLibrarySettings fromConfig(const UserSettingsPointer& pConfig);

    bool isConfigured() const;
    bool enabled = config::kDefaultEnabled;
    bool cacheEnabled = config::kDefaultCacheEnabled;
    QUrl baseUrl;
    QString bearerToken;
    QString trackListPath;
    QString trackDetailPathTemplate;
    QString trackLookupPathTemplate;
    QString recommendationPathTemplate;
    QString audioDownloadPathTemplate;
    QString cacheDirectoryPath;
    int pageSize = config::kDefaultPageSize;
    int recommendationLimit = config::kDefaultRecommendationLimit;
    int cacheMaxMegabytes = config::kDefaultCacheMaxMegabytes;
    int cacheMaxAgeDays = config::kDefaultCacheMaxAgeDays;
    int maxConcurrentDownloads = config::kDefaultMaxConcurrentDownloads;

    bool hasAudioDownloadConfigured() const;
    bool hasTrackLookupConfigured() const;
    bool hasRecommendationsConfigured() const;
};

} // namespace mixxx::library::rest
