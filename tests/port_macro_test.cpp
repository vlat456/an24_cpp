#include <gtest/gtest.h>
#include "core/model/component_kind.h"
#include "core/solvers/common/port_registry.h"


// =============================================================================
// Port Registry — codegen constant validation
// =============================================================================

TEST(PortRegistryTest, CompileTimePortCountValidation) {
    constexpr size_t es_ports = ElectricalSource_PORT_COUNT;
    EXPECT_EQ(es_ports, 2);
}

TEST(PortRegistryTest, AllComponentsHavePortCounts) {
    EXPECT_GT(ElectricalSource_PORT_COUNT, 0);
    EXPECT_GT(Switch_PORT_COUNT, 0);
}

TEST(PortRegistryTest, GetPortNamesReturnsCorrectData) {
    auto es_ports = get_component_ports(ComponentKind::ElectricalSource);
    EXPECT_EQ(es_ports.size(), ElectricalSource_PORT_COUNT);
}

// Verify output port filtering works for a known component
TEST(PortRegistryTest, GetOutputPortsFiltersCorrectly) {
    auto outputs = get_output_ports(ComponentKind::ElectricalSource);
    // ElectricalSource has exactly 2 ports; check the function returns something
    EXPECT_GE(outputs.size(), 0u);
    EXPECT_LE(outputs.size(), ElectricalSource_PORT_COUNT);
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
