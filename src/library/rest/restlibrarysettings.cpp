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

QString mixManTrackListPath() {
    return QStringLiteral("/tracks");
}

QString mixManTrackDetailPathTemplate() {
    return QStringLiteral("/tracks/%1");
}

QString mixManTrackLookupPathTemplate() {
    return QStringLiteral("/tracks?artists=%artist&titles=%title");
}

QString mixManRecommendationPathTemplate() {
    return QStringLiteral("/recommendations/policy-console/path/%1");
}

QString mixManAudioDownloadPathTemplate() {
    return QStringLiteral("/download?track_id=%1");
}

QString mixManHealthPath() {
    return QStringLiteral("/health");
}

QString mixManConfigPath() {
    return QStringLiteral("/config");
}

QString mixManIndexStatusPath() {
    return QStringLiteral("/recommendations/index_status");
}

QString mixManPolicyPresetsPath() {
    return QStringLiteral("/recommendations/policy-presets");
}

QString mixManSessionsPath() {
    return QStringLiteral("/sessions");
}

QString mixManSessionSnapshotPath(const QString& sessionId) {
    return QStringLiteral("/sessions/%1/snapshot").arg(sessionId);
}

QString mixManSessionIntentPath(const QString& sessionId) {
    return QStringLiteral("/sessions/%1/intent").arg(sessionId);
}

QString mixManSessionHeartbeatPath(const QString& sessionId) {
    return QStringLiteral("/sessions/%1/heartbeat").arg(sessionId);
}

QString mixManSessionPlaybackPath(const QString& sessionId) {
    return QStringLiteral("/sessions/%1/playback").arg(sessionId);
}

QString mixManSessionControlClaimPath(const QString& sessionId) {
    return QStringLiteral("/sessions/%1/control/claim").arg(sessionId);
}

QString mixManSessionCandidateSelectPath(const QString& sessionId, const QString& trackId) {
    return QStringLiteral("/sessions/%1/candidates/%2/select").arg(sessionId, trackId);
}

} // namespace config

RestLibrarySettings RestLibrarySettings::fromConfig(const UserSettingsPointer& pConfig) {
    RestLibrarySettings settings;
    if (!pConfig) {
        return settings;
    }

    settings.enabled = pConfig->getValue<bool>(config::kEnabledKey, config::kDefaultEnabled);
    settings.useMixManDefaults = pConfig->getValue<bool>(
            config::kUseMixManDefaultsKey,
            config::kDefaultUseMixManDefaults);
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
    if (settings.useMixManDefaults) {
        settings.trackListPath = config::mixManTrackListPath();
        settings.trackDetailPathTemplate = config::mixManTrackDetailPathTemplate();
        settings.trackLookupPathTemplate = config::mixManTrackLookupPathTemplate();
        settings.recommendationPathTemplate = config::mixManRecommendationPathTemplate();
        settings.audioDownloadPathTemplate = config::mixManAudioDownloadPathTemplate();
    } else {
        settings.trackListPath = pConfig->getValueString(config::kTrackListPathKey);
        settings.trackDetailPathTemplate = pConfig->getValueString(
                config::kTrackDetailPathTemplateKey);
        settings.trackLookupPathTemplate = pConfig->getValueString(
                config::kTrackLookupPathTemplateKey);
        settings.recommendationPathTemplate = pConfig->getValueString(
                config::kRecommendationPathTemplateKey);
        settings.audioDownloadPathTemplate = pConfig->getValueString(
                config::kAudioDownloadPathTemplateKey);
    }
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
    settings.mixManPathDepth = std::clamp(
            pConfig->getValue<int>(
                    config::kMixManPathDepthKey,
                    config::kDefaultMixManPathDepth),
            config::kMinMixManPathDepth,
            config::kMaxMixManPathDepth);
    settings.mixManPolicyPreset = pConfig->getValueString(config::kMixManPolicyPresetKey);
    if (settings.mixManPolicyPreset.trimmed().isEmpty()) {
        settings.mixManPolicyPreset = QStringLiteral("dj_assist");
    }
    settings.mixManTargetEnergyEnabled = pConfig->getValue<bool>(
            config::kMixManTargetEnergyEnabledKey,
            config::kDefaultMixManTargetEnergyEnabled);
    settings.mixManTargetEnergy = std::clamp(
            pConfig->getValue<int>(
                    config::kMixManTargetEnergyKey,
                    config::kDefaultMixManTargetEnergy),
            config::kMinMixManTargetEnergy,
            config::kMaxMixManTargetEnergy);
    settings.mixManTargetColorEnabled = pConfig->getValue<bool>(
            config::kMixManTargetColorEnabledKey,
            config::kDefaultMixManTargetColorEnabled);
    settings.mixManTargetColor = pConfig->getValueString(config::kMixManTargetColorKey);
    settings.mixManTargetBpmEnabled = pConfig->getValue<bool>(
            config::kMixManTargetBpmEnabledKey,
            config::kDefaultMixManTargetBpmEnabled);
    settings.mixManTargetBpm = std::clamp(
            pConfig->getValue<int>(
                    config::kMixManTargetBpmKey,
                    config::kDefaultMixManTargetBpm),
            config::kMinMixManTargetBpm,
            config::kMaxMixManTargetBpm);
    settings.mixManAdminApprovedOnly = pConfig->getValue<bool>(
            config::kMixManAdminApprovedOnlyKey,
            config::kDefaultMixManAdminApprovedOnly);
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

double RestLibrarySettings::mixManTargetEnergyNormalized() const {
    return static_cast<double>(mixManTargetEnergy) /
            static_cast<double>(config::kMaxMixManTargetEnergy);
}

} // namespace mixxx::library::rest
