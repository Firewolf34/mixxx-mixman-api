#pragma once

#include <memory>

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

#include "analyzer/trackanalysisscheduler.h"
#include "preferences/usersettings.h"
#include "track/track_decl.h"
#include "track/trackid.h"

namespace mixxx::library::rest {

enum class RestLibraryLoudnessState {
    Ready,
    Analyzing,
    Failed,
};

struct RestLibraryLoudnessResult {
    TrackId trackId;
    RestLibraryLoudnessState state = RestLibraryLoudnessState::Failed;
    QString errorText;
};

class RestLibraryLoudnessManager : public QObject {
    Q_OBJECT

  public:
    using QObject::QObject;
    ~RestLibraryLoudnessManager() override = default;

    virtual RestLibraryLoudnessResult prepareTrack(const TrackPointer& pTrack) = 0;

  signals:
    void trackLoudnessPrepared(
            const mixxx::library::rest::RestLibraryLoudnessResult& result);
};

class RestLibraryLoudnessAnalyzer final : public RestLibraryLoudnessManager {
    Q_OBJECT

  public:
    RestLibraryLoudnessAnalyzer(
            UserSettingsPointer pConfig,
            TrackAnalysisScheduler::Pointer pScheduler,
            QObject* parent = nullptr);
    ~RestLibraryLoudnessAnalyzer() override;

    RestLibraryLoudnessResult prepareTrack(const TrackPointer& pTrack) override;

  private slots:
    void slotTrackProgress(TrackId trackId, AnalyzerProgress analyzerProgress);

  private:
    RestLibraryLoudnessResult failedResult(
            TrackId trackId,
            const QString& errorText) const;

    const UserSettingsPointer m_pConfig;
    TrackAnalysisScheduler::Pointer m_pScheduler;
    QHash<TrackId, TrackPointer> m_pendingTracks;
    QSet<TrackId> m_preparedTrackIds;
};

} // namespace mixxx::library::rest

Q_DECLARE_METATYPE(mixxx::library::rest::RestLibraryLoudnessResult)
