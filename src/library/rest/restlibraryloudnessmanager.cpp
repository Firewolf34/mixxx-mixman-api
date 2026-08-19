#include "library/rest/restlibraryloudnessmanager.h"

#include <utility>

#include "analyzer/analyzerprogress.h"
#include "moc_restlibraryloudnessmanager.cpp"
#include "preferences/replaygainsettings.h"
#include "track/track.h"

namespace mixxx::library::rest {

RestLibraryLoudnessAnalyzer::RestLibraryLoudnessAnalyzer(
        UserSettingsPointer pConfig,
        TrackAnalysisScheduler::Pointer pScheduler,
        QObject* parent)
        : RestLibraryLoudnessManager(parent),
          m_pConfig(std::move(pConfig)),
          m_pScheduler(std::move(pScheduler)) {
    qRegisterMetaType<RestLibraryLoudnessResult>(
            "mixxx::library::rest::RestLibraryLoudnessResult");
    if (m_pScheduler) {
        connect(m_pScheduler.get(),
                &TrackAnalysisScheduler::trackProgress,
                this,
                &RestLibraryLoudnessAnalyzer::slotTrackProgress);
    }
}

RestLibraryLoudnessAnalyzer::~RestLibraryLoudnessAnalyzer() {
    if (m_pScheduler) {
        m_pScheduler->stop();
    }
}

RestLibraryLoudnessResult RestLibraryLoudnessAnalyzer::prepareTrack(
        const TrackPointer& pTrack) {
    if (!pTrack || !pTrack->getId().isValid()) {
        return failedResult({}, tr("REST track could not be prepared for loudness analysis."));
    }

    const TrackId trackId = pTrack->getId();
    const ReplayGainSettings settings(m_pConfig);
    if (!settings.getReplayGainEnabled() ||
            (pTrack->getReplayGain().hasRatio() &&
                    (m_preparedTrackIds.contains(trackId) ||
                            !settings.getReplayGainReanalyze()))) {
        return {trackId, RestLibraryLoudnessState::Ready, {}};
    }
    if (!settings.getReplayGainAnalyzerEnabled()) {
        return failedResult(
                trackId,
                tr("ReplayGain analysis is disabled; the REST track was not loaded."));
    }
    const int analyzerVersion = settings.getReplayGainAnalyzerVersion();
    if (analyzerVersion != 1 && analyzerVersion != 2) {
        return failedResult(
                trackId,
                tr("ReplayGain analyzer selection is invalid; the REST track was not loaded."));
    }
    if (m_pendingTracks.contains(trackId)) {
        return {trackId, RestLibraryLoudnessState::Analyzing, {}};
    }
    if (!m_pScheduler) {
        return failedResult(
                trackId,
                tr("ReplayGain analysis is unavailable; the REST track was not loaded."));
    }

    m_pendingTracks.insert(trackId, pTrack);
    if (!m_pScheduler->scheduleTrack(trackId)) {
        m_pendingTracks.remove(trackId);
        return failedResult(
                trackId,
                tr("ReplayGain analysis could not be scheduled; the REST track was not loaded."));
    }
    m_pScheduler->resume();
    return {trackId, RestLibraryLoudnessState::Analyzing, {}};
}

void RestLibraryLoudnessAnalyzer::slotTrackProgress(
        TrackId trackId,
        AnalyzerProgress analyzerProgress) {
    if (analyzerProgress != kAnalyzerProgressDone &&
            analyzerProgress != kAnalyzerProgressUnknown) {
        return;
    }
    const TrackPointer pTrack = m_pendingTracks.take(trackId);
    if (!pTrack) {
        return;
    }

    RestLibraryLoudnessResult result;
    result.trackId = trackId;
    if (analyzerProgress == kAnalyzerProgressDone &&
            pTrack->getReplayGain().hasRatio()) {
        m_preparedTrackIds.insert(trackId);
        result.state = RestLibraryLoudnessState::Ready;
    } else {
        result.state = RestLibraryLoudnessState::Failed;
        result.errorText = tr(
                "ReplayGain analysis failed; the REST track was not loaded.");
    }
    emit trackLoudnessPrepared(result);
}

RestLibraryLoudnessResult RestLibraryLoudnessAnalyzer::failedResult(
        TrackId trackId,
        const QString& errorText) const {
    return {trackId, RestLibraryLoudnessState::Failed, errorText};
}

} // namespace mixxx::library::rest
