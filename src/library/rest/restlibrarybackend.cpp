#include "library/rest/restlibrarybackend.h"

#include "moc_restlibrarybackend.cpp"

namespace mixxx::library::rest {

RestLibraryBackend::RestLibraryBackend(QObject* parent)
        : QObject(parent),
          m_cacheManager(&m_networkAccessManager, this) {
}

} // namespace mixxx::library::rest
