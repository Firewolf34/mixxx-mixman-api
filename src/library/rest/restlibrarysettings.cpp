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

namespace config {

QString defaultCacheDirectoryPath(const UserSettingsPointer& pConfig) {
    if (!pConfig) {
        return {};
    }
    return QDir(pConfig->getSettingsPath()).filePath(QStringLiteral("rest-library-cache"));
}

} // namespace config

RestLibrarySettings RestLibrarySettings::fromConfig(const UserSettingsPointer& pConfig) {
    RestLibrarySettings settings;
    if (!pConfig) {
        return settings;
    }

    settings.enabled = pConfig->getValue<bool>(config::kEnabledKey, config::kDefaultEnabled);
    settings.cacheEnabled = pConfig->getValue<bool>(
            config::kCacheEnabledKey,
            config::kDefaultCacheEnabled);
    settings.baseUrl = QUrl(pConfig->getValueString(config::kBaseUrlKey));
    QString keychainAccount = pConfig->getValueString(
            config::kBearerTokenKeychainAccountKey);
    if (keychainAccount.trimmed().isEmpty()) {
        keychainAccount = kDefaultKeychainAccount;
    }
    settings.bearerToken = readBearerTokenFromKeychain(keychainAccount);
    if (settings.bearerToken.isEmpty()) {
        settings.bearerToken = pConfig->getValueString(config::kLocalDevBearerTokenKey);
    }
    settings.trackListPath = pConfig->getValueString(config::kTrackListPathKey);
    settings.trackDetailPathTemplate = pConfig->getValueString(
            config::kTrackDetailPathTemplateKey);
    settings.trackLookupPathTemplate = pConfig->getValueString(
            config::kTrackLookupPathTemplateKey);
    settings.recommendationPathTemplate = pConfig->getValueString(
            config::kRecommendationPathTemplateKey);
    settings.audioDownloadPathTemplate = pConfig->getValueString(
            config::kAudioDownloadPathTemplateKey);
    settings.cacheDirectoryPath = pConfig->getValueString(config::kCacheDirectoryKey);
    if (settings.cacheDirectoryPath.trimmed().isEmpty()) {
        settings.cacheDirectoryPath = config::defaultCacheDirectoryPath(pConfig);
    }
    settings.pageSize = std::clamp(
            pConfig->getValue<int>(config::kPageSizeKey, config::kDefaultPageSize),
            config::kMinPageSize,
            config::kMaxPageSize);
    settings.recommendationLimit = std::clamp(
            pConfig->getValue<int>(
                    config::kRecommendationLimitKey,
                    config::kDefaultRecommendationLimit),
            config::kMinRecommendationLimit,
            config::kMaxRecommendationLimit);
    settings.cacheMaxMegabytes = std::clamp(
            pConfig->getValue<int>(
                    config::kCacheMaxMegabytesKey,
                    config::kDefaultCacheMaxMegabytes),
            config::kMinCacheMaxMegabytes,
            config::kMaxCacheMaxMegabytes);
    settings.cacheMaxAgeDays = std::clamp(
            pConfig->getValue<int>(
                    config::kCacheMaxAgeDaysKey,
                    config::kDefaultCacheMaxAgeDays),
            config::kMinCacheMaxAgeDays,
            config::kMaxCacheMaxAgeDays);
    settings.maxConcurrentDownloads = std::clamp(
            pConfig->getValue<int>(
                    config::kMaxConcurrentDownloadsKey,
                    config::kDefaultMaxConcurrentDownloads),
            config::kMinMaxConcurrentDownloads,
            config::kMaxMaxConcurrentDownloads);
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
