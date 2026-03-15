#include <gtest/gtest.h>
#include "jit_solver/components/port_registry.h"
#include "jit_solver/jit_solver.h"

// ============================================================================
// Factory Validation Tests
// Validates that the JIT component factory (build_systems_dev) correctly
// creates all known component types and that port registries are consistent.
// ============================================================================

namespace {

/// Helper: build a single-component system via build_systems_dev.
/// Creates a DeviceInstance with the given classname and all its registry ports,
/// plus a ground RefNode so signal allocation succeeds.
BuildResult build_single_component(const std::string& classname,
                                   const std::unordered_map<std::string, std::string>& params = {}) {
    auto ports = get_component_ports(classname);

    DeviceInstance dev;
    dev.name = "test_" + classname;
    dev.classname = classname;
    dev.params = params;
    for (const auto& port_name : ports) {
        dev.ports[port_name] = Port{PortDirection::InOut, PortType::Any};
    }

    // Ground reference so the system has at least one fixed signal
    DeviceInstance gnd;
    gnd.name = "gnd";
    gnd.classname = "RefNode";
    gnd.params = {{"value", "0"}};
    gnd.ports["v"] = Port{PortDirection::Out, PortType::V};

    std::vector<DeviceInstance> devices = {dev, gnd};
    std::vector<std::pair<std::string, std::string>> connections;

    return build_systems_dev(devices, connections);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Test that all known component types can be built without throwing
// ---------------------------------------------------------------------------
TEST(FactoryValidationTest, Factory_CreatesAllKnownComponents) {
    std::vector<std::string> component_types = {
        "Battery", "Switch", "HoldButton", "Relay", "Resistor",
        "RefNode", "Bus", "Generator", "GS24", "RUG82", "RU19A",
        "DMR400", "Gyroscope", "AGK47", "Transformer", "Inverter",
        "LerpNode", "IndicatorLight", "HighPowerLoad", "ElectricPump",
        "SolenoidValve", "InertiaNode", "TempSensor", "ElectricHeater",
        "Radiator", "Comparator", "Load", "AZS",
        "AND", "OR", "NOT", "NAND", "XOR",
        "Add", "Subtract", "Multiply", "Divide",
        "Splitter", "Merger",
        "BlueprintInput", "BlueprintOutput",
        "Integrator", "SlewRate", "AsymSlewRate",
        "FastTMO", "AsymTMO", "TimeDelay", "Monostable", "SampleHold",
        "LUT", "Voltmeter",
        "Any_V_to_Bool", "Positive_V_to_Bool",
        "P", "PI", "PD", "PID",
        "GreaterEq", "LesserEq", "Greater", "Lesser"
    };

    for (const auto& classname : component_types) {
        EXPECT_NO_THROW({
            auto result = build_single_component(classname);
            // The device must appear in the built system
            std::string dev_name = "test_" + classname;
            EXPECT_TRUE(result.devices.count(dev_name) > 0)
                << "Factory did not create device for: " << classname;
        }) << "Factory threw for component type: " << classname;
    }
}

// ---------------------------------------------------------------------------
// Test that unknown component types are rejected with an exception
// ---------------------------------------------------------------------------
TEST(FactoryValidationTest, UnknownComponentType_Throws) {
    DeviceInstance unknown;
    unknown.name = "unknown_device";
    unknown.classname = "NonExistentComponent";
    unknown.ports["dummy"] = Port{PortDirection::InOut, PortType::Any};

    DeviceInstance gnd;
    gnd.name = "gnd";
    gnd.classname = "RefNode";
    gnd.params = {{"value", "0"}};
    gnd.ports["v"] = Port{PortDirection::Out, PortType::V};

    std::vector<DeviceInstance> devices = {unknown, gnd};
    std::vector<std::pair<std::string, std::string>> connections;

    EXPECT_THROW(build_systems_dev(devices, connections), std::runtime_error);
}

// ---------------------------------------------------------------------------
// Test that port registry constants match get_component_ports() size
// ---------------------------------------------------------------------------
TEST(FactoryValidationTest, PortRegistryConstants_AreCorrect) {
    EXPECT_EQ(Battery_PORT_COUNT, get_component_ports("Battery").size());
    EXPECT_EQ(Switch_PORT_COUNT, get_component_ports("Switch").size());
    EXPECT_EQ(Relay_PORT_COUNT, get_component_ports("Relay").size());
    EXPECT_EQ(RefNode_PORT_COUNT, get_component_ports("RefNode").size());
    EXPECT_EQ(Bus_PORT_COUNT, get_component_ports("Bus").size());
    EXPECT_EQ(GS24_PORT_COUNT, get_component_ports("GS24").size());
    EXPECT_EQ(RUG82_PORT_COUNT, get_component_ports("RUG82").size());
    EXPECT_EQ(RU19A_PORT_COUNT, get_component_ports("RU19A").size());
    EXPECT_EQ(DMR400_PORT_COUNT, get_component_ports("DMR400").size());
    EXPECT_EQ(Gyroscope_PORT_COUNT, get_component_ports("Gyroscope").size());
    EXPECT_EQ(AGK47_PORT_COUNT, get_component_ports("AGK47").size());
    EXPECT_EQ(Transformer_PORT_COUNT, get_component_ports("Transformer").size());
    EXPECT_EQ(Inverter_PORT_COUNT, get_component_ports("Inverter").size());
    EXPECT_EQ(LerpNode_PORT_COUNT, get_component_ports("LerpNode").size());
    EXPECT_EQ(IndicatorLight_PORT_COUNT, get_component_ports("IndicatorLight").size());
    EXPECT_EQ(HighPowerLoad_PORT_COUNT, get_component_ports("HighPowerLoad").size());
    EXPECT_EQ(ElectricPump_PORT_COUNT, get_component_ports("ElectricPump").size());
    EXPECT_EQ(SolenoidValve_PORT_COUNT, get_component_ports("SolenoidValve").size());
    EXPECT_EQ(InertiaNode_PORT_COUNT, get_component_ports("InertiaNode").size());
    EXPECT_EQ(TempSensor_PORT_COUNT, get_component_ports("TempSensor").size());
    EXPECT_EQ(ElectricHeater_PORT_COUNT, get_component_ports("ElectricHeater").size());
    EXPECT_EQ(Radiator_PORT_COUNT, get_component_ports("Radiator").size());
    EXPECT_EQ(Comparator_PORT_COUNT, get_component_ports("Comparator").size());
    EXPECT_EQ(AZS_PORT_COUNT, get_component_ports("AZS").size());
    EXPECT_EQ(AND_PORT_COUNT, get_component_ports("AND").size());
    EXPECT_EQ(OR_PORT_COUNT, get_component_ports("OR").size());
    EXPECT_EQ(NOT_PORT_COUNT, get_component_ports("NOT").size());
}

// ---------------------------------------------------------------------------
// Test that every port listed in get_component_ports() is recognized by
// string_to_port_name() (i.e. no stale entries in the registry)
// ---------------------------------------------------------------------------
TEST(FactoryValidationTest, AllRegistryPortsAreRecognized) {
    // Collect all component types from the registry
    std::vector<std::string> types = {
        "Battery", "Switch", "HoldButton", "Relay", "Resistor",
        "RefNode", "Bus", "Generator", "GS24", "RUG82", "RU19A",
        "DMR400", "Gyroscope", "AGK47", "Transformer", "Inverter",
        "LerpNode", "IndicatorLight", "HighPowerLoad", "ElectricPump",
        "SolenoidValve", "InertiaNode", "TempSensor", "ElectricHeater",
        "Radiator", "Comparator", "Load", "AZS",
        "AND", "OR", "NOT", "NAND", "XOR",
        "Add", "Subtract", "Multiply", "Divide",
        "Splitter", "Merger",
        "BlueprintInput", "BlueprintOutput",
        "Integrator", "SlewRate", "AsymSlewRate",
        "FastTMO", "AsymTMO", "TimeDelay", "Monostable", "SampleHold",
        "LUT", "Voltmeter",
        "Any_V_to_Bool", "Positive_V_to_Bool",
        "P", "PI", "PD", "PID",
        "GreaterEq", "LesserEq", "Greater", "Lesser"
    };

    for (const auto& type : types) {
        auto ports = get_component_ports(type);
        EXPECT_FALSE(ports.empty())
            << "Component '" << type << "' has no ports in registry";

        for (const auto& port : ports) {
            auto port_enum = string_to_port_name(port);
            EXPECT_TRUE(port_enum.has_value())
                << "Port '" << port << "' of component '" << type
                << "' is not recognized by string_to_port_name()";
        }
    }
}

// ---------------------------------------------------------------------------
// Test that building a component with an unknown port name throws
// (regression test for the std::abort -> std::runtime_error fix)
// ---------------------------------------------------------------------------
TEST(FactoryValidationTest, UnknownPortName_Throws) {
    DeviceInstance dev;
    dev.name = "test_battery";
    dev.classname = "Battery";
    // "bogus_port" does not exist in Battery's port set
    dev.ports["v_in"] = Port{PortDirection::In, PortType::V};
    dev.ports["v_out"] = Port{PortDirection::Out, PortType::V};
    dev.ports["bogus_port"] = Port{PortDirection::Out, PortType::Any};

    DeviceInstance gnd;
    gnd.name = "gnd";
    gnd.classname = "RefNode";
    gnd.params = {{"value", "0"}};
    gnd.ports["v"] = Port{PortDirection::Out, PortType::V};

    std::vector<DeviceInstance> devices = {dev, gnd};
    std::vector<std::pair<std::string, std::string>> connections;

    EXPECT_THROW(build_systems_dev(devices, connections), std::runtime_error);
}
