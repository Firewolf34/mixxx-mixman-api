#include <gtest/gtest.h>

#include "library/rest/restlibrarymutationsequencer.h"

namespace {

using Sequencer = mixxx::library::rest::RestLibraryMutationSequencer;

Sequencer::LeaseFence lease(int generation = 1) {
    return {QStringLiteral("instance-1"), QStringLiteral("lease-1"), generation};
}

} // namespace

TEST(RestLibraryMutationSequencerTest, AllowsOnlyOneMutationInFlight) {
    Sequencer sequencer;
    sequencer.queue(Sequencer::Kind::Playback);
    sequencer.queue(Sequencer::Kind::Snapshot);

    const auto playback = sequencer.takeNext(lease());
    ASSERT_TRUE(playback);
    EXPECT_EQ(playback->kind, Sequencer::Kind::Playback);
    EXPECT_FALSE(sequencer.takeNext(lease()));

    EXPECT_TRUE(sequencer.complete(playback->sequence, playback->kind));
    const auto snapshot = sequencer.takeNext(lease());
    ASSERT_TRUE(snapshot);
    EXPECT_EQ(snapshot->kind, Sequencer::Kind::Snapshot);
}

TEST(RestLibraryMutationSequencerTest, CoalescesQueuedMutationToNewestState) {
    Sequencer sequencer;
    const quint64 first = sequencer.queue(Sequencer::Kind::Playback);
    const auto inFlight = sequencer.takeNext(lease());
    ASSERT_TRUE(inFlight);
    EXPECT_EQ(inFlight->sequence, first);

    sequencer.queue(Sequencer::Kind::Playback);
    const quint64 newest = sequencer.queue(Sequencer::Kind::Playback);
    ASSERT_TRUE(sequencer.complete(inFlight->sequence, inFlight->kind));

    const auto next = sequencer.takeNext(lease());
    ASSERT_TRUE(next);
    EXPECT_EQ(next->sequence, newest);
}

TEST(RestLibraryMutationSequencerTest, IgnoresAdverseStaleCompletion) {
    Sequencer sequencer;
    sequencer.queue(Sequencer::Kind::Playback);
    const auto first = sequencer.takeNext(lease());
    ASSERT_TRUE(first);
    ASSERT_TRUE(sequencer.complete(first->sequence, first->kind));

    sequencer.queue(Sequencer::Kind::Playback);
    const auto second = sequencer.takeNext(lease(2));
    ASSERT_TRUE(second);
    EXPECT_FALSE(sequencer.complete(first->sequence, first->kind));
    EXPECT_TRUE(sequencer.hasInFlight());
    EXPECT_TRUE(sequencer.complete(second->sequence, second->kind));
}

TEST(RestLibraryMutationSequencerTest, FencesLeaseMutationsAtDispatch) {
    Sequencer sequencer;
    sequencer.queue(Sequencer::Kind::Candidate);
    EXPECT_FALSE(sequencer.takeNext({}));

    const auto dispatch = sequencer.takeNext(lease(7));
    ASSERT_TRUE(dispatch);
    EXPECT_EQ(dispatch->lease.instanceId, QStringLiteral("instance-1"));
    EXPECT_EQ(dispatch->lease.leaseId, QStringLiteral("lease-1"));
    EXPECT_EQ(dispatch->lease.generation, 7);
}
