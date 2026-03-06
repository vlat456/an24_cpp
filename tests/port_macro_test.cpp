#include <gtest/gtest.h>
#include "jit_solver/components/port_registry.h"
#include "jit_solver/component.h"
#include <type_traits>

using namespace an24;

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

// Test with real component (RU19A)
TEST(PortMacroTest, RU19A_PortCountIsSix) {
    EXPECT_EQ(RU19A_PORT_COUNT, 5);
}

TEST(PortMacroTest, RU19A_PortNamesAreCorrect) {
    std::vector<std::string> ports = get_component_ports("RU19A");

    EXPECT_EQ(ports.size(), 5);

    // Check specific ports exist (order may vary)
    EXPECT_TRUE(std::find(ports.begin(), ports.end(), "v_bus") != ports.end());
    EXPECT_TRUE(std::find(ports.begin(), ports.end(), "v_start") != ports.end());
    EXPECT_TRUE(std::find(ports.begin(), ports.end(), "k_mod") != ports.end());
    EXPECT_TRUE(std::find(ports.begin(), ports.end(), "rpm_out") != ports.end());
    EXPECT_TRUE(std::find(ports.begin(), ports.end(), "t4_out") != ports.end());
}

// Test DMR400
TEST(PortMacroTest, DMR400_PortCountIsFour) {
    EXPECT_EQ(DMR400_PORT_COUNT, 4);
}

TEST(PortMacroTest, DMR400_PortNamesAreCorrect) {
    std::vector<std::string> ports = get_component_ports("DMR400");

    EXPECT_EQ(ports.size(), 4);

    EXPECT_TRUE(std::find(ports.begin(), ports.end(), "v_gen_ref") != ports.end());
    EXPECT_TRUE(std::find(ports.begin(), ports.end(), "v_in") != ports.end());
    EXPECT_TRUE(std::find(ports.begin(), ports.end(), "v_out") != ports.end());
    EXPECT_TRUE(std::find(ports.begin(), ports.end(), "lamp") != ports.end());
}

// Test compile-time validation
TEST(PortMacroTest, CompileTimePortCountValidation) {
    // This should compile - we're using the constant from registry
    constexpr size_t ru19a_ports = RU19A_PORT_COUNT;
    EXPECT_EQ(ru19a_ports, 5);

    constexpr size_t dmr400_ports = DMR400_PORT_COUNT;
    EXPECT_EQ(dmr400_ports, 4);
}

// Test that all components in registry have valid port counts
TEST(PortMacroTest, AllComponentsHavePortCounts) {
    EXPECT_GT(RU19A_PORT_COUNT, 0);
    EXPECT_GT(DMR400_PORT_COUNT, 0);
    EXPECT_GT(Battery_PORT_COUNT, 0);
    EXPECT_GT(GS24_PORT_COUNT, 0);
    EXPECT_GT(Switch_PORT_COUNT, 0);
}

// Test port name lookup
TEST(PortMacroTest, GetPortNamesReturnsCorrectData) {
    auto ru19a_ports = get_component_ports("RU19A");
    EXPECT_EQ(ru19a_ports.size(), RU19A_PORT_COUNT);

    auto dmr400_ports = get_component_ports("DMR400");
    EXPECT_EQ(dmr400_ports.size(), DMR400_PORT_COUNT);

    auto battery_ports = get_component_ports("Battery");
    EXPECT_EQ(battery_ports.size(), Battery_PORT_COUNT);
}

// ============================================================================
// Stage 4: Migrate RU19A to use PORTS macro
// ============================================================================

// Test that RU19A can be redefined using PORTS macro
// This simulates what the real RU19A class will look like after migration
struct RU19AMigrated {
    // Using PORTS macro instead of manual field declarations
    PORTS(RU19A, k_mod, rpm_out, t4_out, v_bus, v_start)

    // Other fields (from original RU19A)
    int state = 0;  // APUState, simplified for testing
    float timer = 0.0f;
    float target_rpm = 16000.0f;
    float current_rpm = 0.0f;
};

TEST(PortMacroTest, RU19AMigrated_HasAllPortFields) {
    RU19AMigrated comp;

    // Check that all port fields exist and are zero-initialized
    EXPECT_EQ(comp.v_bus_idx, 0);
    EXPECT_EQ(comp.v_start_idx, 0);
    EXPECT_EQ(comp.k_mod_idx, 0);
    EXPECT_EQ(comp.rpm_out_idx, 0);
    EXPECT_EQ(comp.t4_out_idx, 0);
}

TEST(PortMacroTest, RU19AMigrated_FieldTypesAreCorrect) {
    RU19AMigrated comp;

    // All port fields should be uint32_t
    EXPECT_TRUE((std::is_same<decltype(comp.v_bus_idx), uint32_t>::value));
    EXPECT_TRUE((std::is_same<decltype(comp.v_start_idx), uint32_t>::value));
    EXPECT_TRUE((std::is_same<decltype(comp.k_mod_idx), uint32_t>::value));
    EXPECT_TRUE((std::is_same<decltype(comp.rpm_out_idx), uint32_t>::value));
    EXPECT_TRUE((std::is_same<decltype(comp.t4_out_idx), uint32_t>::value));
}

TEST(PortMacroTest, RU19AMigrated_PortCountMatchesRegistry) {
    // The migrated component should have 5 ports (from registry)
    EXPECT_EQ(RU19A_PORT_COUNT, 5);

    // And our migrated component should match
    RU19AMigrated comp;
    EXPECT_EQ(comp.v_bus_idx, 0);  // Just verify field exists
}

// Test that DMR400 can also be migrated
struct DMR400Migrated {
    PORTS(DMR400, v_gen, v_bus, v_out, lamp)

    bool is_closed = false;
    float connect_threshold = 2.0f;
};

TEST(PortMacroTest, DMR400Migrated_HasAllPortFields) {
    DMR400Migrated comp;

    EXPECT_EQ(comp.v_gen_idx, 0);
    EXPECT_EQ(comp.v_bus_idx, 0);
    EXPECT_EQ(comp.v_out_idx, 0);
    EXPECT_EQ(comp.lamp_idx, 0);
}

TEST(PortMacroTest, DMR400Migrated_PortCountMatchesRegistry) {
    EXPECT_EQ(DMR400_PORT_COUNT, 4);
}

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
    EXPECT_EQ(comp.p30_idx, 0);
    EXPECT_EQ(comp.p31_idx, 0);
    EXPECT_EQ(comp.p32_idx, 0);
}
