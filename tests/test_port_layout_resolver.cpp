#include <gtest/gtest.h>
#include "editor/data/node.h"
#include "editor/data/port.h"
#include "editor/visual/node/port_layout_resolver.h"
#include "ui/core/interned_id.h"

TEST(PortLayoutResolver, DefaultLayout_NoOverrides) {
    ui::StringInterner interner;
    std::vector<EditorPort> inputs;
    inputs.emplace_back(interner.intern("v_in"), PortSide::Input, PortType::V);
    inputs.emplace_back(interner.intern("gnd"), PortSide::Input, PortType::V);
    
    std::vector<EditorPort> outputs;
    outputs.emplace_back(interner.intern("v_out"), PortSide::Output, PortType::V);
    
    std::vector<PortLayoutOverride> overrides;
    
    ResolvedLayout layout = resolve_port_layout(inputs, outputs, overrides, interner);
    
    EXPECT_EQ(layout.left.size(), 2u);
    EXPECT_EQ(layout.right.size(), 1u);
    EXPECT_EQ(layout.top.size(), 0u);
    EXPECT_EQ(layout.bottom.size(), 0u);
}

TEST(PortLayoutResolver, OverrideSide_MoveInputToRight) {
    ui::StringInterner interner;
    std::vector<EditorPort> inputs;
    inputs.emplace_back(interner.intern("v_in"), PortSide::Input, PortType::V);
    
    std::vector<EditorPort> outputs;
    
    std::vector<PortLayoutOverride> overrides;
    overrides.push_back({"v_in", PortLayoutSide::Right, std::nullopt});
    
    ResolvedLayout layout = resolve_port_layout(inputs, outputs, overrides, interner);
    
    EXPECT_EQ(layout.left.size(), 0u);
    EXPECT_EQ(layout.right.size(), 1u);
    EXPECT_EQ(layout.right[0].port_name, "v_in");
}

TEST(PortLayoutResolver, OverrideSide_MoveOutputToTop) {
    ui::StringInterner interner;
    std::vector<EditorPort> outputs;
    outputs.emplace_back(interner.intern("rpm_out"), PortSide::Output, PortType::RPM);
    
    std::vector<PortLayoutOverride> overrides;
    overrides.push_back({"rpm_out", PortLayoutSide::Top, std::nullopt});
    
    ResolvedLayout layout = resolve_port_layout({}, outputs, overrides, interner);
    
    EXPECT_EQ(layout.top.size(), 1u);
    EXPECT_EQ(layout.top[0].port_name, "rpm_out");
}

TEST(PortLayoutResolver, OverridePosition_Ordering) {
    ui::StringInterner interner;
    std::vector<EditorPort> outputs;
    outputs.emplace_back(interner.intern("a"), PortSide::Output, PortType::V);
    outputs.emplace_back(interner.intern("b"), PortSide::Output, PortType::V);
    
    std::vector<PortLayoutOverride> overrides;
    overrides.push_back({"a", PortLayoutSide::Right, 1});
    overrides.push_back({"b", PortLayoutSide::Right, 0});
    
    ResolvedLayout layout = resolve_port_layout({}, outputs, overrides, interner);
    
    ASSERT_EQ(layout.right.size(), 2u);
    EXPECT_EQ(layout.right[0].port_name, "b");  // position 0
    EXPECT_EQ(layout.right[1].port_name, "a");  // position 1
}

TEST(PortLayoutResolver, MixedOverrideAndAuto) {
    ui::StringInterner interner;
    std::vector<EditorPort> inputs;
    inputs.emplace_back(interner.intern("a"), PortSide::Input, PortType::V);
    inputs.emplace_back(interner.intern("b"), PortSide::Input, PortType::V);
    inputs.emplace_back(interner.intern("c"), PortSide::Input, PortType::V);
    
    std::vector<PortLayoutOverride> overrides;
    overrides.push_back({"c", PortLayoutSide::Left, 0});
    
    ResolvedLayout layout = resolve_port_layout(inputs, {}, overrides, interner);
    
    ASSERT_EQ(layout.left.size(), 3u);
    EXPECT_EQ(layout.left[0].port_name, "c");  // overridden to position 0
    // a and b come after (auto-positioned)
}

TEST(PortLayoutResolver, OrphanedOverride_Ignored) {
    ui::StringInterner interner;
    std::vector<EditorPort> inputs;
    inputs.emplace_back(interner.intern("v_in"), PortSide::Input, PortType::V);
    
    std::vector<PortLayoutOverride> overrides;
    overrides.push_back({"nonexistent", PortLayoutSide::Top, std::nullopt});
    
    ResolvedLayout layout = resolve_port_layout(inputs, {}, overrides, interner);
    
    EXPECT_EQ(layout.top.size(), 0u);
    EXPECT_EQ(layout.left.size(), 1u);
}

TEST(PortLayoutResolver, PositionCollision_StableSortOrder) {
    ui::StringInterner interner;
    std::vector<EditorPort> outputs;
    outputs.emplace_back(interner.intern("first"), PortSide::Output, PortType::V);
    outputs.emplace_back(interner.intern("second"), PortSide::Output, PortType::V);
    
    std::vector<PortLayoutOverride> overrides;
    overrides.push_back({"first", PortLayoutSide::Right, 0});
    overrides.push_back({"second", PortLayoutSide::Right, 0});
    
    ResolvedLayout layout = resolve_port_layout({}, outputs, overrides, interner);
    
    ASSERT_EQ(layout.right.size(), 2u);
    // First-defined wins position 0
    EXPECT_EQ(layout.right[0].port_name, "first");
    EXPECT_EQ(layout.right[1].port_name, "second");
}

TEST(PortLayoutResolver, PartialOverride_SideOnly) {
    ui::StringInterner interner;
    std::vector<EditorPort> outputs;
    outputs.emplace_back(interner.intern("v_out"), PortSide::Output, PortType::V);
    
    std::vector<PortLayoutOverride> overrides;
    overrides.push_back({"v_out", PortLayoutSide::Bottom, std::nullopt});
    
    ResolvedLayout layout = resolve_port_layout({}, outputs, overrides, interner);
    
    EXPECT_EQ(layout.bottom.size(), 1u);
    EXPECT_EQ(layout.bottom[0].port_name, "v_out");
}

TEST(PortLayoutResolver, AllPortsMovedToOneSide) {
    ui::StringInterner interner;
    std::vector<EditorPort> inputs;
    inputs.emplace_back(interner.intern("a"), PortSide::Input, PortType::V);
    inputs.emplace_back(interner.intern("b"), PortSide::Input, PortType::V);
    
    std::vector<EditorPort> outputs;
    outputs.emplace_back(interner.intern("c"), PortSide::Output, PortType::V);
    outputs.emplace_back(interner.intern("d"), PortSide::Output, PortType::V);
    
    std::vector<PortLayoutOverride> overrides;
    overrides.push_back({"a", PortLayoutSide::Top, std::nullopt});
    overrides.push_back({"b", PortLayoutSide::Top, std::nullopt});
    overrides.push_back({"c", PortLayoutSide::Top, std::nullopt});
    overrides.push_back({"d", PortLayoutSide::Top, std::nullopt});
    
    ResolvedLayout layout = resolve_port_layout(inputs, outputs, overrides, interner);
    
    EXPECT_EQ(layout.top.size(), 4u);
    EXPECT_EQ(layout.left.size(), 0u);
    EXPECT_EQ(layout.right.size(), 0u);
    EXPECT_EQ(layout.bottom.size(), 0u);
}

TEST(PortLayoutResolver, EmptyOverrides_EqualsDefault) {
    ui::StringInterner interner;
    std::vector<EditorPort> inputs;
    inputs.emplace_back(interner.intern("v_in"), PortSide::Input, PortType::V);
    
    std::vector<EditorPort> outputs;
    outputs.emplace_back(interner.intern("v_out"), PortSide::Output, PortType::V);
    
    ResolvedLayout layout = resolve_port_layout(inputs, outputs, {}, interner);
    
    EXPECT_EQ(layout.left.size(), 1u);
    EXPECT_EQ(layout.right.size(), 1u);
}

TEST(PortLayoutResolver, InOutPort_NoDuplicates) {
    // InOut ports appear in both inputs and outputs (see document.cpp:298-300).
    // The resolver must deduplicate by name so each physical port appears once.
    ui::StringInterner interner;
    auto bus_port = interner.intern("v");
    
    std::vector<EditorPort> inputs;
    inputs.emplace_back(bus_port, PortSide::InOut, PortType::V);
    
    std::vector<EditorPort> outputs;
    outputs.emplace_back(bus_port, PortSide::InOut, PortType::V);
    
    ResolvedLayout layout = resolve_port_layout(inputs, outputs, {}, interner);
    
    // Should have exactly one port, on the left (default for InOut)
    size_t total = layout.left.size() + layout.right.size() + 
                   layout.top.size() + layout.bottom.size();
    EXPECT_EQ(total, 1u) << "InOut port appearing in both inputs and outputs must be deduplicated";
    EXPECT_EQ(layout.left.size(), 1u);
    EXPECT_EQ(layout.left[0].port_name, "v");
    EXPECT_EQ(layout.left[0].logical_side, PortSide::InOut);
}

TEST(PortLayoutResolver, InOutPort_OverrideMovesToSide) {
    // An InOut port should be movable to any geometric side via override.
    ui::StringInterner interner;
    auto bus_port = interner.intern("v");
    
    std::vector<EditorPort> inputs;
    inputs.emplace_back(bus_port, PortSide::InOut, PortType::V);
    
    std::vector<EditorPort> outputs;
    outputs.emplace_back(bus_port, PortSide::InOut, PortType::V);
    
    std::vector<PortLayoutOverride> overrides;
    overrides.push_back({"v", PortLayoutSide::Top, std::nullopt});
    
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
    
    std::vector<EditorPort> inputs;
    inputs.emplace_back(in_port, PortSide::Input, PortType::V);
    inputs.emplace_back(inout_port, PortSide::InOut, PortType::V);
    
    std::vector<EditorPort> outputs;
    outputs.emplace_back(inout_port, PortSide::InOut, PortType::V);  // duplicate of InOut
    outputs.emplace_back(out_port, PortSide::Output, PortType::V);
    
    ResolvedLayout layout = resolve_port_layout(inputs, outputs, {}, interner);
    
    // ctrl (Input) + v (InOut) on left; status (Output) on right
    EXPECT_EQ(layout.left.size(), 2u);
    EXPECT_EQ(layout.right.size(), 1u);
    EXPECT_EQ(layout.right[0].port_name, "status");
}

TEST(PortLayoutResolver, LogicalSide_PreservedFromPortDefinition) {
    // Verify that logical_side comes from the EditorPort, not hardcoded.
    ui::StringInterner interner;
    std::vector<EditorPort> inputs;
    inputs.emplace_back(interner.intern("a"), PortSide::Input, PortType::V);
    
    std::vector<EditorPort> outputs;
    outputs.emplace_back(interner.intern("b"), PortSide::Output, PortType::RPM);
    
    ResolvedLayout layout = resolve_port_layout(inputs, outputs, {}, interner);
    
    ASSERT_EQ(layout.left.size(), 1u);
    ASSERT_EQ(layout.right.size(), 1u);
    EXPECT_EQ(layout.left[0].logical_side, PortSide::Input);
    EXPECT_EQ(layout.right[0].logical_side, PortSide::Output);
}
