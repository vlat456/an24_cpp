/// Regression tests for elaborate_for_codegen() (Issue #245).
///
/// Validates that the codegen elaboration path produces identical topology
/// and signal mapping as the JIT elaboration path, modulo key representation
/// (string keys vs InternedId keys).

#include <gtest/gtest.h>

#include "blueprint_v2/flattener/flattener.h"
#include "blueprint_v2/elaboration/sim_export.h"
#include "blueprint_v2/elaboration/codegen_export.h"
#include "blueprint_v2/library/blueprint_library.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/interface/interface.h"
#include "blueprint_v2/interface/port_descriptor.h"
#include "blueprint_v2/path/path.h"
#include "core/solvers/common/signal_key.h"
#include "ui/core/interned_id.h"
#include "io/json/component_registry_json_loader.h"

#include <set>
#include <string>
#include <unordered_map>

namespace {

PrimitiveSpec make_primitive_spec(
    const std::string& classname,
    std::initializer_list<std::pair<const char*, Port>> ports,
    std::vector<Domain> domains)
{
    PrimitiveSpec spec;
    spec.classname = classname;
    spec.domains = std::move(domains);
    spec.solver.execution = ExecutionPhases{.electrical_passive = true};
    for (const auto& [name, port] : ports) {
        spec.ports[name] = port;
    }
    return spec;
}

const ComponentRegistry& parity_registry() {
    static const ComponentRegistry registry = [] {
        ComponentRegistry reg;
        reg.register_type("Battery", make_primitive_spec(
            "Battery",
            {
                {"v_out", Port{bp2::Direction::Output, PortType::V, Domain::Electrical, false}},
                {"v_in", Port{bp2::Direction::Input, PortType::V, Domain::Electrical, false}},
            },
            {Domain::Electrical}));
        reg.register_type("Resistor", make_primitive_spec(
            "Resistor",
            {
                {"v_in", Port{bp2::Direction::Input, PortType::V, Domain::Electrical, false}},
                {"v_out", Port{bp2::Direction::Output, PortType::V, Domain::Electrical, false}},
            },
            {Domain::Electrical}));
        reg.register_type("LED", make_primitive_spec(
            "LED",
            {
                {"v_in", Port{bp2::Direction::Input, PortType::V, Domain::Electrical, false}},
            },
            {Domain::Electrical}));
        reg.register_type("InertiaNode", make_primitive_spec(
            "InertiaNode",
            {
                {"rpm_out", Port{bp2::Direction::Output, PortType::RPM, Domain::Mechanical, false}},
            },
            {Domain::Mechanical}));
        return reg;
    }();
    return registry;
}

bp2::Blueprint::Node make_node(ui::StringInterner& I,
                                const char* id,
                                const char* type,
                                std::initializer_list<bp2::PortDescriptor> ports) {
    bp2::Blueprint::Node n;
    n.semantic.id = I.intern(id);
    n.semantic.type = I.intern(type);
    n.component().iface = bp2::Interface(ports);
    return n;
}

bp2::PortDescriptor in_port(ui::StringInterner& I, const char* name, PortType t = PortType::V) {
    return {I.intern(name), ::domain_for_port_type(t), bp2::Direction::Input, t};
}

bp2::PortDescriptor out_port(ui::StringInterner& I, const char* name, PortType t = PortType::V) {
    return {I.intern(name), ::domain_for_port_type(t), bp2::Direction::Output, t};
}

bp2::Blueprint::Node make_bridge_node(ui::StringInterner& I,
                                        const char* id,
                                        const char* exposed_port,
                                        bool input_side,
                                        PortType type = PortType::V) {
    bp2::Blueprint::Node n;
    n.semantic.id = I.intern(id);
    n.semantic.type = I.intern("BridgePort");
    n.view.name = exposed_port;
    n.content = bp2::Blueprint::Node::BridgePortData{
        I.intern(exposed_port),
        input_side ? bp2::BridgeDirection::Input
                    : bp2::BridgeDirection::Output,
        type,
    };
    return n;
}

std::set<std::string> collect_device_names(const auto& input) {
    std::set<std::string> out;
    for (const auto& dev : input.devices) {
        out.insert(dev.name);
    }
    return out;
}

/// Check that two node.port string keys map to the same signal index.
bool same_signal_codegen(const bp2::elaboration::CodegenBuildInput& input,
                          const std::string& a,
                          const std::string& b) {
    auto it_a = input.port_to_signal.find(a);
    auto it_b = input.port_to_signal.find(b);
    if (it_a == input.port_to_signal.end() || it_b == input.port_to_signal.end()) {
        return false;
    }
    return it_a->second == it_b->second;
}

} // namespace


// ==============================================================================
// Codegen vs JIT: device list and signal_count must be identical for a
// simple two-device flat netlist.
//
// Topology: bat.v_out → r1.v_in
// ==============================================================================

TEST(CodegenExportParity, FlatTwoDevicesMatchJit) {
    ui::StringInterner I;
    bp2::PathArena arena(I);
    bp2::BlueprintLibrary library;

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "bat", "Battery", {
        out_port(I, "v_out"),
    }));
    bp = bp.with_node(make_node(I, "r1", "Resistor", {
        in_port(I, "v_in"),
        out_port(I, "v_out"),
    }));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w1");
    w.source = {I.intern("bat"), I.intern("v_out")};
    w.target = {I.intern("r1"), I.intern("v_in")};
    w.domain = Domain::Electrical;
    bp = bp.with_wire(std::move(w));

    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(bp, arena);

    auto jit = bp2::elaboration::elaborate_for_jit(netlist, arena, I, parity_registry());
    auto cg  = bp2::elaboration::elaborate_for_codegen(netlist, arena, I, parity_registry());

    // Same devices
    auto jit_devs = collect_device_names(jit);
    auto cg_devs  = collect_device_names(cg);
    EXPECT_EQ(jit_devs, cg_devs);
    EXPECT_TRUE(cg_devs.count("bat"));
    EXPECT_TRUE(cg_devs.count("r1"));

    // Same signal_count
    EXPECT_EQ(jit.signal_count, cg.signal_count);

    // Connected ports share the same signal in codegen output
    std::string bat_v_out = signal_key::make_node_port_key("bat", "v_out");
    std::string r1_v_in   = signal_key::make_node_port_key("r1", "v_in");
    EXPECT_TRUE(same_signal_codegen(cg, bat_v_out, r1_v_in))
        << "bat.v_out and r1.v_in must share the same signal";

    // Disconnected port has a different signal
    std::string r1_v_out = signal_key::make_node_port_key("r1", "v_out");
    EXPECT_FALSE(same_signal_codegen(cg, bat_v_out, r1_v_out))
        << "bat.v_out and r1.v_out must NOT share the same signal";
}


// ==============================================================================
// Codegen vs JIT: single-level embedded blueprint with bridge port.
//
// Topology:
//   root: bat ──v_out──→ [inst].vin
//   inst: vin(bridge) ──ext──→ r1.v_in
//
// Verifies bridge signal keys are present in codegen output.
// ==============================================================================

TEST(CodegenExportParity, SingleLevelBridgeKeysMatchJit) {
    ui::StringInterner I;
    bp2::PathArena arena(I);
    bp2::BlueprintLibrary library;

    // Inner blueprint: bridge(vin) → r1
    bp2::Blueprint inner;
    inner = inner.with_interface(bp2::Interface({in_port(I, "vin")}));
    inner = inner.with_node(make_bridge_node(I, "vin", "vin", true));
    inner = inner.with_node(make_node(I, "r1", "Resistor", {
        in_port(I, "v_in"),
        out_port(I, "v_out"),
    }));

    bp2::Blueprint::Wire iw;
    iw.id = I.intern("iw1");
    iw.source = {I.intern("vin"), I.intern("ext")};
    iw.target = {I.intern("r1"), I.intern("v_in")};
    iw.domain = Domain::Electrical;
    inner = inner.with_wire(std::move(iw));

    // Root: bat → [inst].vin
    bp2::Blueprint root;
    root = root.with_node(make_node(I, "bat", "Battery", {
        out_port(I, "v_out"),
    }));

    bp2::Blueprint::Node inst;
    inst.semantic.id = I.intern("inst");
    inst.semantic.type = I.intern("EmbeddedType");
    inst.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(inner.with_id(I.intern("EmbeddedType"))))
    };
    root = root.with_node(std::move(inst));

    bp2::Blueprint::Wire rw;
    rw.id = I.intern("rw1");
    rw.source = {I.intern("bat"), I.intern("v_out")};
    rw.target = {I.intern("inst"), I.intern("vin")};
    rw.domain = Domain::Electrical;
    root = root.with_wire(std::move(rw));

    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(root, arena);

    auto jit = bp2::elaboration::elaborate_for_jit(netlist, arena, I, parity_registry());
    auto cg  = bp2::elaboration::elaborate_for_codegen(netlist, arena, I, parity_registry());

    // Same devices (no bridge nodes in either)
    auto jit_devs = collect_device_names(jit);
    auto cg_devs  = collect_device_names(cg);
    EXPECT_EQ(jit_devs, cg_devs);
    EXPECT_EQ(cg_devs.size(), 2u);
    EXPECT_TRUE(cg_devs.count("bat"));
    EXPECT_TRUE(cg_devs.count("inst:r1"));

    // Same signal_count
    EXPECT_EQ(jit.signal_count, cg.signal_count);

    // Bridge key "inst.vin" must exist in codegen output
    std::string inst_vin = signal_key::make_node_port_key("inst", "vin");
    EXPECT_TRUE(cg.port_to_signal.count(inst_vin))
        << "Missing bridge key: " << inst_vin;

    // All three endpoints must be on the same signal
    std::string bat_v_out = signal_key::make_node_port_key("bat", "v_out");
    std::string inst_r1_v_in = signal_key::make_node_port_key("inst:r1", "v_in");

    EXPECT_TRUE(same_signal_codegen(cg, bat_v_out, inst_vin));
    EXPECT_TRUE(same_signal_codegen(cg, inst_vin, inst_r1_v_in));
}


// ==============================================================================
// Codegen signal keys use "node_id.port_name" string format.
// Verifies that every key in codegen output can be split on '.' to recover
// the device name and port name.
// ==============================================================================

TEST(CodegenExportParity, SignalKeysAreNodeDotPort) {
    ui::StringInterner I;
    bp2::PathArena arena(I);
    bp2::BlueprintLibrary library;

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "bat", "Battery", {
        out_port(I, "v_out"),
    }));
    bp = bp.with_node(make_node(I, "r1", "Resistor", {
        in_port(I, "v_in"),
        out_port(I, "v_out"),
    }));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w1");
    w.source = {I.intern("bat"), I.intern("v_out")};
    w.target = {I.intern("r1"), I.intern("v_in")};
    w.domain = Domain::Electrical;
    bp = bp.with_wire(std::move(w));

    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(bp, arena);
    auto cg = bp2::elaboration::elaborate_for_codegen(netlist, arena, I, parity_registry());

    for (const auto& [key, sig_idx] : cg.port_to_signal) {
        const size_t dot = key.rfind('.');
        EXPECT_NE(dot, std::string::npos)
            << "Signal key '" << key << "' is missing '.' separator";
        EXPECT_GT(dot, 0u)
            << "Signal key '" << key << "' has empty node_id before '.'";
        EXPECT_LT(dot + 1, key.size())
            << "Signal key '" << key << "' has empty port_name after '.'";
    }
}


// ==============================================================================
// Codegen output has default params filled from spec.
// Devices must contain default param values even when not explicitly set
// in the blueprint.
// ==============================================================================

TEST(CodegenExportParity, DefaultParamsAreFilled) {
    ui::StringInterner I;
    bp2::PathArena arena(I);
    bp2::BlueprintLibrary library;

    // Register a component with a default param
    ComponentRegistry reg;
    PrimitiveSpec spec;
    spec.classname = "Resistor";
    spec.domains = {Domain::Electrical};
    spec.solver.execution = ExecutionPhases{.electrical_passive = true};
    spec.ports["v_in"]  = Port{bp2::Direction::Input, PortType::V, Domain::Electrical, false};
    spec.ports["v_out"] = Port{bp2::Direction::Output, PortType::V, Domain::Electrical, false};
    spec.params["resistance"] = ParamSpec{ParamSchemaType::Float, "100.0", std::nullopt, std::nullopt};
    reg.register_type("Resistor", std::move(spec));

    bp2::Blueprint bp;
    bp = bp.with_node(make_node(I, "r1", "Resistor", {
        in_port(I, "v_in"),
        out_port(I, "v_out"),
    }));

    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(bp, arena);
    auto cg = bp2::elaboration::elaborate_for_codegen(netlist, arena, I, reg);

    ASSERT_EQ(cg.devices.size(), 1u);
    EXPECT_EQ(cg.devices[0].name, "r1");

    // The default "resistance" param must be present even though we never set it
    EXPECT_TRUE(cg.devices[0].params.count("resistance"))
        << "Default param 'resistance' should be filled from spec";
    EXPECT_EQ(cg.devices[0].params.at("resistance"), "100.0");
}
