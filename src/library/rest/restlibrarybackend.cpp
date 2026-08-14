#include "library/rest/restlibrarybackend.h"

#include "moc_restlibrarybackend.cpp"

namespace mixxx::library::rest {

RestLibraryBackend::RestLibraryBackend(
        QObject* parent,
        QNetworkAccessManager* pNetworkAccessManager)
        : QObject(parent),
          m_pNetworkAccessManager(
                  pNetworkAccessManager ? pNetworkAccessManager
                                        : &m_networkAccessManager),
          m_cacheManager(m_pNetworkAccessManager.data(), this) {
}

} // namespace mixxx::library::rest
