#include <gtest/gtest.h>

#include "core/solvers/aot/codegen.h"
#include "core/solvers/aot/codegen_composite_helpers.h"
#include "core/solvers/jit/jit_solver.h"
#include "json_parser/json_parser.h"
#include "jit_build_input_test_helper.h"
#include "test_fixtures.h"
#include "test_helpers.h"

namespace {

} // namespace

// DISABLED: AOT codegen smoke test checking for legacy solver-specific method names.
// In push model, codegen may use different method names or execution ordering,
// causing these string searches to fail. This test validates codegen output
// format rather than functional equivalence.
TEST(JitAotBridgeEquivalence, MinimalBridgeTopologyAndCodegenSmoke) {
    TypeRegistry registry;

    PrimitiveSpec gnd = make_refnode_type(bp2::Direction::Output);
    registry.types["RefNode"] = gnd;

    PrimitiveSpec cmd = make_any_v_to_bool_type();
    cmd.ports["v_in"] = Port{bp2::Direction::Input, PortType::Any, std::nullopt};
    registry.types["Any_V_to_Bool"] = cmd;

    PrimitiveSpec src;
    src.classname = "ControlledVoltageSource";
    src.domains = {Domain::Electrical};
    src.execution = make_execution(false, false, false, false, true, false, false, false, false);
    src.ports["cmd"] = Port{bp2::Direction::Input, PortType::Any, std::nullopt};
    src.ports["v_neg"] = Port{bp2::Direction::Input, PortType::V, std::nullopt};
    src.ports["v_pos"] = Port{bp2::Direction::Output, PortType::V, std::nullopt};
    src.ports["i_out"] = Port{bp2::Direction::Output, PortType::Any, std::nullopt};
    src.ports["gain"] = Port{bp2::Direction::Input, PortType::Any, std::nullopt};
    src.ports["offset"] = Port{bp2::Direction::Input, PortType::Any, std::nullopt};
    src.ports["min_v"] = Port{bp2::Direction::Input, PortType::Any, std::nullopt};
    src.ports["max_v"] = Port{bp2::Direction::Input, PortType::Any, std::nullopt};
    src.params["r_internal"] = ParamSpec{ParamSchemaType::Float, "0.1"};
    src.solver_owned_electrical = true;
    {
        SolverRole role;
        role.kind = "TheveninSource";
        role.port_map["pos"] = "v_pos";
        role.port_map["neg"] = "v_neg";
        role.param_map["resistance"] = "r_internal";
        role.value_map["voltage"] = 0.0f;
        role.value_map["bind_handle"] = 1.0f;
        src.solver_role = role;
    }
    registry.types["ControlledVoltageSource"] = src;

    PrimitiveSpec val = make_value_type();
    registry.types["Value"] = val;

    PrimitiveSpec meter = make_voltmeter_type();
    registry.types["Voltmeter"] = meter;

    std::vector<DeviceInstance> devices;

    DeviceInstance d_gnd;
    d_gnd.name = "gnd";
    d_gnd.classname = "RefNode";
    d_gnd = merge_device_instance(d_gnd, gnd);
    devices.push_back(d_gnd);

    DeviceInstance d_cmd_src;
    d_cmd_src.name = "cmd_src";
    d_cmd_src.classname = "RefNode";
    d_cmd_src.params["value"] = "1.0";
    d_cmd_src = merge_device_instance(d_cmd_src, gnd);
    devices.push_back(d_cmd_src);

    DeviceInstance d_bool;
    d_bool.name = "cmd_logic";
    d_bool.classname = "Any_V_to_Bool";
    d_bool = merge_device_instance(d_bool, cmd);
    devices.push_back(d_bool);

    DeviceInstance d_src;
    d_src.name = "src";
    d_src.classname = "ControlledVoltageSource";
    d_src.params["r_internal"] = "0.1";
    d_src = merge_device_instance(d_src, src);
    devices.push_back(d_src);

    // Value nodes for CVS port-based params
    DeviceInstance d_gain;
    d_gain.name = "src_gain";
    d_gain.classname = "Value";
    d_gain.params["value"] = "28.0";
    d_gain = merge_device_instance(d_gain, val);
    devices.push_back(d_gain);

    DeviceInstance d_offset;
    d_offset.name = "src_offset";
    d_offset.classname = "Value";
    d_offset.params["value"] = "0.0";
    d_offset = merge_device_instance(d_offset, val);
    devices.push_back(d_offset);

    DeviceInstance d_min_v;
    d_min_v.name = "src_min_v";
    d_min_v.classname = "Value";
    d_min_v.params["value"] = "0.0";
    d_min_v = merge_device_instance(d_min_v, val);
    devices.push_back(d_min_v);

    DeviceInstance d_max_v;
    d_max_v.name = "src_max_v";
    d_max_v.classname = "Value";
    d_max_v.params["value"] = "40.0";
    d_max_v = merge_device_instance(d_max_v, val);
    devices.push_back(d_max_v);

    DeviceInstance d_meter;
    d_meter.name = "meter";
    d_meter.classname = "Voltmeter";
    d_meter = merge_device_instance(d_meter, meter);
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

    // AOT side here is a structural codegen smoke test, not numeric parity:
    // we assign deterministic per-port indices directly (no union-find collapsing).
    std::unordered_map<std::string, uint32_t> port_to_signal;
    uint32_t next_signal = 0;
    for (const auto& dev : devices) {
        for (const auto& [port_name, _] : dev.ports) {
            port_to_signal[dev.name + "." + port_name] = next_signal++;
        }
    }

    const std::string aot_header =
        CodeGen::generate_header("bridge_equivalence.json", devices, connections, port_to_signal, next_signal);
    const std::string aot_source =
        CodeGen::generate_source("bridge_equivalence.h", devices, connections, port_to_signal, next_signal);

    ASSERT_FALSE(aot_header.empty());
    ASSERT_FALSE(aot_source.empty());

    std::vector<std::pair<std::string, std::string>> conn_pairs;
    for (const auto& c : connections) {
        conn_pairs.emplace_back(c.from, c.to);
    }
    BuildResult jit = build_systems_dev(make_jit_input_from_composite(devices, {}, connections));

    auto signal_of = [&](const std::string& port) {
        auto it = jit.port_to_signal.find(port);
        EXPECT_NE(it, jit.port_to_signal.end()) << port;
        return it == jit.port_to_signal.end() ? UINT32_MAX : it->second;
    };

    EXPECT_EQ(signal_of("cmd_src.v"), signal_of("cmd_logic.v_in"));
    EXPECT_EQ(signal_of("cmd_logic.o"), signal_of("src.cmd"));
    EXPECT_EQ(signal_of("gnd.v"), signal_of("src.v_neg"));
    EXPECT_EQ(signal_of("src.v_pos"), signal_of("meter.v_in"));

    EXPECT_NE(aot_source.find("cmd_logic.execute"), std::string::npos);
    EXPECT_NE(aot_source.find("src.execute"), std::string::npos);
    EXPECT_NE(aot_source.find("meter.execute"), std::string::npos);
}

TEST(JitAotBridgeEquivalence, SignalAllocationParityForBridgeAndAliasRules) {
    std::vector<DeviceInstance> devices;
    PrimitiveSpec resistor_type = make_resistor_type();
    std::vector<BridgePortDefinition> bridges = {
        make_bridge_port_def("vin", bp2::Direction::Input),
        make_bridge_port_def("vout", bp2::Direction::Output),
    };

    DeviceInstance pass;
    pass.name = "pass";
    pass.classname = "Resistor";
    pass.ports["v_in"] = Port{bp2::Direction::Input, PortType::V, std::nullopt};
    pass.ports["v_out"] = Port{bp2::Direction::Output, PortType::V, std::nullopt};
    pass.params["conductance"] = "1.0";
    pass = merge_device_instance(pass, resistor_type);
    devices.push_back(pass);

    std::vector<Connection> connections = {
        {"vin.port", "pass.v_in"},
        {"pass.v_out", "vout.port"},
    };

    std::vector<std::pair<std::string, std::string>> conn_pairs;
    conn_pairs.reserve(connections.size());
    for (const auto& c : connections) {
        conn_pairs.emplace_back(c.from, c.to);
    }
    BuildResult jit = build_systems_dev(make_jit_input_from_composite(devices, bridges, connections));

    std::vector<std::string> all_ports;
    std::unordered_map<std::string, uint32_t> port_to_idx;
    codegen_composite_detail::build_port_index_map(devices, bridges, all_ports, port_to_idx);

    codegen_composite_detail::UnionFind uf(all_ports.size());
    codegen_composite_detail::apply_signal_allocation_rules(uf, devices, bridges, connections, port_to_idx);

    uint32_t aot_signal_count = 0;
    auto aot_port_to_signal =
        codegen_composite_detail::finalize_signal_indices(uf, all_ports, port_to_idx, aot_signal_count);

    for (const auto& [port, aot_sig] : aot_port_to_signal) {
        auto it_jit = jit.port_to_signal.find(port);
        ASSERT_NE(it_jit, jit.port_to_signal.end()) << "Missing JIT signal for port " << port;
        (void)aot_sig;
    }

    for (const auto& [port_a, aot_sig_a] : aot_port_to_signal) {
        for (const auto& [port_b, aot_sig_b] : aot_port_to_signal) {
            const bool aot_same = (aot_sig_a == aot_sig_b);
            const bool jit_same = (jit.port_to_signal.at(port_a) == jit.port_to_signal.at(port_b));
            EXPECT_EQ(jit_same, aot_same)
                << "Partition mismatch for ports '" << port_a << "' and '" << port_b << "'";
        }
    }

    EXPECT_EQ(jit.port_to_signal.at("vin.ext"), jit.port_to_signal.at("vin.port"));
    EXPECT_EQ(jit.port_to_signal.at("vout.ext"), jit.port_to_signal.at("vout.port"));
    EXPECT_EQ(jit.port_to_signal.at("vin.port"), jit.port_to_signal.at("pass.v_in"));
    EXPECT_EQ(jit.port_to_signal.at("pass.v_out"), jit.port_to_signal.at("vout.port"));
    EXPECT_EQ(jit.signal_count, aot_signal_count);
}

TEST(JitAotBridgeEquivalence, VisualOnlyDevicesIgnoredByBothPaths) {
    std::vector<DeviceInstance> devices;
    PrimitiveSpec resistor_type = make_resistor_type();
    PrimitiveSpec value_type = make_value_type();
    std::vector<BridgePortDefinition> bridges = {
        make_bridge_port_def("vin", bp2::Direction::Input),
        make_bridge_port_def("vout", bp2::Direction::Output),
    };

    DeviceInstance load;
    load.name = "load";
    load.classname = "Resistor";
    load.ports["v_in"] = Port{bp2::Direction::Input, PortType::V, std::nullopt};
    load.ports["v_out"] = Port{bp2::Direction::Output, PortType::V, std::nullopt};
    load.params["conductance"] = "1.0";
    load = merge_device_instance(load, resistor_type);
    devices.push_back(load);

    DeviceInstance visual;
    visual.name = "ui_only";
    visual.classname = "Value";
    visual.visual_only = true;
    visual.ports["o"] = Port{bp2::Direction::Output, PortType::Any, std::nullopt};
    visual = merge_device_instance(visual, value_type);
    visual.visual_only = true;  // re-set after merge (value_type.visual_only is false)
    devices.push_back(visual);

    std::vector<Connection> connections = {
        {"vin.port", "load.v_in"},
        {"load.v_out", "vout.port"},
    };

    std::vector<std::pair<std::string, std::string>> conn_pairs;
    conn_pairs.reserve(connections.size());
    for (const auto& c : connections) {
        conn_pairs.emplace_back(c.from, c.to);
    }

    BuildResult jit = build_systems_dev(make_jit_input_from_composite(devices, bridges, connections));

    std::vector<std::string> all_ports;
    std::unordered_map<std::string, uint32_t> port_to_idx;
    codegen_composite_detail::build_port_index_map(devices, bridges, all_ports, port_to_idx);

    codegen_composite_detail::UnionFind uf(all_ports.size());
    codegen_composite_detail::apply_signal_allocation_rules(uf, devices, bridges, connections, port_to_idx);

    uint32_t aot_signal_count = 0;
    auto aot_port_to_signal =
        codegen_composite_detail::finalize_signal_indices(uf, all_ports, port_to_idx, aot_signal_count);

    // Bridges are elaboration-only for runtime component execution and AOT codegen,
    // but their ports remain part of signal allocation in both paths.
    EXPECT_EQ(jit.signal_count, aot_signal_count);
    EXPECT_EQ(jit.port_to_signal.count("ui_only.o"), 0u)
        << "JIT should ignore visual-only device ports";
    EXPECT_EQ(aot_port_to_signal.count("ui_only.o"), 0u)
        << "AOT should ignore visual-only device ports";

    EXPECT_EQ(jit.port_to_signal.at("vin.ext"), jit.port_to_signal.at("vin.port"));
    EXPECT_EQ(jit.port_to_signal.at("vout.ext"), jit.port_to_signal.at("vout.port"));
    EXPECT_EQ(aot_port_to_signal.at("vin.ext"), aot_port_to_signal.at("vin.port"));
    EXPECT_EQ(aot_port_to_signal.at("vout.ext"), aot_port_to_signal.at("vout.port"));

    EXPECT_EQ(jit.devices.count("vin"), 0u);
    EXPECT_EQ(jit.devices.count("vout"), 0u);
}
