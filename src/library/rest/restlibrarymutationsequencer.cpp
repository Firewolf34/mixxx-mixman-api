#include "library/rest/restlibrarymutationsequencer.h"

namespace mixxx::library::rest {

bool RestLibraryMutationSequencer::LeaseFence::isValid() const {
    return !instanceId.isEmpty() && !leaseId.isEmpty() && generation > 0;
}

quint64 RestLibraryMutationSequencer::queue(Kind kind) {
    if (kind == Kind::Count) {
        return 0;
    }
    const quint64 sequence = ++m_nextSequence;
    m_queued[index(kind)] = sequence;
    return sequence;
}

void RestLibraryMutationSequencer::cancel(Kind kind) {
    if (kind != Kind::Count) {
        m_queued[index(kind)] = 0;
    }
}

bool RestLibraryMutationSequencer::hasQueued(Kind kind) const {
    return kind != Kind::Count && m_queued[index(kind)] > 0;
}

bool RestLibraryMutationSequencer::hasAnyQueued() const {
    for (const quint64 sequence : m_queued) {
        if (sequence > 0) {
            return true;
        }
    }
    return false;
}

std::optional<RestLibraryMutationSequencer::Dispatch>
RestLibraryMutationSequencer::takeNext(const LeaseFence& lease) {
    if (m_inFlight) {
        return std::nullopt;
    }
    constexpr std::array priorities{
            Kind::Claim,
            Kind::Renew,
            Kind::PolicyRefresh,
            Kind::Candidate,
            Kind::Playback,
            Kind::Snapshot,
            Kind::Release,
    };
    for (const Kind kind : priorities) {
        const quint64 sequence = m_queued[index(kind)];
        if (sequence == 0 || (requiresLease(kind) && !lease.isValid())) {
            continue;
        }
        m_queued[index(kind)] = 0;
        m_inFlight = Dispatch{kind, sequence, requiresLease(kind) ? lease : LeaseFence{}};
        return m_inFlight;
    }
    return std::nullopt;
}

bool RestLibraryMutationSequencer::complete(quint64 sequence, Kind kind) {
    if (!m_inFlight || m_inFlight->sequence != sequence ||
            m_inFlight->kind != kind) {
        return false;
    }
    m_inFlight.reset();
    return true;
}

void RestLibraryMutationSequencer::clear() {
    m_queued.fill(0);
    m_inFlight.reset();
}

bool RestLibraryMutationSequencer::requiresLease(Kind kind) {
    return kind == Kind::Renew || kind == Kind::Candidate ||
            kind == Kind::Playback || kind == Kind::Snapshot ||
            kind == Kind::Release;
}

} // namespace mixxx::library::rest
