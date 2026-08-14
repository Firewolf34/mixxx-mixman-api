#include "library/rest/restlibrarysettings.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

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

const QString kRestLibraryKeychainService = QStringLiteral("Mixxx REST Library");

class QtKeychainCredentialStore final : public RestLibraryCredentialStore {
  public:
    QString read(const QString& account) override {
#ifdef __QTKEYCHAIN__
        QKeychain::ReadPasswordJob readJob(kRestLibraryKeychainService);
        readJob.setAutoDelete(false);
        readJob.setKey(account);

        QEventLoop loop;
        readJob.connect(
                &readJob, &QKeychain::ReadPasswordJob::finished, &loop, &QEventLoop::quit);
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

    bool write(const QString& account, const QString& secret) override {
#ifdef __QTKEYCHAIN__
        QKeychain::WritePasswordJob writeJob(kRestLibraryKeychainService);
        writeJob.setAutoDelete(false);
        writeJob.setKey(account);
        writeJob.setTextData(secret);
        QEventLoop loop;
        writeJob.connect(
                &writeJob, &QKeychain::WritePasswordJob::finished, &loop, &QEventLoop::quit);
        writeJob.start();
        loop.exec();
        return writeJob.error() == QKeychain::Error::NoError;
#else
        Q_UNUSED(account);
        Q_UNUSED(secret);
        return false;
#endif
    }

    bool remove(const QString& account) override {
#ifdef __QTKEYCHAIN__
        QKeychain::DeletePasswordJob deleteJob(kRestLibraryKeychainService);
        deleteJob.setAutoDelete(false);
        deleteJob.setKey(account);
        QEventLoop loop;
        deleteJob.connect(
                &deleteJob, &QKeychain::DeletePasswordJob::finished, &loop, &QEventLoop::quit);
        deleteJob.start();
        loop.exec();
        return deleteJob.error() == QKeychain::Error::NoError ||
                deleteJob.error() == QKeychain::Error::EntryNotFound;
#else
        Q_UNUSED(account);
        return true;
#endif
    }
};

int effectivePort(const QUrl& url) {
    if (url.port() >= 0) {
        return url.port();
    }
    return url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0
            ? 443
            : 80;
}

QString sessionCredentialAccount(
        const RestLibrarySettings& settings,
        const QString& sessionId) {
    QUrl scopedUrl = settings.baseUrl.adjusted(
            QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment |
            QUrl::StripTrailingSlash);
    const QByteArray scope = QStringLiteral("%1\nmixxx\nrest_library\n%2")
                                     .arg(scopedUrl.toString(), sessionId.trimmed())
                                     .toUtf8();
    return QStringLiteral("session-v3:%1")
            .arg(QString::fromLatin1(
                    QCryptographicHash::hash(scope, QCryptographicHash::Sha256).toHex()));
}

} // namespace

RestLibraryCredentialStore* defaultRestLibraryCredentialStore() {
    static QtKeychainCredentialStore store;
    return &store;
}

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
    return QStringLiteral("/api/v3/sessions");
}

QString mixManSessionInstancesPath(const QString& sessionId) {
    return QStringLiteral("/api/v3/sessions/%1/instances").arg(sessionId);
}

QString mixManSessionInstanceHeartbeatPath(
        const QString& sessionId,
        const QString& instanceId) {
    return QStringLiteral("/api/v3/sessions/%1/instances/%2/heartbeat")
            .arg(sessionId, instanceId);
}

QString mixManSessionInstanceDisconnectPath(
        const QString& sessionId,
        const QString& instanceId) {
    return QStringLiteral("/api/v3/sessions/%1/instances/%2/disconnect")
            .arg(sessionId, instanceId);
}

QString mixManSessionStatePath(const QString& sessionId, const QString& instanceId) {
    return QStringLiteral("/api/v3/sessions/%1/state?instance_id=%2")
            .arg(sessionId, QString::fromUtf8(QUrl::toPercentEncoding(instanceId)));
}

QString mixManSessionSnapshotPath(const QString& sessionId) {
    return QStringLiteral("/api/v3/sessions/%1/snapshot").arg(sessionId);
}

QString mixManSessionIntentPath(const QString& sessionId) {
    return QStringLiteral("/api/v3/sessions/%1/intent").arg(sessionId);
}

QString mixManSessionPlaybackPath(const QString& sessionId) {
    return QStringLiteral("/api/v3/sessions/%1/playback").arg(sessionId);
}

QString mixManSessionPlaybackControlClaimPath(const QString& sessionId) {
    return QStringLiteral("/api/v3/sessions/%1/playback-control/claim").arg(sessionId);
}

QString mixManSessionPlaybackControlRenewPath(const QString& sessionId) {
    return QStringLiteral("/api/v3/sessions/%1/playback-control/renew").arg(sessionId);
}

QString mixManSessionPlaybackControlReleasePath(const QString& sessionId) {
    return QStringLiteral("/api/v3/sessions/%1/playback-control/release").arg(sessionId);
}

QString mixManSessionCandidateSelectPath(const QString& sessionId, const QString& trackId) {
    return QStringLiteral("/api/v3/sessions/%1/candidates/%2/select").arg(sessionId, trackId);
}

QString mixManSessionActionsPath(const QString& sessionId) {
    return QStringLiteral("/api/v3/sessions/%1/actions").arg(sessionId);
}

QUrl urlWithRestPath(const QUrl& baseUrl, const QString& path) {
    const QUrl pathUrl(path);
    if (pathUrl.isValid() && !pathUrl.isRelative()) {
        return isSameOrigin(baseUrl, pathUrl) ? pathUrl : QUrl();
    }

    QUrl url = baseUrl;
    const QString basePath = url.path();
    const QString relativePath = pathUrl.path().isEmpty() ? path : pathUrl.path();
    QString joinedPath;
    if (relativePath.startsWith(QLatin1Char('/'))) {
        joinedPath = basePath;
        if (joinedPath.endsWith(QLatin1Char('/'))) {
            joinedPath.chop(1);
        }
        joinedPath += relativePath;
    } else {
        joinedPath = basePath;
        if (!joinedPath.endsWith(QLatin1Char('/'))) {
            joinedPath += QLatin1Char('/');
        }
        joinedPath += relativePath;
    }
    if (joinedPath.isEmpty()) {
        joinedPath = QStringLiteral("/");
    }
    url.setPath(joinedPath);
    url.setQuery(pathUrl.query());
    return url;
}

bool isSameOrigin(const QUrl& lhs, const QUrl& rhs) {
    return lhs.scheme().compare(rhs.scheme(), Qt::CaseInsensitive) == 0 &&
            lhs.host().compare(rhs.host(), Qt::CaseInsensitive) == 0 &&
            effectivePort(lhs) == effectivePort(rhs);
}

bool isLoopbackUrl(const QUrl& url) {
    if (url.host().compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0) {
        return true;
    }
    const QHostAddress address(url.host());
    return !address.isNull() && address.isLoopback();
}

} // namespace config

RestLibrarySettings RestLibrarySettings::fromConfig(
        const UserSettingsPointer& pConfig,
        RestLibraryCredentialStore* pCredentialStore) {
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
    settings.mixManSessionId = pConfig->getValueString(config::kMixManSessionIdKey).trimmed();
    settings.bearerTokenKeychainAccount = bearerTokenAccountForUrl(settings.baseUrl);
    const QString configuredBearerAccount = pConfig->getValueString(
            config::kBearerTokenKeychainAccountKey);
    if (configuredBearerAccount != settings.bearerTokenKeychainAccount) {
        if (!configuredBearerAccount.isEmpty()) {
            kLogger.warning()
                    << "Ignoring REST library bearer token account that is not scoped to the configured server";
        }
        pConfig->setValue(
                config::kBearerTokenKeychainAccountKey,
                settings.bearerTokenKeychainAccount);
    }
    if (!pCredentialStore) {
        pCredentialStore = defaultRestLibraryCredentialStore();
    }
    settings.bearerToken = pCredentialStore->read(settings.bearerTokenKeychainAccount);
    const QString legacyBearerToken =
            pConfig->getValueString(config::kLocalDevBearerTokenKey);
    if (settings.bearerToken.isEmpty() && !legacyBearerToken.isEmpty()) {
        if (pCredentialStore->write(
                    settings.bearerTokenKeychainAccount, legacyBearerToken)) {
            settings.bearerToken = legacyBearerToken;
        } else {
            kLogger.warning()
                    << "REST library bearer token migration failed; plaintext token was discarded";
        }
    }
    if (pConfig->exists(config::kLocalDevBearerTokenKey)) {
        pConfig->remove(config::kLocalDevBearerTokenKey);
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

QString generateMixManSessionId(const QString& prefix) {
    QString normalizedPrefix = prefix.trimmed().toLower();
    if (normalizedPrefix.isEmpty()) {
        normalizedPrefix = QStringLiteral("mixxx");
    }
    return QStringLiteral("%1-%2")
            .arg(normalizedPrefix,
                    QUuid::createUuid().toString(QUuid::WithoutBraces).toLower());
}

RestLibrarySessionCredentials readMixManSessionCredentials(
        const RestLibrarySettings& settings,
        const QString& sessionId) {
    RestLibrarySessionCredentials credentials;
#ifdef __QTKEYCHAIN__
    QKeychain::ReadPasswordJob readJob(kRestLibraryKeychainService);
    readJob.setAutoDelete(false);
    readJob.setKey(sessionCredentialAccount(settings, sessionId));
    QEventLoop loop;
    readJob.connect(&readJob, &QKeychain::ReadPasswordJob::finished, &loop, &QEventLoop::quit);
    readJob.start();
    loop.exec();
    if (readJob.error() == QKeychain::Error::NoError) {
        const QJsonDocument document = QJsonDocument::fromJson(readJob.textData().toUtf8());
        if (document.isObject()) {
            credentials.instanceId =
                    document.object().value(QStringLiteral("instance_id")).toString();
            credentials.resumeToken =
                    document.object().value(QStringLiteral("resume_token")).toString();
        }
    }
#else
    Q_UNUSED(settings);
    Q_UNUSED(sessionId);
#endif
    return credentials;
}

bool writeMixManSessionCredentials(
        const RestLibrarySettings& settings,
        const QString& sessionId,
        const RestLibrarySessionCredentials& credentials) {
    if (!credentials.isComplete()) {
        return false;
    }
#ifdef __QTKEYCHAIN__
    QKeychain::WritePasswordJob writeJob(kRestLibraryKeychainService);
    writeJob.setAutoDelete(false);
    writeJob.setKey(sessionCredentialAccount(settings, sessionId));
    writeJob.setTextData(QString::fromUtf8(QJsonDocument(QJsonObject{
            {QStringLiteral("instance_id"), credentials.instanceId},
            {QStringLiteral("resume_token"), credentials.resumeToken},
    }).toJson(QJsonDocument::Compact)));
    QEventLoop loop;
    writeJob.connect(&writeJob, &QKeychain::WritePasswordJob::finished, &loop, &QEventLoop::quit);
    writeJob.start();
    loop.exec();
    return writeJob.error() == QKeychain::Error::NoError;
#else
    Q_UNUSED(settings);
    Q_UNUSED(sessionId);
    return false;
#endif
}

bool clearMixManSessionCredentials(
        const RestLibrarySettings& settings,
        const QString& sessionId) {
#ifdef __QTKEYCHAIN__
    QKeychain::DeletePasswordJob deleteJob(kRestLibraryKeychainService);
    deleteJob.setAutoDelete(false);
    deleteJob.setKey(sessionCredentialAccount(settings, sessionId));
    QEventLoop loop;
    deleteJob.connect(&deleteJob, &QKeychain::DeletePasswordJob::finished, &loop, &QEventLoop::quit);
    deleteJob.start();
    loop.exec();
    return deleteJob.error() == QKeychain::Error::NoError ||
            deleteJob.error() == QKeychain::Error::EntryNotFound;
#else
    Q_UNUSED(settings);
    Q_UNUSED(sessionId);
    return false;
#endif
}

bool RestLibrarySettings::isConfigured() const {
    const QString scheme = baseUrl.scheme().toLower();
    return enabled &&
            baseUrl.isValid() &&
            !baseUrl.isEmpty() &&
            !baseUrl.isRelative() &&
            baseUrl.userInfo().isEmpty() &&
            (scheme == QStringLiteral("https") || scheme == QStringLiteral("http")) &&
            hasAllowedBearerTransport() &&
            !trackListPath.trimmed().isEmpty() &&
            config::urlWithRestPath(baseUrl, trackListPath).isValid();
}

bool RestLibrarySettings::hasAudioDownloadConfigured() const {
    return isConfigured() &&
            cacheEnabled &&
            !cacheDirectoryPath.trimmed().isEmpty() &&
            audioDownloadPathTemplate.contains(QStringLiteral("%1")) &&
            config::urlWithRestPath(baseUrl, audioDownloadPathTemplate).isValid();
}

bool RestLibrarySettings::hasTrackLookupConfigured() const {
    return isConfigured() &&
            !trackLookupPathTemplate.trimmed().isEmpty() &&
            config::urlWithRestPath(baseUrl, trackLookupPathTemplate).isValid();
}

bool RestLibrarySettings::hasRecommendationsConfigured() const {
    return isConfigured() &&
            recommendationPathTemplate.contains(QStringLiteral("%1")) &&
            config::urlWithRestPath(baseUrl, recommendationPathTemplate).isValid();
}

bool RestLibrarySettings::hasAllowedBearerTransport() const {
    return bearerToken.isEmpty() ||
            baseUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0 ||
            (baseUrl.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) == 0 &&
                    config::isLoopbackUrl(baseUrl));
}

bool RestLibrarySettings::maySendBearerTokenTo(const QUrl& url) const {
    return !bearerToken.isEmpty() && hasAllowedBearerTransport() &&
            config::isSameOrigin(baseUrl, url);
}

double RestLibrarySettings::mixManTargetEnergyNormalized() const {
    return static_cast<double>(mixManTargetEnergy) /
            static_cast<double>(config::kMaxMixManTargetEnergy);
}

QString bearerTokenAccountForUrl(const QUrl& baseUrl) {
    const QUrl scopedUrl = baseUrl.adjusted(
            QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment |
            QUrl::StripTrailingSlash);
    return QStringLiteral("bearer:%1")
            .arg(QString::fromLatin1(
                    QCryptographicHash::hash(
                            scopedUrl.toString().toUtf8(), QCryptographicHash::Sha256)
                            .toHex()));
}

bool writeRestLibraryBearerToken(
        const RestLibrarySettings& settings,
        const QString& token,
        RestLibraryCredentialStore* pCredentialStore) {
    if (token.isEmpty()) {
        return clearRestLibraryBearerToken(settings, pCredentialStore);
    }
    if (!pCredentialStore) {
        pCredentialStore = defaultRestLibraryCredentialStore();
    }
    return pCredentialStore->write(settings.bearerTokenKeychainAccount, token);
}

bool clearRestLibraryBearerToken(
        const RestLibrarySettings& settings,
        RestLibraryCredentialStore* pCredentialStore) {
    if (!pCredentialStore) {
        pCredentialStore = defaultRestLibraryCredentialStore();
    }
    return pCredentialStore->remove(settings.bearerTokenKeychainAccount);
}

} // namespace mixxx::library::rest
