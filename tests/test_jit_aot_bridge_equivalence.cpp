#include <gtest/gtest.h>

#include "codegen/codegen.h"
#include "jit_solver/jit_solver.h"
#include "json_parser/json_parser.h"
#include "test_helpers.h"

namespace {

} // namespace

// DISABLED: AOT codegen smoke test checking for legacy solver-specific method names.
// In push model, codegen may use different method names or execution ordering,
// causing these string searches to fail. This test validates codegen output
// format rather than functional equivalence.
TEST(JitAotBridgeEquivalence, MinimalBridgeTopologyAndCodegenSmoke) {
    TypeRegistry registry;

    TypeDefinition gnd;
    gnd.classname = "RefNode";
    gnd.cpp_class = true;
    gnd.domains = {Domain::Electrical};
    gnd.execution = make_execution(true, false, false, false, false, false, false, false, false);
    gnd.ports["v"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    registry.types["RefNode"] = gnd;

    TypeDefinition cmd;
    cmd.classname = "Any_V_to_Bool";
    cmd.cpp_class = true;
    cmd.domains = {Domain::Electrical};
    cmd.execution = make_execution(false, false, true, false, false, false, false, false, false);
    cmd.ports["v_in"] = Port{PortDirection::In, PortType::Any, std::nullopt};
    cmd.ports["o"] = Port{PortDirection::Out, PortType::Bool, std::nullopt};
    registry.types["Any_V_to_Bool"] = cmd;

    TypeDefinition src;
    src.classname = "ControlledVoltageSource";
    src.cpp_class = true;
    src.domains = {Domain::Electrical};
    src.execution = make_execution(false, false, false, false, true, false, false, false, false);
    src.ports["cmd"] = Port{PortDirection::In, PortType::Any, std::nullopt};
    src.ports["v_neg"] = Port{PortDirection::In, PortType::V, std::nullopt};
    src.ports["v_pos"] = Port{PortDirection::Out, PortType::V, std::nullopt};
    src.ports["i_out"] = Port{PortDirection::Out, PortType::Any, std::nullopt};
    src.ports["gain"] = Port{PortDirection::In, PortType::Any, std::nullopt};
    src.ports["offset"] = Port{PortDirection::In, PortType::Any, std::nullopt};
    src.ports["min_v"] = Port{PortDirection::In, PortType::Any, std::nullopt};
    src.ports["max_v"] = Port{PortDirection::In, PortType::Any, std::nullopt};
    registry.types["ControlledVoltageSource"] = src;

    TypeDefinition val;
    val.classname = "Value";
    val.cpp_class = true;
    val.domains = {Domain::Logical};
    val.execution = make_execution(true, false, false, false, false, false, false, false, false);
    val.ports["o"] = Port{PortDirection::Out, PortType::Any, std::nullopt};
    registry.types["Value"] = val;

    TypeDefinition meter;
    meter.classname = "Voltmeter";
    meter.cpp_class = true;
    meter.domains = {Domain::Electrical};
    meter.execution = make_execution(false, true, false, false, false, false, false, false, false);
    meter.ports["v_in"] = Port{PortDirection::In, PortType::V, std::nullopt};
    meter.ports["out"] = Port{PortDirection::Out, PortType::V, std::nullopt};
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
    BuildResult jit = build_systems_dev(devices, conn_pairs);

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
