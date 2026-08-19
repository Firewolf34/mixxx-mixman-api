#include "library/rest/restlibrarybackend.h"

#include <utility>

#include "library/library.h"
#include "library/rest/restlibraryloudnessmanager.h"
#include "moc_restlibrarybackend.cpp"
#include "util/assert.h"

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

RestLibraryBackend::~RestLibraryBackend() = default;

void RestLibraryBackend::initializeLoudnessManager(
        Library* pLibrary,
        UserSettingsPointer pConfig) {
    VERIFY_OR_DEBUG_ASSERT(pLibrary) {
        return;
    }
    if (m_pLoudnessManager) {
        return;
    }
    m_pLoudnessManager = std::make_unique<RestLibraryLoudnessAnalyzer>(
            std::move(pConfig),
            pLibrary->createTrackAnalysisScheduler(
                    1,
                    static_cast<AnalyzerModeFlags>(
                            AnalyzerModeFlags::GainOnly |
                            AnalyzerModeFlags::LowPriority)));
}

} // namespace mixxx::library::rest
