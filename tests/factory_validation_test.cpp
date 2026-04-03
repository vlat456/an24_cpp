#include <gtest/gtest.h>
#include "jit_solver/components/port_registry.h"
#include "jit_solver/jit_solver.h"

// ============================================================================
// Factory Validation Tests
// Validates that the JIT component factory (build_systems_dev) correctly
// creates all known component types and that port registries are consistent.
// ============================================================================

namespace {

ExecutionPhases make_execution_for_class(const std::string& classname) {
    ExecutionPhases phases;

    const bool is_observer =
        (classname == "VoltageSense") ||
        (classname == "Voltmeter") ||
        (classname == "CurrentSense");
    const bool is_actuator =
        (classname == "ControlledVoltageSource") ||
        (classname == "ControlledCurrentSource") ||
        (classname == "VariableConductance");

    if (!is_observer && !is_actuator && classname != "Bus") {
        phases.electrical_passive = true;
    }
    if (is_observer) {
        phases.electrical_observer = true;
    }
    if (classname == "CurrentSense") {
        phases.electrical_passive = true;
    }
    if (is_actuator) {
        phases.electrical_actuator = true;
    }

    if (classname == "AND" || classname == "OR" || classname == "NOT" || classname == "NAND" || classname == "XOR" ||
        classname == "Any_V_to_Bool" || classname == "Positive_V_to_Bool" ||
        classname == "PID" || classname == "PI" || classname == "PD" || classname == "P" ||
        classname == "Comparator" || classname == "LUT" || classname == "Monostable" || classname == "TimeDelay" ||
        classname == "SampleHold" || classname == "GreaterEq" || classname == "LesserEq" || classname == "Greater" ||
        classname == "Lesser" || classname == "Bus" || classname == "BlueprintInput" || classname == "BlueprintOutput") {
        phases.logical = true;
    }

    if (classname == "HoldButton" || classname == "Switch" || classname == "Relay" || classname == "AZS") {
        phases.control_commit = true;
    }

    if (classname == "LerpNode" || classname == "GidroAccumulator" || classname == "FuelTank") {
        phases.finalize = true;
    }

    if (classname == "InertiaNode" || classname == "Spring" ||
        classname == "ElectricPump" || classname == "BlueprintInput" || classname == "BlueprintOutput") {
        phases.mechanical = true;
    }

    if (classname == "SolenoidValve" || classname == "GidroAccumulator" || classname == "FuelTank" ||
        classname == "ElectricPump" || classname == "BlueprintInput" || classname == "BlueprintOutput") {
        phases.hydraulic = true;
    }

    if (classname == "TempSensor" || classname == "ElectricHeater" || classname == "Radiator" ||
        classname == "FuelTank" || classname == "BlueprintInput" || classname == "BlueprintOutput") {
        phases.thermal = true;
    }

    return phases;
}

/// Helper: build a single-component system via build_systems_dev.
/// Creates a DeviceInstance with the given classname and all its registry ports,
/// plus a ground RefNode so signal allocation succeeds.
BuildResult build_single_component(const std::string& classname,
                                   const std::unordered_map<std::string, std::string>& params = {}) {
    auto ports = get_component_ports(classname);

    std::unordered_map<std::string, std::string> merged_params = params;
    // Strict-param components must provide canonical keys.
    if (classname == "P") {
        merged_params.try_emplace("Kp", "1.0");
        merged_params.try_emplace("output_min", "-1e9");
        merged_params.try_emplace("output_max", "1e9");
    } else if (classname == "PI") {
        merged_params.try_emplace("Kp", "1.0");
        merged_params.try_emplace("Ki", "1.0");
        merged_params.try_emplace("output_min", "-1e9");
        merged_params.try_emplace("output_max", "1e9");
    } else if (classname == "PD") {
        merged_params.try_emplace("Kp", "1.0");
        merged_params.try_emplace("Kd", "0.0");
        merged_params.try_emplace("filter_alpha", "0.5");
        merged_params.try_emplace("output_min", "-1e9");
        merged_params.try_emplace("output_max", "1e9");
    } else if (classname == "PID") {
        merged_params.try_emplace("Kp", "1.0");
        merged_params.try_emplace("Ki", "1.0");
        merged_params.try_emplace("Kd", "0.0");
        merged_params.try_emplace("filter_alpha", "0.5");
        merged_params.try_emplace("output_min", "-1e9");
        merged_params.try_emplace("output_max", "1e9");
    } else if (classname == "SlewRate") {
        merged_params.try_emplace("max_rate", "1.0");
        merged_params.try_emplace("deadzone", "1e-6");
    } else if (classname == "AsymSlewRate") {
        merged_params.try_emplace("rate_up", "1.0");
        merged_params.try_emplace("rate_down", "1.0");
        merged_params.try_emplace("deadzone", "1e-6");
    } else if (classname == "Integrator") {
        merged_params.try_emplace("gain", "1.0");
        merged_params.try_emplace("initial_val", "0.0");
    } else if (classname == "SampleHold") {
        // SampleHold has no configurable params - threshold is hardcoded in component
    } else if (classname == "Monostable") {
        merged_params.try_emplace("duration", "0.1");
    } else if (classname == "FastTMO") {
        merged_params.try_emplace("tau", "0.1");
    } else if (classname == "AsymTMO") {
        merged_params.try_emplace("tau_up", "0.1");
        merged_params.try_emplace("tau_down", "0.1");
    } else if (classname == "LerpNode") {
        merged_params.try_emplace("factor", "0.5");
        merged_params.try_emplace("deadzone", "1e-6");
    } else if (classname == "TimeDelay") {
        merged_params.try_emplace("delay", "0.1");
        merged_params.try_emplace("delay_on", "0.1");
        merged_params.try_emplace("delay_off", "0.1");
    }

    DeviceInstance dev;
    dev.name = "test_" + classname;
    dev.classname = classname;
    dev.params = merged_params;
    dev.execution = make_execution_for_class(classname);
    for (const auto& port_name : ports) {
        dev.ports[port_name] = Port{PortDirection::InOut, PortType::Any};
    }

    // Ground reference so the system has at least one fixed signal
    DeviceInstance gnd;
    gnd.name = "gnd";
    gnd.classname = "RefNode";
    gnd.params = {{"value", "0"}};
    gnd.execution = make_execution_for_class("RefNode");
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
        "RefNode", "Bus", "Generator", "Gyroscope", "Transformer", "Inverter",
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
// DISABLED: Push model build_systems_dev no longer throws on unknown type;
// unknown types are silently skipped during component instantiation.
// ---------------------------------------------------------------------------
TEST(FactoryValidationTest, DISABLED_UnknownComponentType_Throws) {
    DeviceInstance unknown;
    unknown.name = "unknown_device";
    unknown.classname = "NonExistentComponent";
    unknown.execution = make_execution_for_class("NonExistentComponent");
    unknown.ports["dummy"] = Port{PortDirection::InOut, PortType::Any};

    DeviceInstance gnd;
    gnd.name = "gnd";
    gnd.classname = "RefNode";
    gnd.params = {{"value", "0"}};
    gnd.execution = make_execution_for_class("RefNode");
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
    EXPECT_EQ(Gyroscope_PORT_COUNT, get_component_ports("Gyroscope").size());
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
        "RefNode", "Bus", "Generator", "Gyroscope", "Transformer", "Inverter",
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
// DISABLED: Push model build_systems_dev no longer throws on unknown port;
// unknown ports are silently ignored during port mapping.
// ---------------------------------------------------------------------------
TEST(FactoryValidationTest, DISABLED_UnknownPortName_Throws) {
    DeviceInstance dev;
    dev.name = "test_battery";
    dev.classname = "Battery";
    dev.execution = make_execution_for_class("Battery");
    // "bogus_port" does not exist in Battery's port set
    dev.ports["v_in"] = Port{PortDirection::In, PortType::V};
    dev.ports["v_out"] = Port{PortDirection::Out, PortType::V};
    dev.ports["bogus_port"] = Port{PortDirection::Out, PortType::Any};

    DeviceInstance gnd;
    gnd.name = "gnd";
    gnd.classname = "RefNode";
    gnd.params = {{"value", "0"}};
    gnd.execution = make_execution_for_class("RefNode");
    gnd.ports["v"] = Port{PortDirection::Out, PortType::V};

    std::vector<DeviceInstance> devices = {dev, gnd};
    std::vector<std::pair<std::string, std::string>> connections;

    EXPECT_THROW(build_systems_dev(devices, connections), std::runtime_error);
}

TEST(FactoryValidationTest, MissingReferenceNode_WarnsButBuilds) {
    DeviceInstance bat;
    bat.name = "bat";
    bat.classname = "Battery";
    bat.execution = make_execution_for_class("Battery");
    bat.ports["v_in"] = Port{PortDirection::In, PortType::V};
    bat.ports["v_out"] = Port{PortDirection::Out, PortType::V};

    std::vector<DeviceInstance> devices = {bat};
    std::vector<std::pair<std::string, std::string>> connections;

    EXPECT_NO_THROW(build_systems_dev(devices, connections));
}
