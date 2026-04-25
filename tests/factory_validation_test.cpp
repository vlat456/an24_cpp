#include <gtest/gtest.h>
#include "core/model/component_kind.h"
#include "core/solvers/common/port_registry.h"
#include "core/solvers/jit/jit_solver.h"
#include "jit_build_input_test_helper.h"

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
        classname == "Lesser" || classname == "Bus") {
        phases.logical = true;
    }

    if (classname == "HoldButton" || classname == "Switch" || classname == "Relay" || classname == "AZS") {
        phases.control_commit = true;
    }

    if (classname == "LerpNode" || classname == "GidroAccumulator" || classname == "FuelTank") {
        phases.finalize = true;
    }

    if (classname == "InertiaNode" || classname == "Spring" ||
        classname == "ElectricPump") {
        phases.mechanical = true;
    }

    if (classname == "SolenoidValve" || classname == "GidroAccumulator" || classname == "FuelTank" ||
        classname == "ElectricPump") {
        phases.hydraulic = true;
    }

    if (classname == "TempSensor" || classname == "ElectricHeater" || classname == "Radiator" ||
        classname == "FuelTank") {
        phases.thermal = true;
    }

    return phases;
}

/// Helper: build a single-component system via build_systems_dev.
/// Creates a DeviceInstance with the given classname and all its registry ports,
/// plus a ground RefNode so signal allocation succeeds.
BuildResult build_single_component(const std::string& classname,
                                   const std::unordered_map<std::string, std::string>& params = {}) {
    auto ports = get_component_ports(parse_component_kind(classname).value_or(ComponentKind::Unknown));

    std::unordered_map<std::string, std::string> merged_params = params;
    // Strict-param components must provide canonical keys.
    if (classname == "P") {
        merged_params.try_emplace("Kp", "1.0");
        merged_params.try_emplace("output_min", "-1e9");
        merged_params.try_emplace("output_max", "1e9");
    } else if (classname == "PI") {
        // Kp, Ki, output_min, output_max are now ports (not params)
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
        merged_params.try_emplace("deadzone", "1e-6");
    } else if (classname == "LerpNode") {
        merged_params.try_emplace("factor", "0.5");
        merged_params.try_emplace("deadzone", "1e-6");
    } else if (classname == "TimeDelay") {
        merged_params.try_emplace("delay_on", "0.1");
        merged_params.try_emplace("delay_off", "0.1");
    }

    DeviceInstance dev;
    dev.name = "test_" + classname;
    dev.classname = classname;
    dev.params = merged_params;
    for (const auto& port_name : ports) {
        dev.ports[port_name] = Port{bp2::Direction::InOut, PortType::Any};
    }

    DeviceInstance gnd;
    gnd.name = "gnd";
    gnd.classname = "RefNode";
    gnd.params = {{"value", "0"}};
    gnd.ports["v"] = Port{bp2::Direction::Output, PortType::V};

    std::vector<DeviceInstance> devices = {dev, gnd};

    return build_systems_dev(make_jit_input(devices, {}));
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Test that all known component types can be built without throwing
// ---------------------------------------------------------------------------
TEST(FactoryValidationTest, Factory_CreatesAllKnownComponents) {
    // Iterate real components only (Unknown and _COUNT are not buildable)
    for (size_t i = 0; i < static_cast<size_t>(ComponentKind::Unknown); ++i) {
        auto kind = static_cast<ComponentKind>(i);
        std::string classname(component_kind_classname(kind));

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
// Test that port registry constants match get_component_ports() size
// ---------------------------------------------------------------------------
TEST(FactoryValidationTest, PortRegistryConstants_AreCorrect) {
    EXPECT_EQ(Switch_PORT_COUNT, get_component_ports(ComponentKind::Switch).size());
    EXPECT_EQ(Relay_PORT_COUNT, get_component_ports(ComponentKind::Relay).size());
    EXPECT_EQ(RefNode_PORT_COUNT, get_component_ports(ComponentKind::RefNode).size());
    EXPECT_EQ(Bus_PORT_COUNT, get_component_ports(ComponentKind::Bus).size());
    EXPECT_EQ(Gyroscope_PORT_COUNT, get_component_ports(ComponentKind::Gyroscope).size());
    EXPECT_EQ(Transformer_PORT_COUNT, get_component_ports(ComponentKind::Transformer).size());
    EXPECT_EQ(Inverter_PORT_COUNT, get_component_ports(ComponentKind::Inverter).size());
    EXPECT_EQ(LerpNode_PORT_COUNT, get_component_ports(ComponentKind::LerpNode).size());
    EXPECT_EQ(IndicatorLight_PORT_COUNT, get_component_ports(ComponentKind::IndicatorLight).size());
    EXPECT_EQ(ElectricPump_PORT_COUNT, get_component_ports(ComponentKind::ElectricPump).size());
    EXPECT_EQ(SolenoidValve_PORT_COUNT, get_component_ports(ComponentKind::SolenoidValve).size());
    EXPECT_EQ(InertiaNode_PORT_COUNT, get_component_ports(ComponentKind::InertiaNode).size());
    EXPECT_EQ(TempSensor_PORT_COUNT, get_component_ports(ComponentKind::TempSensor).size());
    EXPECT_EQ(ElectricHeater_PORT_COUNT, get_component_ports(ComponentKind::ElectricHeater).size());
    EXPECT_EQ(Radiator_PORT_COUNT, get_component_ports(ComponentKind::Radiator).size());
    EXPECT_EQ(Comparator_PORT_COUNT, get_component_ports(ComponentKind::Comparator).size());
    EXPECT_EQ(AZS_PORT_COUNT, get_component_ports(ComponentKind::AZS).size());
    EXPECT_EQ(AND_PORT_COUNT, get_component_ports(ComponentKind::AND).size());
    EXPECT_EQ(OR_PORT_COUNT, get_component_ports(ComponentKind::OR).size());
    EXPECT_EQ(NOT_PORT_COUNT, get_component_ports(ComponentKind::NOT).size());
}

// ---------------------------------------------------------------------------
// Test that every port listed in get_component_ports() is recognized by
// string_to_port_name() (i.e. no stale entries in the registry)
// ---------------------------------------------------------------------------
TEST(FactoryValidationTest, AllRegistryPortsAreRecognized) {
    // Iterate real components only (Unknown has no ports)
    for (size_t i = 0; i < static_cast<size_t>(ComponentKind::Unknown); ++i) {
        auto kind = static_cast<ComponentKind>(i);
        std::string classname(component_kind_classname(kind));

        auto ports = get_component_ports(kind);
        EXPECT_FALSE(ports.empty())
            << "Component '" << classname << "' has no ports in registry";

        for (const auto& port : ports) {
            auto port_enum = string_to_port_name(port);
            EXPECT_TRUE(port_enum.has_value())
                << "Port '" << port << "' of component '" << classname
                << "' is not recognized by string_to_port_name()";
        }
    }
}



TEST(FactoryValidationTest, MissingReferenceNode_WarnsButBuilds) {
    DeviceInstance bat;
    bat.name = "bat";
    bat.classname = "ElectricalSource";
    bat.ports["v_in"] = Port{bp2::Direction::Input, PortType::V};
    bat.ports["v_out"] = Port{bp2::Direction::Output, PortType::V};

    std::vector<DeviceInstance> devices = {bat};

    EXPECT_NO_THROW(build_systems_dev(make_jit_input(devices, {})));
}

// ---------------------------------------------------------------------------
// Regression: Special builders (LUT, RefNode) must use scheduler role from
// metadata, not hardcoded assumptions. Previously, emit_build_LUT hard-coded
// add_consumer and emit_build_RefNode hard-coded add_source, ignoring the
// scheduler_source / solver_owned_electrical metadata. If those values ever
// change, this test catches the mismatch.
// ---------------------------------------------------------------------------
TEST(FactoryValidationTest, SpecialBuilderSchedulerRolesMatchMetadata) {
    // Build a system with LUT (Consumer), RefNode (Source), and Value (Source)
    DeviceInstance lut_dev;
    lut_dev.name = "lut1";
    lut_dev.classname = "LUT";
    lut_dev.params = {{"table", "0:0; 100:100"}};
    for (const auto& port_name : get_component_ports(ComponentKind::LUT)) {
        lut_dev.ports[port_name] = Port{bp2::Direction::InOut, PortType::Any};
    }

    DeviceInstance ref_dev;
    ref_dev.name = "gnd";
    ref_dev.classname = "RefNode";
    ref_dev.params = {{"value", "0"}};
    for (const auto& port_name : get_component_ports(ComponentKind::RefNode)) {
        ref_dev.ports[port_name] = Port{bp2::Direction::InOut, PortType::Any};
    }

    DeviceInstance val_dev;
    val_dev.name = "val1";
    val_dev.classname = "Value";
    val_dev.params = {{"value", "5.0"}};
    for (const auto& port_name : get_component_ports(ComponentKind::Value)) {
        val_dev.ports[port_name] = Port{bp2::Direction::InOut, PortType::Any};
    }

    auto result = build_systems_dev(make_jit_input({lut_dev, ref_dev, val_dev}, {}));

    // RefNode and Value are sources; LUT is a consumer
    EXPECT_GE(result.scheduler.source_count(), 2u)
        << "RefNode and Value should both be scheduler sources";
    EXPECT_GE(result.scheduler.consumer_count(), 1u)
        << "LUT should be a scheduler consumer";
}

// Regression: register_from_library must throw (not silently skip) on missing specs.
// Previously used ASSERT_NE in a void helper, which silently returned on failure,
// leaving tests to run with incomplete registries and produce confusing downstream errors.
TEST(FactoryValidation, RegisterFromLibraryThrowsOnMissingSpec) {
    ComponentRegistry registry;

    // Valid type should not throw
    EXPECT_NO_THROW(register_from_library(registry, {"Resistor"}));
    EXPECT_NE(registry.get("Resistor"), nullptr);

    // Invalid type must throw with descriptive message
    EXPECT_THROW(
        register_from_library(registry, {"NonexistentType_XYZ"}),
        std::runtime_error);

    // Mixed valid + invalid must also throw (stops at first missing)
    EXPECT_THROW(
        register_from_library(registry, {"Resistor", "AlsoMissing_ABC"}),
        std::runtime_error);
}
