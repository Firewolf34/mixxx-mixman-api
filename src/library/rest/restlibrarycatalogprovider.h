#pragma once

#include <QFlags>
#include <QObject>
#include <QString>

#include "library/rest/restlibrarycachemanager.h"
#include "library/rest/restlibraryclient.h"
#include "library/rest/restlibrarytrack.h"
#include "preferences/usersettings.h"

class QNetworkAccessManager;
class QJsonObject;

namespace mixxx::library::rest {

enum class RestLibraryCatalogCapability {
    ResolveAudio = 1 << 0,
    WriteFavour = 1 << 1,
    WriteDjComment = 1 << 2,
    ReturnToReview = 1 << 3,
};
Q_DECLARE_FLAGS(RestLibraryCatalogCapabilities, RestLibraryCatalogCapability)

/// Non-secret snapshot that scopes every catalog and media operation.
/// Remote IDs are stable only within (providerId, scopeIdentity).
struct RestLibraryCatalogContext {
    QString providerId;
    QString scopeIdentity;
    QString cacheIdentity;
    QString displayName;
    RestLibraryCatalogCapabilities capabilities;
    int maxPages = 0;
    int maxTracks = 0;
    bool configured = false;

    bool operator==(const RestLibraryCatalogContext& other) const;
    bool operator!=(const RestLibraryCatalogContext& other) const {
        return !(*this == other);
    }
};

class RestLibraryCatalogProvider : public QObject {
    Q_OBJECT

  public:
    using QObject::QObject;
    ~RestLibraryCatalogProvider() override = default;

    virtual RestLibraryCatalogContext context() const = 0;
    virtual void fetchPage(
            const RestLibraryCatalogContext& context,
            const QString& cursor) = 0;
    virtual void cancelPageFetch() = 0;
    virtual void reconcileTracks(
            const RestLibraryCatalogContext& context,
            const QList<RestLibraryTrack>& tracks) = 0;
    virtual void resolveAudio(
            const RestLibraryCatalogContext& context,
            const QList<RestLibraryTrack>& tracks,
            RestLibraryCacheRequestOwner owner) = 0;
    virtual void cancelAudio(RestLibraryCacheRequestOwner owner) = 0;
    virtual void fetchMutationMetadata(
            const RestLibraryCatalogContext& context) = 0;
    virtual void updateTrackMetadata(
            const RestLibraryCatalogContext& context,
            const QString& remoteId,
            const QJsonObject& fields,
            RestLibraryTrackMutation mutation) = 0;
    virtual void returnTrackToReview(
            const RestLibraryCatalogContext& context,
            const QString& remoteId,
            const QString& reason) = 0;
    virtual void cancelTrackMutations() = 0;

  signals:
    void pageFetched(
            const QString& scopeIdentity,
            const mixxx::library::rest::RestLibraryCatalogPage& page);
    void pageFetchFailed(
            const QString& scopeIdentity,
            const QString& message);
    void mediaStateChanged(
            const mixxx::library::rest::RestLibraryCacheResult& result);
    void mutationMetadataFetched(
            const QString& scopeIdentity,
            const mixxx::library::rest::RestLibraryMutationMetadata& metadata);
    void trackMutationFinished(
            const QString& scopeIdentity,
            const mixxx::library::rest::RestLibraryTrackMutationResult& result);
};

/// Catalog-only MixMan adapter. Session-v3 authority remains in
/// RestLibraryFeature and is intentionally not exposed through this interface.
class MixManRestLibraryCatalogProvider final : public RestLibraryCatalogProvider {
    Q_OBJECT

  public:
    MixManRestLibraryCatalogProvider(
            UserSettingsPointer pConfig,
            QNetworkAccessManager* pNetworkAccessManager,
            RestLibraryCacheManager* pCacheManager,
            QObject* parent = nullptr);

    RestLibraryCatalogContext context() const override;
    void fetchPage(
            const RestLibraryCatalogContext& context,
            const QString& cursor) override;
    void cancelPageFetch() override;
    void reconcileTracks(
            const RestLibraryCatalogContext& context,
            const QList<RestLibraryTrack>& tracks) override;
    void resolveAudio(
            const RestLibraryCatalogContext& context,
            const QList<RestLibraryTrack>& tracks,
            RestLibraryCacheRequestOwner owner) override;
    void cancelAudio(RestLibraryCacheRequestOwner owner) override;
    void fetchMutationMetadata(
            const RestLibraryCatalogContext& context) override;
    void updateTrackMetadata(
            const RestLibraryCatalogContext& context,
            const QString& remoteId,
            const QJsonObject& fields,
            RestLibraryTrackMutation mutation) override;
    void returnTrackToReview(
            const RestLibraryCatalogContext& context,
            const QString& remoteId,
            const QString& reason) override;
    void cancelTrackMutations() override;

  private:
    bool settingsForContext(
            const RestLibraryCatalogContext& context,
            RestLibrarySettings* pSettings) const;

    UserSettingsPointer m_pConfig;
    RestLibraryClient m_client;
    RestLibraryCacheManager* const m_pCacheManager;
    QString m_activeScopeIdentity;
    QString m_activeMutationMetadataScopeIdentity;
    QString m_activeMutationScopeIdentity;
    QString m_mutationMetadataScopeIdentity;
    RestLibraryMutationMetadata m_mutationMetadata;
};

} // namespace mixxx::library::rest

Q_DECLARE_OPERATORS_FOR_FLAGS(mixxx::library::rest::RestLibraryCatalogCapabilities)
