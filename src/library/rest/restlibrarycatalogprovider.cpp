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
    connect(&m_client,
            &RestLibraryClient::trackMutationMetadataFetched,
            this,
            [this](const RestLibraryMutationMetadata& metadata) {
                if (m_activeMutationMetadataScopeIdentity.isEmpty()) {
                    return;
                }
                m_mutationMetadataScopeIdentity =
                        m_activeMutationMetadataScopeIdentity;
                m_mutationMetadata = metadata;
                const QString scopeIdentity =
                        std::exchange(m_activeMutationMetadataScopeIdentity, {});
                emit mutationMetadataFetched(scopeIdentity, metadata);
            });
    connect(&m_client,
            &RestLibraryClient::trackMutationFinished,
            this,
            [this](const RestLibraryTrackMutationResult& result) {
                if (m_activeMutationScopeIdentity.isEmpty()) {
                    return;
                }
                const QString scopeIdentity =
                        std::exchange(m_activeMutationScopeIdentity, {});
                emit trackMutationFinished(scopeIdentity, result);
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
    if (context.scopeIdentity == m_mutationMetadataScopeIdentity &&
            m_mutationMetadata.valid) {
        if (m_mutationMetadata.mayWriteFavour) {
            context.capabilities |= RestLibraryCatalogCapability::WriteFavour;
        }
        if (m_mutationMetadata.mayWriteDjComment) {
            context.capabilities |= RestLibraryCatalogCapability::WriteDjComment;
        }
        if (m_mutationMetadata.mayReturnToReview) {
            context.capabilities |= RestLibraryCatalogCapability::ReturnToReview;
        }
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

void MixManRestLibraryCatalogProvider::fetchMutationMetadata(
        const RestLibraryCatalogContext& requestedContext) {
    RestLibrarySettings settings;
    if (!settingsForContext(requestedContext, &settings)) {
        emit mutationMetadataFetched(requestedContext.scopeIdentity, {});
        return;
    }
    m_activeMutationMetadataScopeIdentity = requestedContext.scopeIdentity;
    m_client.fetchTrackMutationMetadata(settings);
}

void MixManRestLibraryCatalogProvider::updateTrackMetadata(
        const RestLibraryCatalogContext& requestedContext,
        const QString& remoteId,
        const QJsonObject& fields,
        RestLibraryTrackMutation mutation) {
    RestLibrarySettings settings;
    if (!settingsForContext(requestedContext, &settings)) {
        RestLibraryTrackMutationResult result;
        result.mutation = mutation;
        result.remoteId = remoteId;
        result.errorText = tr("REST Library settings changed.");
        emit trackMutationFinished(requestedContext.scopeIdentity, result);
        return;
    }
    m_activeMutationScopeIdentity = requestedContext.scopeIdentity;
    m_client.updateTrackMetadata(settings, remoteId, fields, mutation);
}

void MixManRestLibraryCatalogProvider::returnTrackToReview(
        const RestLibraryCatalogContext& requestedContext,
        const QString& remoteId,
        const QString& reason) {
    RestLibrarySettings settings;
    if (!settingsForContext(requestedContext, &settings)) {
        RestLibraryTrackMutationResult result;
        result.mutation = RestLibraryTrackMutation::ReturnToReview;
        result.remoteId = remoteId;
        result.errorText = tr("REST Library settings changed.");
        emit trackMutationFinished(requestedContext.scopeIdentity, result);
        return;
    }
    m_activeMutationScopeIdentity = requestedContext.scopeIdentity;
    m_client.returnTrackToReview(settings, remoteId, reason);
}

void MixManRestLibraryCatalogProvider::cancelTrackMutations() {
    m_client.invalidateTrackMutationRequests();
    m_activeMutationMetadataScopeIdentity.clear();
    m_activeMutationScopeIdentity.clear();
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
