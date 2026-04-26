#include <gtest/gtest.h>
#include "core/model/component_kind.h"
#include "core/solvers/common/port_registry.h"


// =============================================================================
// Port Registry — codegen constant validation
// =============================================================================

TEST(PortRegistryTest, CompileTimePortCountValidation) {
    constexpr size_t es_ports = port_count(ComponentKind::ElectricalSource);
    EXPECT_EQ(es_ports, 2);
}

TEST(PortRegistryTest, AllComponentsHavePortCounts) {
    // Every valid ComponentKind (excluding Unknown/_COUNT) must have accessible metadata.
    // Spot-check a representative sample across the alphabet.
    EXPECT_GT(port_count(ComponentKind::AND), 0);
    EXPECT_GT(port_count(ComponentKind::Accumulator), 0);
    EXPECT_GT(port_count(ComponentKind::ElectricalSource), 0);
    EXPECT_GT(port_count(ComponentKind::Generator), 0);
    EXPECT_GT(port_count(ComponentKind::Relay), 0);
    EXPECT_GT(port_count(ComponentKind::Switch), 0);
    EXPECT_GT(port_count(ComponentKind::VariableConductance), 0);
    EXPECT_GT(port_count(ComponentKind::XOR), 0);
}

TEST(PortRegistryTest, GetPortNamesReturnsCorrectData) {
    auto es_ports = get_component_ports(ComponentKind::ElectricalSource);
    EXPECT_EQ(es_ports.size(), port_count(ComponentKind::ElectricalSource));
}

// Verify output port filtering works for a known component.
// ElectricalSource has ports {v_out, v_in}; only v_out is Output direction.
TEST(PortRegistryTest, GetOutputPortsFiltersCorrectly) {
    auto outputs = get_output_ports(ComponentKind::ElectricalSource);
    EXPECT_EQ(outputs.size(), 1u);
    EXPECT_EQ(outputs[0], "v_out");
}

// ComponentKind — parse round-trip
TEST(PortRegistryTest, ParseComponentKindRoundTrip) {
    for (size_t i = 0; i < static_cast<size_t>(ComponentKind::Unknown); ++i) {
        auto kind = static_cast<ComponentKind>(i);
        auto name = component_kind_classname(kind);
        auto parsed = parse_component_kind(name);
        ASSERT_TRUE(parsed.has_value()) << "Failed to parse: " << name;
        EXPECT_EQ(*parsed, kind) << "Round-trip mismatch for: " << name;
    }
}

// has_component_metadata — Unknown sentinel has no metadata
TEST(PortRegistryTest, UnknownHasNoMetadata) {
    EXPECT_FALSE(has_component_metadata(ComponentKind::Unknown));
    EXPECT_FALSE(has_component_metadata(ComponentKind::_COUNT));
}
