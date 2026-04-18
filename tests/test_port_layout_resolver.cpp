#include <gtest/gtest.h>
#include "editor/data/node_content.h"
#include "editor/visual/node/port_layout_resolver.h"
#include "ui/core/interned_id.h"

TEST(PortLayoutResolver, DefaultLayout_NoOverrides) {
    ui::StringInterner interner;
    std::vector<bp2::NodePort> inputs;
    inputs.emplace_back(interner.intern("v_in"), bp2::Direction::Input, PortType::V);
    inputs.emplace_back(interner.intern("gnd"), bp2::Direction::Input, PortType::V);
    
    std::vector<bp2::NodePort> outputs;
    outputs.emplace_back(interner.intern("v_out"), bp2::Direction::Output, PortType::V);
    
    std::vector<PortLayoutOverride> overrides;
    
    ResolvedLayout layout = resolve_port_layout(inputs, outputs, overrides, interner);
    
    EXPECT_EQ(layout.left.size(), 2u);
    EXPECT_EQ(layout.right.size(), 1u);
    EXPECT_EQ(layout.top.size(), 0u);
    EXPECT_EQ(layout.bottom.size(), 0u);
}

TEST(PortLayoutResolver, OverrideSide_MoveInputToRight) {
    ui::StringInterner interner;
    std::vector<bp2::NodePort> inputs;
    inputs.emplace_back(interner.intern("v_in"), bp2::Direction::Input, PortType::V);
    
    std::vector<bp2::NodePort> outputs;
    
    std::vector<PortLayoutOverride> overrides;
    overrides.push_back({"v_in", bp2::PortLayoutSide::Right, std::nullopt});
    
    ResolvedLayout layout = resolve_port_layout(inputs, outputs, overrides, interner);
    
    EXPECT_EQ(layout.left.size(), 0u);
    EXPECT_EQ(layout.right.size(), 1u);
    EXPECT_EQ(layout.right[0].port_name, "v_in");
}

TEST(PortLayoutResolver, OverrideSide_MoveOutputToTop) {
    ui::StringInterner interner;
    std::vector<bp2::NodePort> outputs;
    outputs.emplace_back(interner.intern("rpm_out"), bp2::Direction::Output, PortType::RPM);
    
    std::vector<PortLayoutOverride> overrides;
    overrides.push_back({"rpm_out", bp2::PortLayoutSide::Top, std::nullopt});
    
    ResolvedLayout layout = resolve_port_layout({}, outputs, overrides, interner);
    
    EXPECT_EQ(layout.top.size(), 1u);
    EXPECT_EQ(layout.top[0].port_name, "rpm_out");
}

TEST(PortLayoutResolver, OverridePosition_Ordering) {
    ui::StringInterner interner;
    std::vector<bp2::NodePort> outputs;
    outputs.emplace_back(interner.intern("a"), bp2::Direction::Output, PortType::V);
    outputs.emplace_back(interner.intern("b"), bp2::Direction::Output, PortType::V);
    
    std::vector<PortLayoutOverride> overrides;
    overrides.push_back({"a", bp2::PortLayoutSide::Right, 1});
    overrides.push_back({"b", bp2::PortLayoutSide::Right, 0});
    
    ResolvedLayout layout = resolve_port_layout({}, outputs, overrides, interner);
    
    ASSERT_EQ(layout.right.size(), 2u);
    EXPECT_EQ(layout.right[0].port_name, "b");  // position 0
    EXPECT_EQ(layout.right[1].port_name, "a");  // position 1
}

TEST(PortLayoutResolver, MixedOverrideAndAuto) {
    ui::StringInterner interner;
    std::vector<bp2::NodePort> inputs;
    inputs.emplace_back(interner.intern("a"), bp2::Direction::Input, PortType::V);
    inputs.emplace_back(interner.intern("b"), bp2::Direction::Input, PortType::V);
    inputs.emplace_back(interner.intern("c"), bp2::Direction::Input, PortType::V);
    
    std::vector<PortLayoutOverride> overrides;
    overrides.push_back({"c", bp2::PortLayoutSide::Left, 0});
    
    ResolvedLayout layout = resolve_port_layout(inputs, {}, overrides, interner);
    
    ASSERT_EQ(layout.left.size(), 3u);
    EXPECT_EQ(layout.left[0].port_name, "c");  // overridden to position 0
    // a and b come after (auto-positioned)
}

TEST(PortLayoutResolver, OrphanedOverride_Ignored) {
    ui::StringInterner interner;
    std::vector<bp2::NodePort> inputs;
    inputs.emplace_back(interner.intern("v_in"), bp2::Direction::Input, PortType::V);
    
    std::vector<PortLayoutOverride> overrides;
    overrides.push_back({"nonexistent", bp2::PortLayoutSide::Top, std::nullopt});
    
    ResolvedLayout layout = resolve_port_layout(inputs, {}, overrides, interner);
    
    EXPECT_EQ(layout.top.size(), 0u);
    EXPECT_EQ(layout.left.size(), 1u);
}

TEST(PortLayoutResolver, PositionCollision_StableSortOrder) {
    ui::StringInterner interner;
    std::vector<bp2::NodePort> outputs;
    outputs.emplace_back(interner.intern("first"), bp2::Direction::Output, PortType::V);
    outputs.emplace_back(interner.intern("second"), bp2::Direction::Output, PortType::V);
    
    std::vector<PortLayoutOverride> overrides;
    overrides.push_back({"first", bp2::PortLayoutSide::Right, 0});
    overrides.push_back({"second", bp2::PortLayoutSide::Right, 0});
    
    ResolvedLayout layout = resolve_port_layout({}, outputs, overrides, interner);
    
    ASSERT_EQ(layout.right.size(), 2u);
    // First-defined wins position 0
    EXPECT_EQ(layout.right[0].port_name, "first");
    EXPECT_EQ(layout.right[1].port_name, "second");
}

TEST(PortLayoutResolver, PartialOverride_SideOnly) {
    ui::StringInterner interner;
    std::vector<bp2::NodePort> outputs;
    outputs.emplace_back(interner.intern("v_out"), bp2::Direction::Output, PortType::V);
    
    std::vector<PortLayoutOverride> overrides;
    overrides.push_back({"v_out", bp2::PortLayoutSide::Bottom, std::nullopt});
    
    ResolvedLayout layout = resolve_port_layout({}, outputs, overrides, interner);
    
    EXPECT_EQ(layout.bottom.size(), 1u);
    EXPECT_EQ(layout.bottom[0].port_name, "v_out");
}

TEST(PortLayoutResolver, AllPortsMovedToOneSide) {
    ui::StringInterner interner;
    std::vector<bp2::NodePort> inputs;
    inputs.emplace_back(interner.intern("a"), bp2::Direction::Input, PortType::V);
    inputs.emplace_back(interner.intern("b"), bp2::Direction::Input, PortType::V);
    
    std::vector<bp2::NodePort> outputs;
    outputs.emplace_back(interner.intern("c"), bp2::Direction::Output, PortType::V);
    outputs.emplace_back(interner.intern("d"), bp2::Direction::Output, PortType::V);
    
    std::vector<PortLayoutOverride> overrides;
    overrides.push_back({"a", bp2::PortLayoutSide::Top, std::nullopt});
    overrides.push_back({"b", bp2::PortLayoutSide::Top, std::nullopt});
    overrides.push_back({"c", bp2::PortLayoutSide::Top, std::nullopt});
    overrides.push_back({"d", bp2::PortLayoutSide::Top, std::nullopt});
    
    ResolvedLayout layout = resolve_port_layout(inputs, outputs, overrides, interner);
    
    EXPECT_EQ(layout.top.size(), 4u);
    EXPECT_EQ(layout.left.size(), 0u);
    EXPECT_EQ(layout.right.size(), 0u);
    EXPECT_EQ(layout.bottom.size(), 0u);
}

TEST(PortLayoutResolver, EmptyOverrides_EqualsDefault) {
    ui::StringInterner interner;
    std::vector<bp2::NodePort> inputs;
    inputs.emplace_back(interner.intern("v_in"), bp2::Direction::Input, PortType::V);
    
    std::vector<bp2::NodePort> outputs;
    outputs.emplace_back(interner.intern("v_out"), bp2::Direction::Output, PortType::V);
    
    ResolvedLayout layout = resolve_port_layout(inputs, outputs, {}, interner);
    
    EXPECT_EQ(layout.left.size(), 1u);
    EXPECT_EQ(layout.right.size(), 1u);
}

TEST(PortLayoutResolver, InOutPort_NoDuplicates) {
    // InOut ports appear in both inputs and outputs (see document.cpp:298-300).
    // The resolver must deduplicate by name so each physical port appears once.
    ui::StringInterner interner;
    auto bus_port = interner.intern("v");
    
    std::vector<bp2::NodePort> inputs;
    inputs.emplace_back(bus_port, bp2::Direction::InOut, PortType::V);
    
    std::vector<bp2::NodePort> outputs;
    outputs.emplace_back(bus_port, bp2::Direction::InOut, PortType::V);
    
    ResolvedLayout layout = resolve_port_layout(inputs, outputs, {}, interner);
    
    // Should have exactly one port, on the left (default for InOut)
    size_t total = layout.left.size() + layout.right.size() + 
                   layout.top.size() + layout.bottom.size();
    EXPECT_EQ(total, 1u) << "InOut port appearing in both inputs and outputs must be deduplicated";
    EXPECT_EQ(layout.left.size(), 1u);
    EXPECT_EQ(layout.left[0].port_name, "v");
    EXPECT_EQ(layout.left[0].logical_direction, bp2::Direction::InOut);
}

TEST(PortLayoutResolver, InOutPort_OverrideMovesToSide) {
    // An InOut port should be movable to any geometric side via override.
    ui::StringInterner interner;
    auto bus_port = interner.intern("v");
    
    std::vector<bp2::NodePort> inputs;
    inputs.emplace_back(bus_port, bp2::Direction::InOut, PortType::V);
    
    std::vector<bp2::NodePort> outputs;
    outputs.emplace_back(bus_port, bp2::Direction::InOut, PortType::V);
    
    std::vector<PortLayoutOverride> overrides;
    overrides.push_back({"v", bp2::PortLayoutSide::Top, std::nullopt});
    
    ResolvedLayout layout = resolve_port_layout(inputs, outputs, overrides, interner);
    
    EXPECT_EQ(layout.top.size(), 1u);
    EXPECT_EQ(layout.left.size(), 0u);
    EXPECT_EQ(layout.right.size(), 0u);
    EXPECT_EQ(layout.bottom.size(), 0u);
}

TEST(PortLayoutResolver, InOutPort_MixedWithRegularPorts) {
    // Verify InOut dedup doesn't interfere with normal Input/Output ports.
    ui::StringInterner interner;
    auto in_port = interner.intern("ctrl");
    auto inout_port = interner.intern("v");
    auto out_port = interner.intern("status");
    
    std::vector<bp2::NodePort> inputs;
    inputs.emplace_back(in_port, bp2::Direction::Input, PortType::V);
    inputs.emplace_back(inout_port, bp2::Direction::InOut, PortType::V);
    
    std::vector<bp2::NodePort> outputs;
    outputs.emplace_back(inout_port, bp2::Direction::InOut, PortType::V);  // duplicate of InOut
    outputs.emplace_back(out_port, bp2::Direction::Output, PortType::V);
    
    ResolvedLayout layout = resolve_port_layout(inputs, outputs, {}, interner);
    
    // ctrl (Input) + v (InOut) on left; status (Output) on right
    EXPECT_EQ(layout.left.size(), 2u);
    EXPECT_EQ(layout.right.size(), 1u);
    EXPECT_EQ(layout.right[0].port_name, "status");
}

TEST(PortLayoutResolver, LogicalDirection_PreservedFromPortDefinition) {
    // Verify that logical_direction comes from the NodePort, not hardcoded.
    ui::StringInterner interner;
    std::vector<bp2::NodePort> inputs;
    inputs.emplace_back(interner.intern("a"), bp2::Direction::Input, PortType::V);

    std::vector<bp2::NodePort> outputs;
    outputs.emplace_back(interner.intern("b"), bp2::Direction::Output, PortType::RPM);

    ResolvedLayout layout = resolve_port_layout(inputs, outputs, {}, interner);

    ASSERT_EQ(layout.left.size(), 1u);
    ASSERT_EQ(layout.right.size(), 1u);
    EXPECT_EQ(layout.left[0].logical_direction, bp2::Direction::Input);
    EXPECT_EQ(layout.right[0].logical_direction, bp2::Direction::Output);
}

// ============================================================
// REGRESSION: Fix 1 — stable_partition preserves insertion order
// ============================================================
// Before this fix, std::partition was used to separate hinted ports
// from auto ports. std::partition does NOT preserve relative order,
// so auto ports could appear in a non-deterministic order. The fix
// uses std::stable_partition, which guarantees that auto ports appear
// in their original insertion order.

TEST(PortLayoutResolver, REGRESSION_StablePartitionPreservesAutoPortOrder) {
    // Create many auto ports (no position overrides) and verify
    // they come out in the same order they were inserted.
    ui::StringInterner interner;
    std::vector<bp2::NodePort> inputs;
    inputs.emplace_back(interner.intern("alpha"), bp2::Direction::Input, PortType::V);
    inputs.emplace_back(interner.intern("beta"), bp2::Direction::Input, PortType::V);
    inputs.emplace_back(interner.intern("gamma"), bp2::Direction::Input, PortType::V);
    inputs.emplace_back(interner.intern("delta"), bp2::Direction::Input, PortType::V);
    inputs.emplace_back(interner.intern("epsilon"), bp2::Direction::Input, PortType::V);
    
    // No overrides — all are auto-positioned
    ResolvedLayout layout = resolve_port_layout(inputs, {}, {}, interner);
    
    ASSERT_EQ(layout.left.size(), 5u);
    EXPECT_EQ(layout.left[0].port_name, "alpha");
    EXPECT_EQ(layout.left[1].port_name, "beta");
    EXPECT_EQ(layout.left[2].port_name, "gamma");
    EXPECT_EQ(layout.left[3].port_name, "delta");
    EXPECT_EQ(layout.left[4].port_name, "epsilon");
}

TEST(PortLayoutResolver, REGRESSION_StablePartitionMixedHintedAndAuto) {
    // Mix of hinted and auto ports. Hinted ports should come first (sorted by hint),
    // then auto ports in their original insertion order.
    ui::StringInterner interner;
    std::vector<bp2::NodePort> inputs;
    inputs.emplace_back(interner.intern("a1"), bp2::Direction::Input, PortType::V);
    inputs.emplace_back(interner.intern("a2"), bp2::Direction::Input, PortType::V);
    inputs.emplace_back(interner.intern("a3"), bp2::Direction::Input, PortType::V);
    inputs.emplace_back(interner.intern("a4"), bp2::Direction::Input, PortType::V);
    
    // Override: a3 to position 0, a1 to position 1
    // Auto: a2, a4 (should appear after hinted, in insertion order)
    std::vector<PortLayoutOverride> overrides;
    overrides.push_back({"a3", bp2::PortLayoutSide::Left, 0});
    overrides.push_back({"a1", bp2::PortLayoutSide::Left, 1});
    
    ResolvedLayout layout = resolve_port_layout(inputs, {}, overrides, interner);
    
    ASSERT_EQ(layout.left.size(), 4u);
    // Hinted first, sorted by hint
    EXPECT_EQ(layout.left[0].port_name, "a3");  // hint 0
    EXPECT_EQ(layout.left[1].port_name, "a1");  // hint 1
    // Auto ports in original insertion order
    EXPECT_EQ(layout.left[2].port_name, "a2");
    EXPECT_EQ(layout.left[3].port_name, "a4");
}
