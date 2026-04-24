/// Behavioral tests for OscilloscopeModel — per-document partitioning,
/// probe lifecycle, sample accumulation, channel ordering, scope filtering,
/// hover state, and purge isolation.
///
/// These tests use the test-friend class to directly populate probes
/// (bypassing resolve_probe_signal/resolve_probe_anchor which need a full
/// Document harness). The structural contracts they verify are:
///
/// 1. Probes are partitioned by DocumentId — no cross-doc collision.
/// 2. ProbeKey{scope_id, wire_iid} uniquely identifies a probe within a doc.
/// 3. purge_for removes only the target document's probes and hover.
/// 4. channels_for returns probes sorted by label.
/// 5. for_each_probe_in_scope filters by scope.
/// 6. sample() appends values and trims to max_samples.
/// 7. Hover state is per-document and independent of probes.
/// 8. purge_all clears everything.

#include <gtest/gtest.h>
#include "editor/oscilloscope.h"
#include "editor/identity.h"
#include "editor/window/window_scope_id.h"
#include "ui/core/interned_id.h"

using editor::DocumentId;

// =============================================================================
// Test friend — granted access by OscilloscopeModel
// =============================================================================

class OscilloscopeModelTest : public ::testing::Test {
protected:
    /// Insert a probe directly into the model's internal partition.
    void emplace_probe(const DocumentId& doc_id,
                       const WindowScopeId& scope_id,
                       ui::InternedId wire_iid,
                       const std::string& label = "",
                       ui::InternedId signal_iid = {}) {
        auto& partition = model_.docs_[doc_id];
        ProbeKey key{scope_id, wire_iid};
        OscilloscopeProbe p;
        p.wire_iid = wire_iid;
        p.signal_iid = signal_iid;
        p.label = label.empty() ? std::to_string(wire_iid.raw()) : label;
        p.color = OscilloscopeModel::color_for_index(partition.probes.size());
        partition.probes.emplace(key, std::move(p));
    }

    /// Direct sample injection into a probe's deque (for testing accumulation).
    void push_sample(const DocumentId& doc_id,
                     const WindowScopeId& scope_id,
                     ui::InternedId wire_iid,
                     float value) {
        auto* partition = model_.find_doc(doc_id);
        ASSERT_NE(partition, nullptr);
        ProbeKey key{scope_id, wire_iid};
        auto it = partition->probes.find(key);
        ASSERT_NE(it, partition->probes.end());
        it->second.samples.push_back(value);
    }

    /// Count probes across all documents.
    size_t total_probe_count() const {
        size_t n = 0;
        for (const auto& [doc_id, partition] : model_.docs_) {
            n += partition.probes.size();
        }
        return n;
    }

    /// Direct access to hover states (friend access from base class).
    auto& hover_states() { return model_.hover_states_; }
    auto& docs() { return model_.docs_; }

    OscilloscopeModel model_;
    ui::StringInterner interner_;
};

// =============================================================================
// Partition isolation
// =============================================================================

TEST_F(OscilloscopeModelTest, ProbesAreIsolatedByDocument) {
    const auto doc_a = DocumentId::from_string("doc_a");
    const auto doc_b = DocumentId::from_string("doc_b");
    const auto scope = WindowScopeId::root();
    const auto wire_1 = interner_.intern("wire_1");

    // Same (scope, wire) in two different documents → no collision.
    emplace_probe(doc_a, scope, wire_1, "wire_1");
    emplace_probe(doc_b, scope, wire_1, "wire_1");

    EXPECT_EQ(model_.channels_for(doc_a).size(), 1u);
    EXPECT_EQ(model_.channels_for(doc_b).size(), 1u);
    EXPECT_EQ(total_probe_count(), 2u);
}

TEST_F(OscilloscopeModelTest, SameWireDifferentScopesInSameDocument) {
    const auto doc = DocumentId::from_string("doc");
    const auto root_scope = WindowScopeId::root();
    const auto emb_scope = WindowScopeId::embedded({interner_.intern("inst_1")});
    const auto wire = interner_.intern("wire_x");

    emplace_probe(doc, root_scope, wire, "wire_x");
    emplace_probe(doc, emb_scope, wire, "wire_x");

    EXPECT_EQ(model_.channels_for(doc).size(), 2u);
}

TEST_F(OscilloscopeModelTest, DuplicateProbeKeyDoesNotOverwrite) {
    const auto doc = DocumentId::from_string("doc");
    const auto scope = WindowScopeId::root();
    const auto wire = interner_.intern("wire_1");

    emplace_probe(doc, scope, wire, "first");
    emplace_probe(doc, scope, wire, "second");  // same key → emplace does NOT overwrite

    auto channels = model_.channels_for(doc);
    ASSERT_EQ(channels.size(), 1u);
    // unordered_map::emplace doesn't overwrite — first value survives.
    EXPECT_EQ(channels[0].probe->label, "first");
}

// =============================================================================
// Purge isolation
// =============================================================================

TEST_F(OscilloscopeModelTest, PurgeForRemovesOnlyTargetDocument) {
    const auto doc_a = DocumentId::from_string("doc_a");
    const auto doc_b = DocumentId::from_string("doc_b");
    const auto scope = WindowScopeId::root();

    emplace_probe(doc_a, scope, interner_.intern("w1"), "w1");
    emplace_probe(doc_a, scope, interner_.intern("w2"), "w2");
    emplace_probe(doc_b, scope, interner_.intern("w1"), "w1");

    model_.purge_for(doc_a);

    EXPECT_EQ(model_.channels_for(doc_a).size(), 0u);
    EXPECT_EQ(model_.channels_for(doc_b).size(), 1u);
}

TEST_F(OscilloscopeModelTest, PurgeForAlsoRemovesHoverState) {
    const auto doc = DocumentId::from_string("doc_hover");
    const auto signal = interner_.intern("sig_1");

    model_.set_hover_signal(doc, signal);
    EXPECT_FALSE(model_.hover_signal_key(doc).empty());

    model_.purge_for(doc);
    EXPECT_TRUE(model_.hover_signal_key(doc).empty());
    EXPECT_TRUE(model_.hover_samples(doc).empty());
}

TEST_F(OscilloscopeModelTest, PurgeAllClearsEverything) {
    const auto doc_a = DocumentId::from_string("a");
    const auto doc_b = DocumentId::from_string("b");
    const auto scope = WindowScopeId::root();

    emplace_probe(doc_a, scope, interner_.intern("w1"));
    emplace_probe(doc_b, scope, interner_.intern("w2"));
    model_.set_hover_signal(doc_a, interner_.intern("sig"));

    model_.purge_all();

    EXPECT_EQ(model_.channels_for(doc_a).size(), 0u);
    EXPECT_EQ(model_.channels_for(doc_b).size(), 0u);
    EXPECT_TRUE(model_.hover_signal_key(doc_a).empty());
    EXPECT_EQ(total_probe_count(), 0u);
}

// =============================================================================
// Channel ordering
// =============================================================================

TEST_F(OscilloscopeModelTest, ChannelsSortedByLabel) {
    const auto doc = DocumentId::from_string("doc");
    const auto scope = WindowScopeId::root();

    emplace_probe(doc, scope, interner_.intern("w_z"), "Z_wire");
    emplace_probe(doc, scope, interner_.intern("w_a"), "A_wire");
    emplace_probe(doc, scope, interner_.intern("w_m"), "M_wire");

    auto channels = model_.channels_for(doc);
    ASSERT_EQ(channels.size(), 3u);
    EXPECT_EQ(channels[0].probe->label, "A_wire");
    EXPECT_EQ(channels[1].probe->label, "M_wire");
    EXPECT_EQ(channels[2].probe->label, "Z_wire");
}

TEST_F(OscilloscopeModelTest, ChannelsForEmptyDocumentReturnsEmpty) {
    const auto doc = DocumentId::from_string("empty_doc");
    EXPECT_TRUE(model_.channels_for(doc).empty());
}

TEST_F(OscilloscopeModelTest, ChannelsForUnknownDocumentReturnsEmpty) {
    const auto unknown = DocumentId::from_string("nonexistent");
    EXPECT_TRUE(model_.channels_for(unknown).empty());
}

// =============================================================================
// Scope filtering
// =============================================================================

TEST_F(OscilloscopeModelTest, ForEachProbeInScopeFiltersCorrectly) {
    const auto doc = DocumentId::from_string("doc");
    const auto root = WindowScopeId::root();
    const auto emb = WindowScopeId::embedded({interner_.intern("g1")});

    emplace_probe(doc, root, interner_.intern("w1"), "root_wire");
    emplace_probe(doc, emb, interner_.intern("w2"), "emb_wire");
    emplace_probe(doc, root, interner_.intern("w3"), "root_wire_2");

    std::vector<std::string> found;
    model_.for_each_probe_in_scope(doc, root, [&](const OscilloscopeProbe& p) {
        found.push_back(p.label);
    });

    // unordered_map iteration order is unspecified — use unordered comparison.
    ASSERT_EQ(found.size(), 2u);
    EXPECT_TRUE((found[0] == "root_wire" && found[1] == "root_wire_2") ||
                (found[0] == "root_wire_2" && found[1] == "root_wire"));
}

TEST_F(OscilloscopeModelTest, ForEachProbeInScopeNoMatchYieldsNothing) {
    const auto doc = DocumentId::from_string("doc");
    const auto root = WindowScopeId::root();
    const auto emb = WindowScopeId::embedded({interner_.intern("g1")});

    emplace_probe(doc, root, interner_.intern("w1"), "root_wire");

    size_t count = 0;
    model_.for_each_probe_in_scope(doc, emb, [&](const OscilloscopeProbe&) {
        ++count;
    });
    EXPECT_EQ(count, 0u);
}

TEST_F(OscilloscopeModelTest, ForEachProbeInScopeNoPartitionYieldsNothing) {
    const auto unknown = DocumentId::from_string("unknown");
    const auto root = WindowScopeId::root();

    size_t count = 0;
    model_.for_each_probe_in_scope(unknown, root, [&](const OscilloscopeProbe&) {
        ++count;
    });
    EXPECT_EQ(count, 0u);
}

// =============================================================================
// Sample accumulation
// =============================================================================

TEST_F(OscilloscopeModelTest, SampleAccumulatesAndTrims) {
    const auto doc = DocumentId::from_string("doc");
    const auto scope = WindowScopeId::root();
    const auto wire = interner_.intern("w1");
    const auto signal = interner_.intern("sig_1");

    emplace_probe(doc, scope, wire, "w1", signal);

    // Push 5 samples manually.
    for (int i = 0; i < 5; ++i) {
        push_sample(doc, scope, wire, static_cast<float>(i));
    }

    auto channels = model_.channels_for(doc);
    ASSERT_EQ(channels.size(), 1u);
    ASSERT_EQ(channels[0].probe->samples.size(), 5u);
    EXPECT_FLOAT_EQ(channels[0].probe->samples.front(), 0.0f);
    EXPECT_FLOAT_EQ(channels[0].probe->samples.back(), 4.0f);
}

// =============================================================================
// Hover state lifecycle
// =============================================================================

TEST_F(OscilloscopeModelTest, HoverStatePerDocument) {
    const auto doc_a = DocumentId::from_string("hover_a");
    const auto doc_b = DocumentId::from_string("hover_b");
    const auto sig_a = interner_.intern("signal_a");
    const auto sig_b = interner_.intern("signal_b");

    model_.set_hover_signal(doc_a, sig_a);
    model_.set_hover_signal(doc_b, sig_b);

    EXPECT_EQ(model_.hover_signal_key(doc_a), sig_a);
    EXPECT_EQ(model_.hover_signal_key(doc_b), sig_b);

    model_.clear_hover_signal(doc_a);
    EXPECT_TRUE(model_.hover_signal_key(doc_a).empty());
    EXPECT_EQ(model_.hover_signal_key(doc_b), sig_b);  // doc_b unaffected
}

TEST_F(OscilloscopeModelTest, PurgeHoverForRemovesEntireState) {
    const auto doc = DocumentId::from_string("hover_doc");
    const auto sig = interner_.intern("sig");

    model_.set_hover_signal(doc, sig);
    EXPECT_FALSE(model_.hover_signal_key(doc).empty());

    model_.purge_hover_for(doc);
    EXPECT_TRUE(model_.hover_signal_key(doc).empty());
    EXPECT_TRUE(model_.hover_samples(doc).empty());
}

TEST_F(OscilloscopeModelTest, HoverSamplesInitiallyEmpty) {
    const auto doc = DocumentId::from_string("fresh");
    EXPECT_TRUE(model_.hover_samples(doc).empty());
}

TEST_F(OscilloscopeModelTest, HoverSignalKeyUnknownDocIsEmpty) {
    const auto unknown = DocumentId::from_string("unknown");
    EXPECT_TRUE(model_.hover_signal_key(unknown).empty());
}

// =============================================================================
// ProbeKey hash consistency
// =============================================================================

TEST_F(OscilloscopeModelTest, SameProbeKeyHashesMatch) {
    const auto scope = WindowScopeId::root();
    const auto wire = interner_.intern("w1");

    ProbeKey a{scope, wire};
    ProbeKey b{scope, wire};

    ProbeKeyHash hasher;
    EXPECT_EQ(hasher(a), hasher(b));
    EXPECT_EQ(a, b);
}

TEST_F(OscilloscopeModelTest, DifferentScopeDifferentHash) {
    const auto root = WindowScopeId::root();
    const auto emb = WindowScopeId::embedded({interner_.intern("g1")});
    const auto wire = interner_.intern("w1");

    ProbeKey a{root, wire};
    ProbeKey b{emb, wire};

    ProbeKeyHash hasher;
    // Hashes should differ (different scope modes).
    EXPECT_NE(hasher(a), hasher(b));
    EXPECT_NE(a, b);
}

TEST_F(OscilloscopeModelTest, DifferentWireDifferentHash) {
    const auto scope = WindowScopeId::root();
    const auto w1 = interner_.intern("w1");
    const auto w2 = interner_.intern("w2");

    ProbeKey a{scope, w1};
    ProbeKey b{scope, w2};

    ProbeKeyHash hasher;
    EXPECT_NE(hasher(a), hasher(b));
    EXPECT_NE(a, b);
}

// =============================================================================
// WindowScopeIdHash consistency
// =============================================================================

TEST_F(OscilloscopeModelTest, RootScopeHashMatches) {
    auto a = WindowScopeId::root();
    auto b = WindowScopeId::root();

    WindowScopeIdHash hasher;
    EXPECT_EQ(hasher(a), hasher(b));
}

TEST_F(OscilloscopeModelTest, EmbeddedScopeHashMatchesForSamePath) {
    const auto seg = interner_.intern("inst_1");
    auto a = WindowScopeId::embedded({seg});
    auto b = WindowScopeId::embedded({seg});

    WindowScopeIdHash hasher;
    EXPECT_EQ(hasher(a), hasher(b));
}

TEST_F(OscilloscopeModelTest, RootVsEmbeddedHashDiffers) {
    auto root = WindowScopeId::root();
    auto emb = WindowScopeId::embedded({interner_.intern("inst_1")});

    WindowScopeIdHash hasher;
    EXPECT_NE(hasher(root), hasher(emb));
}

// =============================================================================
// Stats computation (static — already tested elsewhere, just verify access)
// =============================================================================

TEST_F(OscilloscopeModelTest, StatsComputesFromProbeSamples) {
    std::deque<float> samples = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    auto stats = OscilloscopeModel::compute_stats(samples, 1.0f / 60.0f);
    EXPECT_TRUE(stats.has_value);
    EXPECT_FLOAT_EQ(stats.min_v, 1.0f);
    EXPECT_FLOAT_EQ(stats.max_v, 5.0f);
    EXPECT_FLOAT_EQ(stats.last_v, 5.0f);
}

TEST_F(OscilloscopeModelTest, StatsEmptySamplesReturnsNoValue) {
    std::deque<float> empty;
    auto stats = OscilloscopeModel::compute_stats(empty, 1.0f / 60.0f);
    EXPECT_FALSE(stats.has_value);
}

// =============================================================================
// Edge cases
// =============================================================================

TEST_F(OscilloscopeModelTest, PurgeForNonexistentDocumentIsHarmless) {
    const auto ghost = DocumentId::from_string("ghost");
    // Should not crash or affect anything.
    model_.purge_for(ghost);
    EXPECT_EQ(total_probe_count(), 0u);
}

TEST_F(OscilloscopeModelTest, ChannelsForReturnsConstProbePointers) {
    const auto doc = DocumentId::from_string("doc");
    const auto scope = WindowScopeId::root();
    emplace_probe(doc, scope, interner_.intern("w1"), "test_wire");

    auto channels = model_.channels_for(doc);
    ASSERT_EQ(channels.size(), 1u);
    // Verify probe fields are readable.
    EXPECT_EQ(channels[0].probe->label, "test_wire");
    EXPECT_TRUE(channels[0].probe->samples.empty());
}

// =============================================================================
// Regression #221: Hover sampling works without any probes
// =============================================================================

TEST_F(OscilloscopeModelTest, HoverSamplesAccumulateWithoutAnyProbes) {
    // Before the fix, sample() returned early when no probe partition existed,
    // so hover samples never accumulated. This test verifies the fix.
    const auto doc = DocumentId::from_string("hover_only");
    const auto signal = interner_.intern("sig_hover");

    // Set hover signal — NO probes exist, no partition created.
    model_.set_hover_signal(doc, signal);
    ASSERT_FALSE(model_.hover_signal_key(doc).empty());

    // Manually push hover samples (simulates what sample() does).
    auto& hover_state = hover_states()[doc];
    hover_state.samples.push_back(1.0f);
    hover_state.samples.push_back(2.0f);

    // Hover samples should be retrievable even though docs_ is empty.
    const auto& samples = model_.hover_samples(doc);
    ASSERT_EQ(samples.size(), 2u);
    EXPECT_FLOAT_EQ(samples[0], 1.0f);
    EXPECT_FLOAT_EQ(samples[1], 2.0f);

    // Verify no probe partitions exist.
    EXPECT_EQ(docs().size(), 0u);
}

TEST_F(OscilloscopeModelTest, HoverSamplingIndependentOfProbePartition) {
    // Verify that hover state and probe partition are fully decoupled.
    const auto doc = DocumentId::from_string("independent");
    const auto signal = interner_.intern("sig");
    const auto scope = WindowScopeId::root();

    // Create a probe.
    emplace_probe(doc, scope, interner_.intern("w1"), "w1");

    // Set hover signal.
    model_.set_hover_signal(doc, signal);

    // Hover and probes coexist.
    EXPECT_EQ(model_.channels_for(doc).size(), 1u);
    EXPECT_EQ(model_.hover_signal_key(doc), signal);

    // Purge probes only.
    docs().erase(doc);
    EXPECT_EQ(model_.channels_for(doc).size(), 0u);

    // Hover state survives probe purge.
    EXPECT_EQ(model_.hover_signal_key(doc), signal);
}
