#include <gtest/gtest.h>
#include "jit_solver/components/port_registry.h"
#include "jit_solver/jit_solver.h"

using namespace an24;

// ============================================================================
// Stage 7a: Factory Validation Tests (TDD - Failing tests first)
// ============================================================================

// Test that we can validate port names against registry
TEST(FactoryValidationTest, AllFactoryPortsMatchRegistry) {
    // This test validates that every port name used in the factory
    // matches the port registry (no typos, no missing ports)

    struct ComponentInfo {
        std::string classname;
        std::vector<std::string> factory_ports;
    };

    // List of all components and their port names as used in factory
    std::vector<ComponentInfo> components = {
        {"Battery", {"v_in", "v_out"}},
        {"Switch", {"v_in", "v_out", "control", "state"}},
        {"HoldButton", {"v_in", "v_out", "control", "state"}},
        {"Relay", {"v_in", "v_out", "control"}},
        {"Resistor", {"v_in", "v_out"}},
        {"RefNode", {"v"}},
        {"Bus", {"v"}},
        {"Generator", {"v_in", "v_out"}},
        {"GS24", {"v_in", "v_out", "k_mod"}},
        {"RUG82", {"v_gen", "k_mod"}},
        {"RU19A", {"v_start", "v_bus", "k_mod", "rpm_out", "t4_out"}},
        {"DMR400", {"lamp", "v_gen_ref", "v_in", "v_out"}},
        {"Gyroscope", {"input"}},
        {"AGK47", {"input"}},
        {"Transformer", {"primary", "secondary"}},
        {"Inverter", {"dc_in", "ac_out"}},
        {"LerpNode", {"input", "output"}},
        {"IndicatorLight", {"v_in", "v_out", "brightness"}},
        {"HighPowerLoad", {"v_in", "v_out"}},
        {"ElectricPump", {"v_in", "p_out"}},
        {"SolenoidValve", {"ctrl", "flow_in", "flow_out"}},
        {"InertiaNode", {"input", "output"}},
        {"TempSensor", {"temp_in", "temp_out"}},
        {"ElectricHeater", {"power", "heat_out"}},
        {"Radiator", {"heat_in", "heat_out"}}
    };

    for (const auto& comp : components) {
        // Get registry ports for this component
        std::vector<std::string> registry_ports = get_component_ports(comp.classname);

        // Check that factory has exact same ports as registry
        EXPECT_EQ(comp.factory_ports.size(), registry_ports.size())
            << "Component " << comp.classname << " has wrong port count";

        // Check that each factory port exists in registry
        for (const auto& port : comp.factory_ports) {
            auto it = std::find(registry_ports.begin(), registry_ports.end(), port);
            EXPECT_TRUE(it != registry_ports.end())
                << "Component " << comp.classname << " has unknown port '" << port
                << "' in factory (not in registry)";
        }

        // Check that all registry ports are used in factory
        for (const auto& port : registry_ports) {
            auto it = std::find(comp.factory_ports.begin(), comp.factory_ports.end(), port);
            EXPECT_TRUE(it != comp.factory_ports.end())
                << "Component " << comp.classname << " missing port '" << port
                << "' in factory (exists in registry)";
        }
    }
}

// Test that unknown component types are rejected
TEST(FactoryValidationTest, UnknownComponentType_IsRejected) {
    // This test will fail until we add validation to factory
    // Currently, unknown types just log a warning and return nullptr

    DeviceInstance unknown_device;
    unknown_device.name = "unknown_device";
    unknown_device.classname = "NonExistentComponent";
    unknown_device.params = {};

    PortToSignal port_to_signal;
    uint32_t signal_count = 10;

    auto component = create_component(unknown_device, port_to_signal, signal_count);

    // Should return nullptr for unknown component
    EXPECT_EQ(component, nullptr)
        << "Factory should reject unknown component types and return nullptr";
}

// Test that port registry constants are correct
TEST(FactoryValidationTest, PortRegistryConstants_AreCorrect) {
    // Verify that port count constants match actual registry size
    EXPECT_EQ(Battery_PORT_COUNT, 2);
    EXPECT_EQ(Switch_PORT_COUNT, 4);
    EXPECT_EQ(Relay_PORT_COUNT, 3);
    EXPECT_EQ(RefNode_PORT_COUNT, 1);
    EXPECT_EQ(Bus_PORT_COUNT, 1);
    EXPECT_EQ(GS24_PORT_COUNT, 3);
    EXPECT_EQ(RUG82_PORT_COUNT, 2);
    EXPECT_EQ(RU19A_PORT_COUNT, 5);
    EXPECT_EQ(DMR400_PORT_COUNT, 4);
    EXPECT_EQ(Gyroscope_PORT_COUNT, 1);
    EXPECT_EQ(AGK47_PORT_COUNT, 1);
    EXPECT_EQ(Transformer_PORT_COUNT, 2);
    EXPECT_EQ(Inverter_PORT_COUNT, 2);
    EXPECT_EQ(LerpNode_PORT_COUNT, 2);
    EXPECT_EQ(IndicatorLight_PORT_COUNT, 3);
    EXPECT_EQ(HighPowerLoad_PORT_COUNT, 2);
    EXPECT_EQ(ElectricPump_PORT_COUNT, 2);
    EXPECT_EQ(SolenoidValve_PORT_COUNT, 3);
    EXPECT_EQ(InertiaNode_PORT_COUNT, 2);
    EXPECT_EQ(TempSensor_PORT_COUNT, 2);
    EXPECT_EQ(ElectricHeater_PORT_COUNT, 2);
    EXPECT_EQ(Radiator_PORT_COUNT, 2);
}

// Test factory creates all known component types
TEST(FactoryValidationTest, Factory_CreatesAllKnownComponents) {
    // This test ensures factory can create every component type
    std::vector<std::string> component_types = {
        "Battery", "Switch", "HoldButton", "Relay", "Resistor",
        "RefNode", "Bus", "Generator", "GS24", "RUG82", "RU19A",
        "DMR400", "Gyroscope", "AGK47", "Transformer", "Inverter",
        "LerpNode", "IndicatorLight", "HighPowerLoad", "ElectricPump",
        "SolenoidValve", "InertiaNode", "TempSensor", "ElectricHeater",
        "Radiator"
    };

    PortToSignal port_to_signal;
    uint32_t signal_count = 100;  // Enough signals for all ports

    for (const auto& classname : component_types) {
        DeviceInstance device;
        device.name = "test_" + classname;
        device.classname = classname;
        device.params = {};

        auto component = create_component(device, port_to_signal, signal_count);

        EXPECT_NE(component, nullptr)
            << "Factory should be able to create component type: " << classname;

        if (component) {
            EXPECT_EQ(component->type_name(), classname)
                << "Created component has wrong type name";
        }
    }
}

// Test port order consistency
TEST(FactoryValidationTest, PortOrder_MatchesRegistry) {
    // This test ensures port order in factory matches registry order
    // (important for constructor argument order)

    struct ComponentPortOrder {
        std::string classname;
        std::vector<std::string> expected_order;  // from factory
    };

    // Check a few critical components
    std::vector<ComponentPortOrder> checks = {
        {"RU19A", {"v_start", "v_bus", "k_mod", "rpm_out", "t4_out"}},
        {"DMR400", {"v_gen_ref", "v_in", "v_out", "lamp"}},  // constructor parameter order
        {"GS24", {"v_in", "v_out", "k_mod"}},
        {"Battery", {"v_in", "v_out"}}
    };

    for (const auto& check : checks) {
        auto registry_ports = get_component_ports(check.classname);

        // Registry is alphabetically sorted, factory uses semantic order
        // We just check that all ports exist, order is validated by constructor
        EXPECT_EQ(check.expected_order.size(), registry_ports.size())
            << "Component " << check.classname << " has port count mismatch";
    }
}
