#include <gtest/gtest.h>
#include "core/model/component_kind.h"
#include "core/solvers/common/port_registry.h"
#include "core/solvers/jit/component.h"
#include <type_traits>


// Test component using PORTS macro
struct TestComponentWithPorts {
    PORTS(TestComponent, v_bus, v_start, k_mod)
};

// Test PORTS macro functionality
TEST(PortMacroTest, ComponentHasCorrectFieldTypes) {
    TestComponentWithPorts comp;

    // Check that fields are uint32_t
    EXPECT_TRUE((std::is_same<decltype(comp.v_bus_idx), uint32_t>::value));
    EXPECT_TRUE((std::is_same<decltype(comp.v_start_idx), uint32_t>::value));
    EXPECT_TRUE((std::is_same<decltype(comp.k_mod_idx), uint32_t>::value));
}

TEST(PortMacroTest, ComponentFieldsAreZeroInitialized) {
    TestComponentWithPorts comp;

    EXPECT_EQ(comp.v_bus_idx, 0);
    EXPECT_EQ(comp.v_start_idx, 0);
    EXPECT_EQ(comp.k_mod_idx, 0);
}

// Test compile-time validation
TEST(PortMacroTest, CompileTimePortCountValidation) {
    // This should compile - we're using the constant from registry
    constexpr size_t es_ports = ElectricalSource_PORT_COUNT;
    EXPECT_EQ(es_ports, 2);
}

// Test that all components in registry have valid port counts
TEST(PortMacroTest, AllComponentsHavePortCounts) {
    EXPECT_GT(ElectricalSource_PORT_COUNT, 0);
    EXPECT_GT(Switch_PORT_COUNT, 0);
}

// Test port name lookup
TEST(PortMacroTest, GetPortNamesReturnsCorrectData) {
    auto es_ports = get_component_ports(ComponentKind::ElectricalSource);
    EXPECT_EQ(es_ports.size(), ElectricalSource_PORT_COUNT);
}

// ============================================================================
// Stage 4: Test PORTS macro with many ports (up to 32)
// ============================================================================

// Test PORTS macro with many ports (up to 32)
struct ComponentWith32Ports {
    PORTS(ComponentWith32,
           p1, p2, p3, p4, p5, p6, p7, p8,
           p9, p10, p11, p12, p13, p14, p15, p16,
           p17, p18, p19, p20, p21, p22, p23, p24,
           p25, p26, p27, p28, p29, p30, p31, p32)
};

TEST(PortMacroTest, PORTS_Supports32Ports) {
    ComponentWith32Ports comp;

    // Verify first and last few ports exist
    EXPECT_EQ(comp.p1_idx, 0);
    EXPECT_EQ(comp.p2_idx, 0);
    // Verify p21-p24 are correctly declared (previously skipped in PORTS_25-32)
    EXPECT_EQ(comp.p21_idx, 0);
    EXPECT_EQ(comp.p22_idx, 0);
    EXPECT_EQ(comp.p23_idx, 0);
    EXPECT_EQ(comp.p24_idx, 0);
    // Verify p25-p32 range (previously had missing ## token-pasting bugs)
    EXPECT_EQ(comp.p25_idx, 0);
    EXPECT_EQ(comp.p26_idx, 0);
    EXPECT_EQ(comp.p27_idx, 0);
    EXPECT_EQ(comp.p28_idx, 0);
    EXPECT_EQ(comp.p29_idx, 0);
    EXPECT_EQ(comp.p30_idx, 0);
    EXPECT_EQ(comp.p31_idx, 0);
    EXPECT_EQ(comp.p32_idx, 0);
}

// Verify PORTS_25 specifically declares all 25 ports including p21-p24
struct ComponentWith25Ports {
    PORTS(ComponentWith25,
           a1, a2, a3, a4, a5, a6, a7, a8,
           a9, a10, a11, a12, a13, a14, a15, a16,
           a17, a18, a19, a20, a21, a22, a23, a24,
           a25)
};

TEST(PortMacroTest, PORTS25_DeclaresAllPorts) {
    ComponentWith25Ports comp;
    // These were previously silently dropped by the broken PORTS_25 macro
    EXPECT_EQ(comp.a21_idx, 0);
    EXPECT_EQ(comp.a22_idx, 0);
    EXPECT_EQ(comp.a23_idx, 0);
    EXPECT_EQ(comp.a24_idx, 0);
    EXPECT_EQ(comp.a25_idx, 0);
}
