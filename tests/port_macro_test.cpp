#include <gtest/gtest.h>
#include "jit_solver/components/port_registry.h"
#include <type_traits>

using namespace an24;

// Test with real component (RU19A)
TEST(PortMacroTest, RU19A_PortCountIsSix) {
    EXPECT_EQ(RU19A_PORT_COUNT, 6);
}

TEST(PortMacroTest, RU19A_PortNamesAreCorrect) {
    std::vector<std::string> ports = get_component_ports("RU19A");

    EXPECT_EQ(ports.size(), 6);

    // Check specific ports exist (order may vary)
    EXPECT_TRUE(std::find(ports.begin(), ports.end(), "v_bus") != ports.end());
    EXPECT_TRUE(std::find(ports.begin(), ports.end(), "v_start") != ports.end());
    EXPECT_TRUE(std::find(ports.begin(), ports.end(), "k_mod") != ports.end());
    EXPECT_TRUE(std::find(ports.begin(), ports.end(), "rpm_out") != ports.end());
    EXPECT_TRUE(std::find(ports.begin(), ports.end(), "t4_out") != ports.end());
    EXPECT_TRUE(std::find(ports.begin(), ports.end(), "v_gen_mon") != ports.end());
}

// Test DMR400
TEST(PortMacroTest, DMR400_PortCountIsFour) {
    EXPECT_EQ(DMR400_PORT_COUNT, 4);
}

TEST(PortMacroTest, DMR400_PortNamesAreCorrect) {
    std::vector<std::string> ports = get_component_ports("DMR400");

    EXPECT_EQ(ports.size(), 4);

    EXPECT_TRUE(std::find(ports.begin(), ports.end(), "v_gen") != ports.end());
    EXPECT_TRUE(std::find(ports.begin(), ports.end(), "v_bus") != ports.end());
    EXPECT_TRUE(std::find(ports.begin(), ports.end(), "v_out") != ports.end());
    EXPECT_TRUE(std::find(ports.begin(), ports.end(), "lamp") != ports.end());
}

// Test compile-time validation
TEST(PortMacroTest, CompileTimePortCountValidation) {
    // This should compile - we're using the constant from registry
    constexpr size_t ru19a_ports = RU19A_PORT_COUNT;
    EXPECT_EQ(ru19a_ports, 6);

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

// Test PORTS macro doesn't exist yet (should fail compilation)
// This test is commented out until we implement the macro
// TEST(PortMacroTest, PORTS_MacroCompiles) {
//     // This will fail to compile until PORTS macro is defined
//     struct TestComp {
//         PORTS(TestComp, v_bus, v_start)
//     };
//     TestComp c;
//     EXPECT_EQ(c.v_bus_idx, 0);
// }
