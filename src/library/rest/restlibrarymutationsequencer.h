#pragma once

#include <array>
#include <optional>

#include <QString>
#include <QtGlobal>

namespace mixxx::library::rest {

class RestLibraryMutationSequencer final {
  public:
    enum class Kind {
        Claim,
        Renew,
        PolicyRefresh,
        Candidate,
        Playback,
        Snapshot,
        Release,
        Count,
    };

    struct LeaseFence {
        QString instanceId;
        QString leaseId;
        int generation = 0;

        bool isValid() const;
    };

    struct Dispatch {
        Kind kind = Kind::Count;
        quint64 sequence = 0;
        LeaseFence lease;

        bool isValid() const {
            return kind != Kind::Count && sequence > 0;
        }
    };

    quint64 queue(Kind kind);
    void cancel(Kind kind);
    bool hasQueued(Kind kind) const;
    bool hasAnyQueued() const;
    bool hasInFlight() const {
        return m_inFlight.has_value();
    }
    std::optional<Dispatch> takeNext(const LeaseFence& lease);
    bool complete(quint64 sequence, Kind kind);
    void clear();

  private:
    static constexpr size_t index(Kind kind) {
        return static_cast<size_t>(kind);
    }
    static bool requiresLease(Kind kind);

    std::array<quint64, static_cast<size_t>(Kind::Count)> m_queued{};
    std::optional<Dispatch> m_inFlight;
    quint64 m_nextSequence = 0;
};

} // namespace mixxx::library::rest
