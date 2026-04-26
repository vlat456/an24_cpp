#include <gtest/gtest.h>

#include "core/solvers/aot/codegen.h"
#include "core/solvers/common/signal_allocation.h"
#include "core/solvers/jit/jit_solver.h"
#include "core/registry/component_resolution.h"
#include "jit_build_input_test_helper.h"
#include "core/model/component_registry.h"
#include "core/domain_types.h"

namespace {

std::vector<ResolvedDevice> resolve_all_devices(const std::vector<DeviceInstance>& devices,
                                                 const ComponentRegistry& registry) {
    std::vector<ResolvedDevice> resolved;
    resolved.reserve(devices.size());
    for (const auto& dev : devices) {
        const ComponentSpec* spec = registry.get(dev.classname);
        if (!spec) {
            throw std::runtime_error("Missing test spec for " + dev.classname);
        }
        // Skip visual-only devices - same as elaboration boundary filtering
        if (auto* pres = registry.get_presentation(dev.classname)) {
            if (pres->visual_only) {
                continue;
            }
        }
        resolved.push_back(resolve_component(dev, *spec));
    }
    return resolved;
}

} // namespace

// Verify that JIT and AOT codegen produce consistent bridge signal topologies
// and that AOT source generation emits expected method calls.
TEST(JitAotBridgeEquivalence, MinimalBridgeTopologyAndCodegenSmoke) {
    ComponentRegistry registry;
    register_from_library(registry, {"RefNode", "ControlledVoltageSource", "Value", "Voltmeter"});

    // Override Any_V_to_Bool with simplified port (library uses "Vin" but test uses "v_in")
    PrimitiveSpec cmd = *as_primitive(*test_registry().get("Any_V_to_Bool"));
    cmd.ports["v_in"] = Port{bp2::Direction::Input, PortType::Any, std::nullopt};
    registry.register_type("Any_V_to_Bool", cmd);

    std::vector<DeviceInstance> devices;

    DeviceInstance d_gnd;
    d_gnd.name = "gnd";
    d_gnd.classname = "RefNode";
    devices.push_back(d_gnd);

    DeviceInstance d_cmd_src;
    d_cmd_src.name = "cmd_src";
    d_cmd_src.classname = "RefNode";
    d_cmd_src.params["value"] = "1.0";
    devices.push_back(d_cmd_src);

    DeviceInstance d_bool;
    d_bool.name = "cmd_logic";
    d_bool.classname = "Any_V_to_Bool";
    devices.push_back(d_bool);

    DeviceInstance d_src;
    d_src.name = "src";
    d_src.classname = "ControlledVoltageSource";
    d_src.params["r_internal"] = "0.1";
    devices.push_back(d_src);

    // Value nodes for CVS port-based params
    DeviceInstance d_gain;
    d_gain.name = "src_gain";
    d_gain.classname = "Value";
    d_gain.params["value"] = "28.0";
    devices.push_back(d_gain);

    DeviceInstance d_offset;
    d_offset.name = "src_offset";
    d_offset.classname = "Value";
    d_offset.params["value"] = "0.0";
    devices.push_back(d_offset);

    DeviceInstance d_min_v;
    d_min_v.name = "src_min_v";
    d_min_v.classname = "Value";
    d_min_v.params["value"] = "0.0";
    devices.push_back(d_min_v);

    DeviceInstance d_max_v;
    d_max_v.name = "src_max_v";
    d_max_v.classname = "Value";
    d_max_v.params["value"] = "40.0";
    devices.push_back(d_max_v);

    DeviceInstance d_meter;
    d_meter.name = "meter";
    d_meter.classname = "Voltmeter";
    devices.push_back(d_meter);

    std::vector<Connection> connections = {
        {"cmd_src.v", "cmd_logic.v_in"},
        {"cmd_logic.o", "src.cmd"},
        {"gnd.v", "src.v_neg"},
        {"src.v_pos", "meter.v_in"},
        {"src_gain.o", "src.gain"},
        {"src_offset.o", "src.offset"},
        {"src_min_v.o", "src.min_v"},
        {"src_max_v.o", "src.max_v"},
    };

    const std::vector<ResolvedDevice> resolved_devices = resolve_all_devices(devices, registry);

    // AOT side here is a structural codegen smoke test, not numeric parity:
    // we assign deterministic per-port indices directly (no union-find collapsing).
    std::unordered_map<std::string, uint32_t> port_to_signal;
    uint32_t next_signal = 0;
    for (const auto& dev : resolved_devices) {
        for (const auto& [port_name, _] : dev.ports) {
            port_to_signal[dev.name + "." + port_name] = next_signal++;
        }
    }

    const std::string aot_header =
        CodeGen::generate_header("bridge_equivalence.json", resolved_devices, port_to_signal, next_signal);
    const std::string aot_source =
        CodeGen::generate_source("bridge_equivalence.h", resolved_devices, port_to_signal, next_signal);

    ASSERT_FALSE(aot_header.empty());
    ASSERT_FALSE(aot_source.empty());

    BuildResult jit = build_systems_dev(make_jit_input_from_composite(devices, {}, connections, &registry));

    EXPECT_EQ(jit_signal_of(jit, "cmd_src.v"), jit_signal_of(jit, "cmd_logic.v_in"));
    EXPECT_EQ(jit_signal_of(jit, "cmd_logic.o"), jit_signal_of(jit, "src.cmd"));
    EXPECT_EQ(jit_signal_of(jit, "gnd.v"), jit_signal_of(jit, "src.v_neg"));
    EXPECT_EQ(jit_signal_of(jit, "src.v_pos"), jit_signal_of(jit, "meter.v_in"));

    EXPECT_NE(aot_source.find("cmd_logic.execute"), std::string::npos);
    EXPECT_NE(aot_source.find("src.execute"), std::string::npos);
    EXPECT_NE(aot_source.find("meter.execute"), std::string::npos);
}

TEST(JitAotBridgeEquivalence, SignalAllocationParityForBridgeAndAliasRules) {
    ComponentRegistry registry;
    register_from_library(registry, {"Resistor"});

    std::vector<DeviceInstance> devices;
    std::vector<BridgePortDefinition> bridges = {
        make_bridge_port_def("vin", bp2::BridgeDirection::Input),
        make_bridge_port_def("vout", bp2::BridgeDirection::Output),
    };

    DeviceInstance pass;
    pass.name = "pass";
    pass.classname = "Resistor";
    pass.ports["v_in"] = Port{bp2::Direction::Input, PortType::V, std::nullopt};
    pass.ports["v_out"] = Port{bp2::Direction::Output, PortType::V, std::nullopt};
    pass.params["conductance"] = "1.0";
    devices.push_back(pass);

    std::vector<Connection> connections = {
        {"vin.port", "pass.v_in"},
        {"pass.v_out", "vout.port"},
    };

    BuildResult jit = build_systems_dev(make_jit_input_from_composite(devices, bridges, connections));
    const std::vector<ResolvedDevice> resolved_for_aot = resolve_all_devices(devices, registry);

    std::vector<std::string> all_ports;
    std::unordered_map<std::string, uint32_t> port_to_idx;
    signal_alloc::build_port_index_map(resolved_for_aot, bridges, all_ports, port_to_idx);

    signal_alloc::UnionFind uf(all_ports.size());
    signal_alloc::apply_signal_allocation_rules(uf, resolved_for_aot, bridges, connections, port_to_idx);

    uint32_t aot_signal_count = 0;
    auto aot_port_to_signal =
        signal_alloc::finalize_signal_indices(uf, all_ports, port_to_idx, aot_signal_count);

    for (const auto& [port, aot_sig] : aot_port_to_signal) {
        auto it_jit = jit.port_to_signal.find(jit.signal_key_interner.lookup(port));
        ASSERT_NE(it_jit, jit.port_to_signal.end()) << "Missing JIT signal for port " << port;
        (void)aot_sig;
    }

    for (const auto& [port_a, aot_sig_a] : aot_port_to_signal) {
        for (const auto& [port_b, aot_sig_b] : aot_port_to_signal) {
            const bool aot_same = (aot_sig_a == aot_sig_b);
            const bool jit_same = (jit.port_to_signal.at(jit.signal_key_interner.lookup(port_a)) == jit.port_to_signal.at(jit.signal_key_interner.lookup(port_b)));
            EXPECT_EQ(jit_same, aot_same)
                << "Partition mismatch for ports '" << port_a << "' and '" << port_b << "'";
        }
    }

    EXPECT_EQ(jit.port_to_signal.at(jit.signal_key_interner.lookup("vin.ext")), jit.port_to_signal.at(jit.signal_key_interner.lookup("vin.port")));
    EXPECT_EQ(jit.port_to_signal.at(jit.signal_key_interner.lookup("vout.ext")), jit.port_to_signal.at(jit.signal_key_interner.lookup("vout.port")));
    EXPECT_EQ(jit.port_to_signal.at(jit.signal_key_interner.lookup("vin.port")), jit.port_to_signal.at(jit.signal_key_interner.lookup("pass.v_in")));
    EXPECT_EQ(jit.port_to_signal.at(jit.signal_key_interner.lookup("pass.v_out")), jit.port_to_signal.at(jit.signal_key_interner.lookup("vout.port")));
    EXPECT_EQ(jit.signal_count, aot_signal_count);
}

TEST(JitAotBridgeEquivalence, VisualOnlyDevicesIgnoredByBothPaths) {
    ComponentRegistry registry;
    register_from_library(registry, {"Resistor"});
    PrimitiveSpec value_type = *as_primitive(*test_registry().get("Value"));
    // visual_only is now on TypePresentation - bundle with register_type
    registry.register_type("Value", value_type, TypePresentation{.visual_only = true});

    std::vector<DeviceInstance> devices;
    std::vector<BridgePortDefinition> bridges = {
        make_bridge_port_def("vin", bp2::BridgeDirection::Input),
        make_bridge_port_def("vout", bp2::BridgeDirection::Output),
    };

    DeviceInstance load;
    load.name = "load";
    load.classname = "Resistor";
    load.ports["v_in"] = Port{bp2::Direction::Input, PortType::V, std::nullopt};
    load.ports["v_out"] = Port{bp2::Direction::Output, PortType::V, std::nullopt};
    load.params["conductance"] = "1.0";
    devices.push_back(load);

    DeviceInstance visual;
    visual.name = "ui_only";
    visual.classname = "Value";
    visual.ports["o"] = Port{bp2::Direction::Output, PortType::Any, std::nullopt};
    devices.push_back(visual);

    std::vector<Connection> connections = {
        {"vin.port", "load.v_in"},
        {"load.v_out", "vout.port"},
    };

    BuildResult jit = build_systems_dev(make_jit_input_from_composite(devices, bridges, connections, &registry));
    const std::vector<ResolvedDevice> resolved_for_aot = resolve_all_devices(devices, registry);

    std::vector<std::string> all_ports;
    std::unordered_map<std::string, uint32_t> port_to_idx;
    signal_alloc::build_port_index_map(resolved_for_aot, bridges, all_ports, port_to_idx);

    signal_alloc::UnionFind uf(all_ports.size());
    signal_alloc::apply_signal_allocation_rules(uf, resolved_for_aot, bridges, connections, port_to_idx);

    uint32_t aot_signal_count = 0;
    auto aot_port_to_signal =
        signal_alloc::finalize_signal_indices(uf, all_ports, port_to_idx, aot_signal_count);

    // Bridges are elaboration-only for runtime component execution and AOT codegen,
    // but their ports remain part of signal allocation in both paths.
    EXPECT_EQ(jit.signal_count, aot_signal_count);
    EXPECT_EQ(jit.port_to_signal.count(jit.signal_key_interner.lookup("ui_only.o")), 0u)
        << "JIT should ignore visual-only device ports";
    EXPECT_EQ(aot_port_to_signal.count("ui_only.o"), 0u)
        << "AOT should ignore visual-only device ports";

    EXPECT_EQ(jit.port_to_signal.at(jit.signal_key_interner.lookup("vin.ext")), jit.port_to_signal.at(jit.signal_key_interner.lookup("vin.port")));
    EXPECT_EQ(jit.port_to_signal.at(jit.signal_key_interner.lookup("vout.ext")), jit.port_to_signal.at(jit.signal_key_interner.lookup("vout.port")));
    EXPECT_EQ(aot_port_to_signal.at("vin.ext"), aot_port_to_signal.at("vin.port"));
    EXPECT_EQ(aot_port_to_signal.at("vout.ext"), aot_port_to_signal.at("vout.port"));

    EXPECT_EQ(jit.devices.count("vin"), 0u);
    EXPECT_EQ(jit.devices.count("vout"), 0u);
}

// =============================================================================
// Regression: Hydraulic solver_roles must not crash AOT codegen (issue #306)
//
// FuelTank and SolenoidValve have solver_role entries with domain="Hydraulic".
// The AOT electrical extraction loop must skip non-electrical solver_roles,
// not throw "unsupported solver_role kind". Previously, the JIT path had the
// domain guard but the AOT path did not.
// =============================================================================

TEST(JitAotBridgeEquivalence, HydraulicSolverRolesSkippedInAotCodegen) {
    ComponentRegistry registry;
    register_from_library(registry, {"FuelTank", "SolenoidValve", "Value", "RefNode"});

    std::vector<DeviceInstance> devices;

    DeviceInstance d_gnd;
    d_gnd.name = "gnd";
    d_gnd.classname = "RefNode";
    d_gnd.params["value"] = "0";
    devices.push_back(d_gnd);

    DeviceInstance d_tank;
    d_tank.name = "tank1";
    d_tank.classname = "FuelTank";
    d_tank.params["capacity"] = "500";
    d_tank.params["density"] = "0.78";
    d_tank.params["level"] = "500";
    d_tank.params["internal_r"] = "0.1";
    d_tank.params["tank_height"] = "1.0";
    for (const auto& port_name : get_component_ports(ComponentKind::FuelTank)) {
        d_tank.ports[port_name] = Port{bp2::Direction::InOut, PortType::Any};
    }
    devices.push_back(d_tank);

    DeviceInstance d_valve;
    d_valve.name = "valve1";
    d_valve.classname = "SolenoidValve";
    d_valve.params["normally_closed"] = "true";
    d_valve.params["g_open"] = "10.0";
    d_valve.params["g_closed"] = "0.0001";
    for (const auto& port_name : get_component_ports(ComponentKind::SolenoidValve)) {
        d_valve.ports[port_name] = Port{bp2::Direction::InOut, PortType::Any};
    }
    devices.push_back(d_valve);

    const std::vector<ResolvedDevice> resolved = resolve_all_devices(devices, registry);

    // Verify the solver_role is present and has Hydraulic domain
    const ResolvedDevice* tank_dev = nullptr;
    const ResolvedDevice* valve_dev = nullptr;
    for (const auto& r : resolved) {
        if (r.name == "tank1") tank_dev = &r;
        if (r.name == "valve1") valve_dev = &r;
    }
    ASSERT_NE(tank_dev, nullptr);
    ASSERT_NE(valve_dev, nullptr);
    ASSERT_TRUE(tank_dev->solver_role.has_value());
    ASSERT_TRUE(valve_dev->solver_role.has_value());
    EXPECT_EQ(tank_dev->solver_role->domain, Domain::Hydraulic);
    EXPECT_EQ(valve_dev->solver_role->domain, Domain::Hydraulic);

    // Build port-to-signal map for AOT codegen
    std::unordered_map<std::string, uint32_t> port_to_signal;
    uint32_t next_signal = 0;
    for (const auto& dev : resolved) {
        for (const auto& [port_name, _] : dev.ports) {
            port_to_signal[dev.name + "." + port_name] = next_signal++;
        }
    }

    // AOT codegen must NOT throw — hydraulic solver_roles must be silently skipped
    ElectricalPlanCodegen plan;
    EXPECT_NO_THROW({
        plan = extract_electrical_plan(resolved, port_to_signal);
    }) << "AOT codegen must not crash on hydraulic solver_roles (domain guard required)";

    // Hydraulic components must not appear in the electrical plan.
    // component_debug tracks every element's origin device.
    for (const auto& dbg : plan.component_debug) {
        EXPECT_NE(dbg.device_name, "tank1")
            << "FuelTank (hydraulic solver_role) must not appear in electrical plan";
        EXPECT_NE(dbg.device_name, "valve1")
            << "SolenoidValve (hydraulic solver_role) must not appear in electrical plan";
    }

    // JIT path must also succeed
    EXPECT_NO_THROW({
        build_systems_dev(make_jit_input(devices, {}));
    }) << "JIT path must handle hydraulic solver_roles gracefully";
}
