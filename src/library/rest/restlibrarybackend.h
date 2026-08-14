#pragma once

#include <QNetworkAccessManager>
#include <QObject>

#include "library/rest/restlibrarycachemanager.h"

namespace mixxx::library::rest {

class RestLibraryBackend final : public QObject {
    Q_OBJECT

  public:
    explicit RestLibraryBackend(QObject* parent = nullptr);

    QNetworkAccessManager* networkAccessManager() {
        return &m_networkAccessManager;
    }

    RestLibraryCacheManager* cacheManager() {
        return &m_cacheManager;
    }

  private:
    QNetworkAccessManager m_networkAccessManager;
    RestLibraryCacheManager m_cacheManager;
};

} // namespace mixxx::library::rest
