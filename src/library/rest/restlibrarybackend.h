#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>

#include <memory>

#include "library/rest/restlibrarycachemanager.h"
#include "preferences/usersettings.h"

class Library;

namespace mixxx::library::rest {

class RestLibraryLoudnessManager;

class RestLibraryBackend final : public QObject {
    Q_OBJECT

  public:
    explicit RestLibraryBackend(
            QObject* parent = nullptr,
            QNetworkAccessManager* pNetworkAccessManager = nullptr);
    ~RestLibraryBackend() override;

    QNetworkAccessManager* networkAccessManager() {
        return m_pNetworkAccessManager.data();
    }

    RestLibraryCacheManager* cacheManager() {
        return &m_cacheManager;
    }

    void initializeLoudnessManager(
            Library* pLibrary,
            UserSettingsPointer pConfig);
    RestLibraryLoudnessManager* loudnessManager() {
        return m_pLoudnessManager.get();
    }

  private:
    QNetworkAccessManager m_networkAccessManager;
    QPointer<QNetworkAccessManager> m_pNetworkAccessManager;
    RestLibraryCacheManager m_cacheManager;
    std::unique_ptr<RestLibraryLoudnessManager> m_pLoudnessManager;
};

} // namespace mixxx::library::rest
