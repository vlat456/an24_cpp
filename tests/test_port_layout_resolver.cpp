#include <gtest/gtest.h>
#include "editor/data/node_content.h"
#include "editor/visual/node/port_layout_resolver.h"
#include "blueprint_v2/interface/port_descriptor.h"
#include "core/domain_types.h"
#include "core/strings/interned_id.h"

TEST(PortLayoutResolver, DefaultLayout_NoOverrides) {
    core::StringInterner interner;
    std::vector<bp2::PortDescriptor> inputs;
    inputs.push_back({interner.intern("v_in"), Domain::Electrical, bp2::Direction::Input, PortType::V});
    inputs.push_back({interner.intern("gnd"), Domain::Electrical, bp2::Direction::Input, PortType::V});
    
    std::vector<bp2::PortDescriptor> outputs;
    outputs.push_back({interner.intern("v_out"), Domain::Electrical, bp2::Direction::Output, PortType::V});
    
    std::vector<PortLayoutOverride> overrides;
    
    ResolvedLayout layout = resolve_port_layout(inputs, outputs, overrides, interner);
    
    EXPECT_EQ(layout.left.size(), 2u);
    EXPECT_EQ(layout.right.size(), 1u);
    EXPECT_EQ(layout.top.size(), 0u);
    EXPECT_EQ(layout.bottom.size(), 0u);
}

TEST(PortLayoutResolver, OverrideSide_MoveInputToRight) {
    core::StringInterner interner;
    std::vector<bp2::PortDescriptor> inputs;
    inputs.push_back({interner.intern("v_in"), Domain::Electrical, bp2::Direction::Input, PortType::V});
    
    std::vector<bp2::PortDescriptor> outputs;
    
    std::vector<PortLayoutOverride> overrides;
    overrides.push_back({"v_in", bp2::PortLayoutSide::Right, std::nullopt});
    
    ResolvedLayout layout = resolve_port_layout(inputs, outputs, overrides, interner);
    
    EXPECT_EQ(layout.left.size(), 0u);
    EXPECT_EQ(layout.right.size(), 1u);
    EXPECT_EQ(layout.right[0].port_name, "v_in");
}

TEST(PortLayoutResolver, OverrideSide_MoveOutputToTop) {
    core::StringInterner interner;
    std::vector<bp2::PortDescriptor> outputs;
    outputs.push_back({interner.intern("rpm_out"), Domain::Mechanical, bp2::Direction::Output, PortType::RPM});
    
    std::vector<PortLayoutOverride> overrides;
    overrides.push_back({"rpm_out", bp2::PortLayoutSide::Top, std::nullopt});
    
    ResolvedLayout layout = resolve_port_layout({}, outputs, overrides, interner);
    
    EXPECT_EQ(layout.top.size(), 1u);
    EXPECT_EQ(layout.top[0].port_name, "rpm_out");
}

TEST(PortLayoutResolver, OverridePosition_Ordering) {
    core::StringInterner interner;
    std::vector<bp2::PortDescriptor> outputs;
    outputs.push_back({interner.intern("a"), Domain::Electrical, bp2::Direction::Output, PortType::V});
    outputs.push_back({interner.intern("b"), Domain::Electrical, bp2::Direction::Output, PortType::V});
    
    std::vector<PortLayoutOverride> overrides;
    overrides.push_back({"a", bp2::PortLayoutSide::Right, 1});
    overrides.push_back({"b", bp2::PortLayoutSide::Right, 0});
    
    ResolvedLayout layout = resolve_port_layout({}, outputs, overrides, interner);
    
    ASSERT_EQ(layout.right.size(), 2u);
    EXPECT_EQ(layout.right[0].port_name, "b");  // position 0
    EXPECT_EQ(layout.right[1].port_name, "a");  // position 1
}

TEST(PortLayoutResolver, MixedOverrideAndAuto) {
    core::StringInterner interner;
    std::vector<bp2::PortDescriptor> inputs;
    inputs.push_back({interner.intern("a"), Domain::Electrical, bp2::Direction::Input, PortType::V});
    inputs.push_back({interner.intern("b"), Domain::Electrical, bp2::Direction::Input, PortType::V});
    inputs.push_back({interner.intern("c"), Domain::Electrical, bp2::Direction::Input, PortType::V});
    
    std::vector<PortLayoutOverride> overrides;
    overrides.push_back({"c", bp2::PortLayoutSide::Left, 0});
    
    ResolvedLayout layout = resolve_port_layout(inputs, {}, overrides, interner);
    
    ASSERT_EQ(layout.left.size(), 3u);
    EXPECT_EQ(layout.left[0].port_name, "c");  // overridden to position 0
}

TEST(PortLayoutResolver, OrphanedOverride_Ignored) {
    core::StringInterner interner;
    std::vector<bp2::PortDescriptor> inputs;
    inputs.push_back({interner.intern("v_in"), Domain::Electrical, bp2::Direction::Input, PortType::V});
    
    std::vector<PortLayoutOverride> overrides;
    overrides.push_back({"nonexistent", bp2::PortLayoutSide::Top, std::nullopt});
    
    ResolvedLayout layout = resolve_port_layout(inputs, {}, overrides, interner);
    
    EXPECT_EQ(layout.top.size(), 0u);
    EXPECT_EQ(layout.left.size(), 1u);
}

TEST(PortLayoutResolver, PositionCollision_StableSortOrder) {
    core::StringInterner interner;
    std::vector<bp2::PortDescriptor> outputs;
    outputs.push_back({interner.intern("first"), Domain::Electrical, bp2::Direction::Output, PortType::V});
    outputs.push_back({interner.intern("second"), Domain::Electrical, bp2::Direction::Output, PortType::V});
    
    std::vector<PortLayoutOverride> overrides;
    overrides.push_back({"first", bp2::PortLayoutSide::Right, 0});
    overrides.push_back({"second", bp2::PortLayoutSide::Right, 0});
    
    ResolvedLayout layout = resolve_port_layout({}, outputs, overrides, interner);
    
    ASSERT_EQ(layout.right.size(), 2u);
    EXPECT_EQ(layout.right[0].port_name, "first");
    EXPECT_EQ(layout.right[1].port_name, "second");
}

TEST(PortLayoutResolver, PartialOverride_SideOnly) {
    core::StringInterner interner;
    std::vector<bp2::PortDescriptor> outputs;
    outputs.push_back({interner.intern("v_out"), Domain::Electrical, bp2::Direction::Output, PortType::V});
    
    std::vector<PortLayoutOverride> overrides;
    overrides.push_back({"v_out", bp2::PortLayoutSide::Bottom, std::nullopt});
    
    ResolvedLayout layout = resolve_port_layout({}, outputs, overrides, interner);
    
    EXPECT_EQ(layout.bottom.size(), 1u);
    EXPECT_EQ(layout.bottom[0].port_name, "v_out");
}

TEST(PortLayoutResolver, AllPortsMovedToOneSide) {
    core::StringInterner interner;
    std::vector<bp2::PortDescriptor> inputs;
    inputs.push_back({interner.intern("a"), Domain::Electrical, bp2::Direction::Input, PortType::V});
    inputs.push_back({interner.intern("b"), Domain::Electrical, bp2::Direction::Input, PortType::V});
    
    std::vector<bp2::PortDescriptor> outputs;
    outputs.push_back({interner.intern("c"), Domain::Electrical, bp2::Direction::Output, PortType::V});
    outputs.push_back({interner.intern("d"), Domain::Electrical, bp2::Direction::Output, PortType::V});
    
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
    core::StringInterner interner;
    std::vector<bp2::PortDescriptor> inputs;
    inputs.push_back({interner.intern("v_in"), Domain::Electrical, bp2::Direction::Input, PortType::V});
    
    std::vector<bp2::PortDescriptor> outputs;
    outputs.push_back({interner.intern("v_out"), Domain::Electrical, bp2::Direction::Output, PortType::V});
    
    ResolvedLayout layout = resolve_port_layout(inputs, outputs, {}, interner);
    
    EXPECT_EQ(layout.left.size(), 1u);
    EXPECT_EQ(layout.right.size(), 1u);
}

TEST(PortLayoutResolver, InOutPort_NoDuplicates) {
    core::StringInterner interner;
    auto bus_port = interner.intern("v");
    
    std::vector<bp2::PortDescriptor> inputs;
    inputs.push_back({bus_port, Domain::Electrical, bp2::Direction::InOut, PortType::V});
    
    std::vector<bp2::PortDescriptor> outputs;
    outputs.push_back({bus_port, Domain::Electrical, bp2::Direction::InOut, PortType::V});
    
    ResolvedLayout layout = resolve_port_layout(inputs, outputs, {}, interner);
    
    size_t total = layout.left.size() + layout.right.size() + 
                   layout.top.size() + layout.bottom.size();
    EXPECT_EQ(total, 1u);
    EXPECT_EQ(layout.left.size(), 1u);
    EXPECT_EQ(layout.left[0].port_name, "v");
    EXPECT_EQ(layout.left[0].logical_direction, bp2::Direction::InOut);
}

TEST(PortLayoutResolver, InOutPort_OverrideMovesToSide) {
    core::StringInterner interner;
    auto bus_port = interner.intern("v");
    
    std::vector<bp2::PortDescriptor> inputs;
    inputs.push_back({bus_port, Domain::Electrical, bp2::Direction::InOut, PortType::V});
    
    std::vector<bp2::PortDescriptor> outputs;
    outputs.push_back({bus_port, Domain::Electrical, bp2::Direction::InOut, PortType::V});
    
    std::vector<PortLayoutOverride> overrides;
    overrides.push_back({"v", bp2::PortLayoutSide::Top, std::nullopt});
    
    ResolvedLayout layout = resolve_port_layout(inputs, outputs, overrides, interner);
    
    EXPECT_EQ(layout.top.size(), 1u);
    EXPECT_EQ(layout.left.size(), 0u);
    EXPECT_EQ(layout.right.size(), 0u);
    EXPECT_EQ(layout.bottom.size(), 0u);
}

TEST(PortLayoutResolver, InOutPort_MixedWithRegularPorts) {
    core::StringInterner interner;
    auto in_port = interner.intern("ctrl");
    auto inout_port = interner.intern("v");
    auto out_port = interner.intern("status");
    
    std::vector<bp2::PortDescriptor> inputs;
    inputs.push_back({in_port, Domain::Electrical, bp2::Direction::Input, PortType::V});
    inputs.push_back({inout_port, Domain::Electrical, bp2::Direction::InOut, PortType::V});
    
    std::vector<bp2::PortDescriptor> outputs;
    outputs.push_back({inout_port, Domain::Electrical, bp2::Direction::InOut, PortType::V});
    outputs.push_back({out_port, Domain::Electrical, bp2::Direction::Output, PortType::V});
    
    ResolvedLayout layout = resolve_port_layout(inputs, outputs, {}, interner);
    
    EXPECT_EQ(layout.left.size(), 2u);
    EXPECT_EQ(layout.right.size(), 1u);
    EXPECT_EQ(layout.right[0].port_name, "status");
}

TEST(PortLayoutResolver, LogicalDirection_PreservedFromPortDefinition) {
    core::StringInterner interner;
    std::vector<bp2::PortDescriptor> inputs;
    inputs.push_back({interner.intern("a"), Domain::Electrical, bp2::Direction::Input, PortType::V});

    std::vector<bp2::PortDescriptor> outputs;
    outputs.push_back({interner.intern("b"), Domain::Mechanical, bp2::Direction::Output, PortType::RPM});

    ResolvedLayout layout = resolve_port_layout(inputs, outputs, {}, interner);

    ASSERT_EQ(layout.left.size(), 1u);
    ASSERT_EQ(layout.right.size(), 1u);
    EXPECT_EQ(layout.left[0].logical_direction, bp2::Direction::Input);
    EXPECT_EQ(layout.right[0].logical_direction, bp2::Direction::Output);
}

TEST(PortLayoutResolver, REGRESSION_StablePartitionPreservesAutoPortOrder) {
    core::StringInterner interner;
    std::vector<bp2::PortDescriptor> inputs;
    inputs.push_back({interner.intern("alpha"), Domain::Electrical, bp2::Direction::Input, PortType::V});
    inputs.push_back({interner.intern("beta"), Domain::Electrical, bp2::Direction::Input, PortType::V});
    inputs.push_back({interner.intern("gamma"), Domain::Electrical, bp2::Direction::Input, PortType::V});
    inputs.push_back({interner.intern("delta"), Domain::Electrical, bp2::Direction::Input, PortType::V});
    inputs.push_back({interner.intern("epsilon"), Domain::Electrical, bp2::Direction::Input, PortType::V});
    
    ResolvedLayout layout = resolve_port_layout(inputs, {}, {}, interner);
    
    ASSERT_EQ(layout.left.size(), 5u);
    EXPECT_EQ(layout.left[0].port_name, "alpha");
    EXPECT_EQ(layout.left[1].port_name, "beta");
    EXPECT_EQ(layout.left[2].port_name, "gamma");
    EXPECT_EQ(layout.left[3].port_name, "delta");
    EXPECT_EQ(layout.left[4].port_name, "epsilon");
}

TEST(PortLayoutResolver, REGRESSION_StablePartitionMixedHintedAndAuto) {
    core::StringInterner interner;
    std::vector<bp2::PortDescriptor> inputs;
    inputs.push_back({interner.intern("a1"), Domain::Electrical, bp2::Direction::Input, PortType::V});
    inputs.push_back({interner.intern("a2"), Domain::Electrical, bp2::Direction::Input, PortType::V});
    inputs.push_back({interner.intern("a3"), Domain::Electrical, bp2::Direction::Input, PortType::V});
    inputs.push_back({interner.intern("a4"), Domain::Electrical, bp2::Direction::Input, PortType::V});
    
    std::vector<PortLayoutOverride> overrides;
    overrides.push_back({"a3", bp2::PortLayoutSide::Left, 0});
    overrides.push_back({"a1", bp2::PortLayoutSide::Left, 1});
    
    ResolvedLayout layout = resolve_port_layout(inputs, {}, overrides, interner);
    
    ASSERT_EQ(layout.left.size(), 4u);
    EXPECT_EQ(layout.left[0].port_name, "a3");
    EXPECT_EQ(layout.left[1].port_name, "a1");
    EXPECT_EQ(layout.left[2].port_name, "a2");
    EXPECT_EQ(layout.left[3].port_name, "a4");
}