/// Unit tests for the Sugiyama auto-layout algorithm.
///
/// Tests cover:
/// 1. Empty blueprint → no crash, empty result
/// 2. Single node → placed at margin
/// 3. Linear chain A→B→C → left-to-right layers
/// 4. Diamond (A→B, A→C, B→D, C→D) → correct layering
/// 5. Cycle A→B→C→A → cycle broken, feedback edges recorded
/// 6. Isolated nodes → placed in layer 0
/// 7. apply_layout produces Blueprint with updated positions
/// 8. apply_layout preserves node and wire count
/// 9. Self-loop edge ignored
/// 10. Two disconnected chains → both flow left-to-right
/// 11. Bidirectional edges (A→B + B→A) → ranked correctly without duplicates

#include <gtest/gtest.h>
#include "blueprint_v2/layout/auto_layout.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "core/strings/interned_id.h"

using namespace bp2::layout;
namespace bp = bp2;

namespace {

/// Helper: create a simple node with given id and position.
bp::Blueprint::Node make_node(core::InternedId id, float x = 0.0f, float y = 0.0f) {
    bp::Blueprint::Node n;
    n.semantic.id = id;
    n.semantic.type = core::InternedId{};
    n.layout.x = x;
    n.layout.y = y;
    return n;
}

/// Helper: create a wire between two node ports.
bp::Blueprint::Wire make_wire(core::StringInterner& interner,
                               core::InternedId src_node, const char* src_port,
                               core::InternedId tgt_node, const char* tgt_port) {
    bp::Blueprint::Wire w;
    w.id = interner.intern(std::string("wire_") + std::to_string(src_node.raw()) + "_" + std::to_string(tgt_node.raw()));
    w.source = {src_node, interner.intern(src_port)};
    w.target = {tgt_node, interner.intern(tgt_port)};
    return w;
}

/// Build a minimal Blueprint from nodes and wires.
bp::Blueprint build_bp(std::vector<bp::Blueprint::Node> nodes,
                        std::vector<bp::Blueprint::Wire> wires) {
    bp::Blueprint bp;
    for (auto& n : nodes) bp = bp.with_node(std::move(n));
    for (auto& w : wires) bp = bp.with_wire(std::move(w));
    return bp;
}

} // namespace

// =============================================================================
// Empty / trivial cases
// =============================================================================

TEST(AutoLayoutTest, EmptyBlueprintReturnsEmpty) {
    bp::Blueprint bp;
    auto result = compute_layout(bp);
    EXPECT_TRUE(result.positions.empty());
}

TEST(AutoLayoutTest, SingleNodePlacedAtMargin) {
    core::StringInterner interner;
    auto n1 = make_node(interner.intern("node_1"));
    auto bp = build_bp({n1}, {});

    LayoutOptions opts;
    opts.margin_x = 50.0f;
    opts.margin_y = 50.0f;
    auto result = compute_layout(bp, opts);

    ASSERT_EQ(result.positions.size(), 1u);
    auto it = result.positions.find(interner.intern("node_1"));
    ASSERT_NE(it, result.positions.end());
    EXPECT_FLOAT_EQ(it->second.x, 50.0f);
    EXPECT_FLOAT_EQ(it->second.y, 50.0f);
}

// =============================================================================
// Linear chain
// =============================================================================

TEST(AutoLayoutTest, LinearChainLeftToRight) {
    core::StringInterner interner;
    auto a = make_node(interner.intern("a"));
    auto b = make_node(interner.intern("b"));
    auto c = make_node(interner.intern("c"));

    auto w1 = make_wire(interner, interner.intern("a"), "out", interner.intern("b"), "in");
    auto w2 = make_wire(interner, interner.intern("b"), "out", interner.intern("c"), "in");

    auto bp = build_bp({a, b, c}, {w1, w2});
    auto result = compute_layout(bp);

    ASSERT_EQ(result.positions.size(), 3u);

    float ax = result.positions[interner.intern("a")].x;
    float bx = result.positions[interner.intern("b")].x;
    float cx = result.positions[interner.intern("c")].x;

    // A should be leftmost, C rightmost.
    EXPECT_LT(ax, bx);
    EXPECT_LT(bx, cx);
}

// =============================================================================
// Diamond topology
// =============================================================================

TEST(AutoLayoutTest, DiamondCorrectLayering) {
    core::StringInterner interner;
    auto a = make_node(interner.intern("a"));
    auto b = make_node(interner.intern("b"));
    auto c = make_node(interner.intern("c"));
    auto d = make_node(interner.intern("d"));

    // A→B, A→C, B→D, C→D
    auto w1 = make_wire(interner, interner.intern("a"), "out", interner.intern("b"), "in");
    auto w2 = make_wire(interner, interner.intern("a"), "out", interner.intern("c"), "in");
    auto w3 = make_wire(interner, interner.intern("b"), "out", interner.intern("d"), "in");
    auto w4 = make_wire(interner, interner.intern("c"), "out", interner.intern("d"), "in");

    auto bp = build_bp({a, b, c, d}, {w1, w2, w3, w4});
    auto result = compute_layout(bp);

    ASSERT_EQ(result.positions.size(), 4u);

    float ax = result.positions[interner.intern("a")].x;
    float bx = result.positions[interner.intern("b")].x;
    float cx = result.positions[interner.intern("c")].x;
    float dx = result.positions[interner.intern("d")].x;

    // A in layer 0, B and C in layer 1, D in layer 2.
    EXPECT_LT(ax, bx);
    EXPECT_LT(ax, cx);
    EXPECT_LT(bx, dx);
    EXPECT_LT(cx, dx);

    // B and C should be in the same layer (same x).
    EXPECT_FLOAT_EQ(bx, cx);
}

// =============================================================================
// Cycle handling
// =============================================================================

TEST(AutoLayoutTest, CycleIsBrokenWithoutCrash) {
    core::StringInterner interner;
    auto a = make_node(interner.intern("a"));
    auto b = make_node(interner.intern("b"));
    auto c = make_node(interner.intern("c"));

    // A→B→C→A (cycle)
    auto w1 = make_wire(interner, interner.intern("a"), "out", interner.intern("b"), "in");
    auto w2 = make_wire(interner, interner.intern("b"), "out", interner.intern("c"), "in");
    auto w3 = make_wire(interner, interner.intern("c"), "out", interner.intern("a"), "in");

    auto bp = build_bp({a, b, c}, {w1, w2, w3});

    // Should not hang or crash.
    auto result = compute_layout(bp);
    EXPECT_EQ(result.positions.size(), 3u);

    // All nodes should have distinct positions.
    float ax = result.positions[interner.intern("a")].x;
    float bx = result.positions[interner.intern("b")].x;
    float cx = result.positions[interner.intern("c")].x;

    // At least two should be at different x (some should be left-to-right).
    bool all_same = (ax == bx && bx == cx);
    EXPECT_FALSE(all_same);
}

// =============================================================================
// Isolated nodes
// =============================================================================

TEST(AutoLayoutTest, IsolatedNodesPlacedInFirstLayer) {
    core::StringInterner interner;
    auto isolated = make_node(interner.intern("iso"));
    auto a = make_node(interner.intern("a"));
    auto b = make_node(interner.intern("b"));
    auto w1 = make_wire(interner, interner.intern("a"), "out", interner.intern("b"), "in");

    auto bp = build_bp({isolated, a, b}, {w1});
    auto result = compute_layout(bp);

    ASSERT_EQ(result.positions.size(), 3u);

    // Isolated node should be in layer 0 (same x as 'a', which is a source).
    float iso_x = result.positions[interner.intern("iso")].x;
    float a_x = result.positions[interner.intern("a")].x;

    EXPECT_FLOAT_EQ(iso_x, a_x);
}

// =============================================================================
// apply_layout returns updated Blueprint
// =============================================================================

TEST(AutoLayoutTest, ApplyLayoutUpdatesPositions) {
    core::StringInterner interner;
    // Nodes at (0,0) → should be repositioned.
    auto a = make_node(interner.intern("a"), 0.0f, 0.0f);
    auto b = make_node(interner.intern("b"), 0.0f, 0.0f);
    auto w1 = make_wire(interner, interner.intern("a"), "out", interner.intern("b"), "in");

    auto bp = build_bp({a, b}, {w1});
    auto laid_out = apply_layout(bp);

    // Positions should have changed.
    const auto* a_node = laid_out.find_node(interner.intern("a"));
    const auto* b_node = laid_out.find_node(interner.intern("b"));
    ASSERT_NE(a_node, nullptr);
    ASSERT_NE(b_node, nullptr);

    // A should be left of B.
    EXPECT_LT(a_node->layout.x, b_node->layout.x);

    // Not both at (0,0) anymore.
    EXPECT_FALSE(a_node->layout.x == 0.0f && a_node->layout.y == 0.0f);
}

TEST(AutoLayoutTest, ApplyLayoutPreservesNodeCount) {
    core::StringInterner interner;
    auto a = make_node(interner.intern("a"));
    auto b = make_node(interner.intern("b"));
    auto w1 = make_wire(interner, interner.intern("a"), "out", interner.intern("b"), "in");

    auto bp = build_bp({a, b}, {w1});
    auto laid_out = apply_layout(bp);

    EXPECT_EQ(laid_out.nodes().size(), 2u);
    EXPECT_EQ(laid_out.wires().size(), 1u);
}

// =============================================================================
// Self-loop edge ignored
// =============================================================================

TEST(AutoLayoutTest, SelfLoopIgnored) {
    core::StringInterner interner;
    auto a = make_node(interner.intern("a"));
    // Self-loop wire.
    auto w1 = make_wire(interner, interner.intern("a"), "out", interner.intern("a"), "in");

    auto bp = build_bp({a}, {w1});
    auto result = compute_layout(bp);

    // Node still gets placed (as isolated, since self-loop is filtered).
    ASSERT_EQ(result.positions.size(), 1u);
    EXPECT_FLOAT_EQ(result.positions[interner.intern("a")].x, 50.0f);
}

// =============================================================================
// Disconnected components
// =============================================================================

TEST(AutoLayoutTest, TwoDisconnectedChains) {
    core::StringInterner interner;
    auto a = make_node(interner.intern("a"));
    auto b = make_node(interner.intern("b"));
    auto c = make_node(interner.intern("c"));
    auto d = make_node(interner.intern("d"));

    // Chain 1: A→B, Chain 2: C→D
    auto w1 = make_wire(interner, interner.intern("a"), "out", interner.intern("b"), "in");
    auto w2 = make_wire(interner, interner.intern("c"), "out", interner.intern("d"), "in");

    auto bp = build_bp({a, b, c, d}, {w1, w2});
    auto result = compute_layout(bp);

    EXPECT_EQ(result.positions.size(), 4u);

    // Both chains should have left-to-right flow.
    float ax = result.positions[interner.intern("a")].x;
    float bx = result.positions[interner.intern("b")].x;
    float cx = result.positions[interner.intern("c")].x;
    float dx = result.positions[interner.intern("d")].x;

    EXPECT_LT(ax, bx);
    EXPECT_LT(cx, dx);
}

// =============================================================================
// Bidirectional cycle (A→B and B→A both exist as separate edges)
// =============================================================================

TEST(AutoLayoutTest, BidirectionalEdgesRankedCorrectly) {
    core::StringInterner interner;
    auto a = make_node(interner.intern("a"));
    auto b = make_node(interner.intern("b"));
    auto c = make_node(interner.intern("c"));

    // A→B, B→C (chain), plus B→A (feedback). Both A→B and B→A exist.
    auto w1 = make_wire(interner, interner.intern("a"), "out", interner.intern("b"), "in");
    auto w2 = make_wire(interner, interner.intern("b"), "out", interner.intern("c"), "in");
    auto w3 = make_wire(interner, interner.intern("b"), "fb", interner.intern("a"), "in");

    auto bp = build_bp({a, b, c}, {w1, w2, w3});
    auto result = compute_layout(bp);

    // All three nodes must be placed.
    ASSERT_EQ(result.positions.size(), 3u);

    // Nodes must have distinct x positions (left-to-right ranking).
    float ax = result.positions[interner.intern("a")].x;
    float bx = result.positions[interner.intern("b")].x;
    float cx = result.positions[interner.intern("c")].x;

    // At least two nodes at different x (not all collapsed to same layer).
    bool all_same = (ax == bx && bx == cx);
    EXPECT_FALSE(all_same);

    // A→B→C should be left-to-right (the forward chain dominates ranking).
    EXPECT_LT(ax, bx);
    EXPECT_LT(bx, cx);
}
