#include "library/rest/restlibrarysettings.h"

#include <algorithm>

#include <QDir>
#include <QEventLoop>

#ifdef __QTKEYCHAIN__
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <qt6keychain/keychain.h>
#else
#include <qt5keychain/keychain.h>
#endif
#endif // __QTKEYCHAIN__

#include "util/logger.h"

namespace mixxx::library::rest {

namespace {

const Logger kLogger("RestLibrarySettings");

const ConfigKey kEnabledKey("[RestLibrary]", "Enabled");
const ConfigKey kBaseUrlKey("[RestLibrary]", "BaseUrl");
const ConfigKey kBearerTokenKeychainAccountKey("[RestLibrary]", "BearerTokenKeychainAccount");
const ConfigKey kLocalDevBearerTokenKey("[RestLibrary]", "LocalDevBearerToken");
const ConfigKey kTrackListPathKey("[RestLibrary]", "TrackListPath");
const ConfigKey kTrackDetailPathTemplateKey("[RestLibrary]", "TrackDetailPathTemplate");
const ConfigKey kTrackLookupPathTemplateKey("[RestLibrary]", "TrackLookupPathTemplate");
const ConfigKey kRecommendationPathTemplateKey("[RestLibrary]", "RecommendationPathTemplate");
const ConfigKey kAudioDownloadPathTemplateKey("[RestLibrary]", "AudioDownloadPathTemplate");
const ConfigKey kCacheEnabledKey("[RestLibrary]", "CacheEnabled");
const ConfigKey kCacheDirectoryKey("[RestLibrary]", "CacheDirectory");
const ConfigKey kCacheMaxMegabytesKey("[RestLibrary]", "CacheMaxMegabytes");
const ConfigKey kCacheMaxAgeDaysKey("[RestLibrary]", "CacheMaxAgeDays");
const ConfigKey kMaxConcurrentDownloadsKey("[RestLibrary]", "MaxConcurrentDownloads");
const ConfigKey kPageSizeKey("[RestLibrary]", "PageSize");
const ConfigKey kRecommendationLimitKey("[RestLibrary]", "RecommendationLimit");

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

const QString kDefaultKeychainAccount = QStringLiteral("default");

QString readBearerTokenFromKeychain(const QString& account) {
#ifdef __QTKEYCHAIN__
    QKeychain::ReadPasswordJob readJob(QStringLiteral("Mixxx REST Library"));
    readJob.setAutoDelete(false);
    readJob.setKey(account);

    QEventLoop loop;
    readJob.connect(&readJob, &QKeychain::ReadPasswordJob::finished, &loop, &QEventLoop::quit);
    readJob.start();
    loop.exec();

    if (readJob.error() == QKeychain::Error::NoError) {
        return readJob.textData();
    }
    kLogger.debug() << "REST library bearer token was not available from keychain";
#else
    Q_UNUSED(account);
#endif
    return {};
}

} // namespace

RestLibrarySettings RestLibrarySettings::fromConfig(const UserSettingsPointer& pConfig) {
    RestLibrarySettings settings;
    if (!pConfig) {
        return settings;
    }

    settings.enabled = pConfig->getValue<bool>(kEnabledKey, false);
    settings.cacheEnabled = pConfig->getValue<bool>(kCacheEnabledKey, true);
    settings.baseUrl = QUrl(pConfig->getValueString(kBaseUrlKey));
    QString keychainAccount = pConfig->getValueString(kBearerTokenKeychainAccountKey);
    if (keychainAccount.trimmed().isEmpty()) {
        keychainAccount = kDefaultKeychainAccount;
    }
    settings.bearerToken = readBearerTokenFromKeychain(keychainAccount);
    if (settings.bearerToken.isEmpty()) {
        settings.bearerToken = pConfig->getValueString(kLocalDevBearerTokenKey);
    }
    settings.trackListPath = pConfig->getValueString(kTrackListPathKey);
    settings.trackDetailPathTemplate = pConfig->getValueString(kTrackDetailPathTemplateKey);
    settings.trackLookupPathTemplate = pConfig->getValueString(kTrackLookupPathTemplateKey);
    settings.recommendationPathTemplate = pConfig->getValueString(kRecommendationPathTemplateKey);
    settings.audioDownloadPathTemplate = pConfig->getValueString(kAudioDownloadPathTemplateKey);
    settings.cacheDirectoryPath = pConfig->getValueString(kCacheDirectoryKey);
    if (settings.cacheDirectoryPath.trimmed().isEmpty()) {
        settings.cacheDirectoryPath = QDir(pConfig->getSettingsPath())
                                              .filePath(QStringLiteral("rest-library-cache"));
    }
    settings.pageSize = std::clamp(
            pConfig->getValue<int>(kPageSizeKey, kDefaultPageSize),
            kMinPageSize,
            kMaxPageSize);
    settings.recommendationLimit = std::clamp(
            pConfig->getValue<int>(kRecommendationLimitKey, kDefaultRecommendationLimit),
            kMinRecommendationLimit,
            kMaxRecommendationLimit);
    settings.cacheMaxMegabytes = std::clamp(
            pConfig->getValue<int>(
                    kCacheMaxMegabytesKey,
                    kDefaultCacheMaxMegabytes),
            kMinCacheMaxMegabytes,
            kMaxCacheMaxMegabytes);
    settings.cacheMaxAgeDays = std::clamp(
            pConfig->getValue<int>(kCacheMaxAgeDaysKey, kDefaultCacheMaxAgeDays),
            kMinCacheMaxAgeDays,
            kMaxCacheMaxAgeDays);
    settings.maxConcurrentDownloads = std::clamp(
            pConfig->getValue<int>(
                    kMaxConcurrentDownloadsKey,
                    kDefaultMaxConcurrentDownloads),
            kMinMaxConcurrentDownloads,
            kMaxMaxConcurrentDownloads);
    return settings;
}

bool RestLibrarySettings::isConfigured() const {
    return enabled &&
            baseUrl.isValid() &&
            !baseUrl.isEmpty() &&
            !trackListPath.trimmed().isEmpty();
}

bool RestLibrarySettings::hasAudioDownloadConfigured() const {
    return isConfigured() &&
            cacheEnabled &&
            !cacheDirectoryPath.trimmed().isEmpty() &&
            audioDownloadPathTemplate.contains(QStringLiteral("%1"));
}

bool RestLibrarySettings::hasTrackLookupConfigured() const {
    return isConfigured() &&
            !trackLookupPathTemplate.trimmed().isEmpty();
}

bool RestLibrarySettings::hasRecommendationsConfigured() const {
    return isConfigured() &&
            recommendationPathTemplate.contains(QStringLiteral("%1"));
}

} // namespace mixxx::library::rest
