#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>

#include "library/rest/restlibrarycachemanager.h"

namespace mixxx::library::rest {

class RestLibraryBackend final : public QObject {
    Q_OBJECT

  public:
    explicit RestLibraryBackend(
            QObject* parent = nullptr,
            QNetworkAccessManager* pNetworkAccessManager = nullptr);

    QNetworkAccessManager* networkAccessManager() {
        return m_pNetworkAccessManager.data();
    }

    RestLibraryCacheManager* cacheManager() {
        return &m_cacheManager;
    }

  private:
    QNetworkAccessManager m_networkAccessManager;
    QPointer<QNetworkAccessManager> m_pNetworkAccessManager;
    RestLibraryCacheManager m_cacheManager;
};

} // namespace mixxx::library::rest
