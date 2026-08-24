#include "library/rest/restlibrarycatalogprovider.h"

#include <QCryptographicHash>
#include <QUrl>
#include <utility>

#include "moc_restlibrarycatalogprovider.cpp"

namespace mixxx::library::rest {

namespace {

QString mixManScopeIdentity(const RestLibrarySettings& settings) {
    QUrl url = settings.baseUrl.adjusted(
            QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment);
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    url.setPath(path);
    const QString scope = url.toString(QUrl::FullyEncoded) + QLatin1Char('|') +
            settings.trackListPath + QLatin1Char('|') +
            (settings.useMixManDefaults ? QStringLiteral("mixman")
                                        : QStringLiteral("custom")) +
            QLatin1Char('|') + settings.credentialContextNamespace();
    return QStringLiteral("mixman:") +
            QString::fromLatin1(QCryptographicHash::hash(
                    scope.toUtf8(), QCryptographicHash::Sha256)
                                        .toHex());
}

} // namespace

bool RestLibraryCatalogContext::operator==(
        const RestLibraryCatalogContext& other) const {
    return providerId == other.providerId &&
            scopeIdentity == other.scopeIdentity &&
            cacheIdentity == other.cacheIdentity &&
            displayName == other.displayName &&
            capabilities == other.capabilities &&
            maxPages == other.maxPages &&
            maxTracks == other.maxTracks &&
            configured == other.configured;
}

MixManRestLibraryCatalogProvider::MixManRestLibraryCatalogProvider(
        UserSettingsPointer pConfig,
        QNetworkAccessManager* pNetworkAccessManager,
        RestLibraryCacheManager* pCacheManager,
        QObject* parent)
        : RestLibraryCatalogProvider(parent),
          m_pConfig(std::move(pConfig)),
          m_client(pNetworkAccessManager, this),
          m_pCacheManager(pCacheManager) {
    connect(&m_client,
            &RestLibraryClient::trackCatalogPageFetched,
            this,
            [this](const RestLibraryCatalogPage& page) {
                if (!m_activeScopeIdentity.isEmpty()) {
                    emit pageFetched(m_activeScopeIdentity, page);
                }
            });
    connect(&m_client,
            &RestLibraryClient::trackCatalogFetchFailed,
            this,
            [this](const QString& message) {
                if (!m_activeScopeIdentity.isEmpty()) {
                    emit pageFetchFailed(m_activeScopeIdentity, message);
                }
            });
    if (m_pCacheManager) {
        connect(m_pCacheManager,
                &RestLibraryCacheManager::trackCacheStateChanged,
                this,
                &RestLibraryCatalogProvider::mediaStateChanged);
    }
}

RestLibraryCatalogContext MixManRestLibraryCatalogProvider::context() const {
    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(m_pConfig);
    RestLibraryCatalogContext context;
    context.providerId = QStringLiteral("mixman");
    context.scopeIdentity = mixManScopeIdentity(settings);
    context.cacheIdentity = RestLibraryCacheManager::cacheIdentity(settings);
    context.displayName = tr("MixMan");
    context.maxPages = settings.maxCatalogPages;
    context.maxTracks = settings.maxCatalogTracks;
    context.configured = settings.isConfigured() && settings.useMixManDefaults;
    if (context.configured && settings.hasAudioDownloadConfigured()) {
        context.capabilities |= RestLibraryCatalogCapability::ResolveAudio;
    }
    return context;
}

void MixManRestLibraryCatalogProvider::fetchPage(
        const RestLibraryCatalogContext& requestedContext,
        const QString& cursor) {
    RestLibrarySettings settings;
    if (!settingsForContext(requestedContext, &settings)) {
        emit pageFetchFailed(
                requestedContext.scopeIdentity,
                tr("REST Library catalog settings changed."));
        return;
    }
    m_activeScopeIdentity = requestedContext.scopeIdentity;
    m_client.fetchTrackCatalogPage(settings, cursor);
}

void MixManRestLibraryCatalogProvider::cancelPageFetch() {
    m_client.cancelTrackCatalogRequest();
    m_activeScopeIdentity.clear();
}

void MixManRestLibraryCatalogProvider::reconcileTracks(
        const RestLibraryCatalogContext& requestedContext,
        const QList<RestLibraryTrack>& tracks) {
    RestLibrarySettings settings;
    if (m_pCacheManager && settingsForContext(requestedContext, &settings)) {
        m_pCacheManager->reconcileTracks(tracks, settings);
    }
}

void MixManRestLibraryCatalogProvider::resolveAudio(
        const RestLibraryCatalogContext& requestedContext,
        const QList<RestLibraryTrack>& tracks,
        RestLibraryCacheRequestOwner owner) {
    RestLibrarySettings settings;
    if (m_pCacheManager &&
            requestedContext.capabilities.testFlag(
                    RestLibraryCatalogCapability::ResolveAudio) &&
            settingsForContext(requestedContext, &settings)) {
        m_pCacheManager->cacheTracks(tracks, settings, owner);
    }
}

void MixManRestLibraryCatalogProvider::cancelAudio(
        RestLibraryCacheRequestOwner owner) {
    if (m_pCacheManager) {
        m_pCacheManager->cancelRequests(owner);
    }
}

bool MixManRestLibraryCatalogProvider::settingsForContext(
        const RestLibraryCatalogContext& requestedContext,
        RestLibrarySettings* pSettings) const {
    const RestLibrarySettings settings = RestLibrarySettings::fromConfig(m_pConfig);
    if (requestedContext != context()) {
        return false;
    }
    if (pSettings) {
        *pSettings = settings;
    }
    return true;
}

} // namespace mixxx::library::rest
