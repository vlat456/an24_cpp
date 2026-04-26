#include <gtest/gtest.h>

#include "blueprint_v2/flattener/flattener.h"
#include "blueprint_v2/elaboration/sim_export.h"
#include "blueprint_v2/library/blueprint_library.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/interface/interface.h"
#include "blueprint_v2/interface/port_descriptor.h"
#include "blueprint_v2/path/path.h"
#include "core/solvers/common/signal_key.h"
#include "core/strings/interned_id.h"
#include "io/json/component_registry_json_loader.h"
#include "elaboration_parity_fixtures.h"

#include <set>
#include <string>
#include <unordered_map>

namespace {

/// Check that two node.port keys map to the same signal index in port_to_signal.
bool connected_on_same_signal(const JitBuildInput& jit_input,
                              const std::string& a,
                              const std::string& b) {
    const core::InternedId key_a = jit_input.signal_key_interner.lookup(a);
    const core::InternedId key_b = jit_input.signal_key_interner.lookup(b);
    if (key_a.empty() || key_b.empty()) return false;
    auto it_a = jit_input.port_to_signal.find(key_a);
    auto it_b = jit_input.port_to_signal.find(key_b);
    if (it_a == jit_input.port_to_signal.end() || it_b == jit_input.port_to_signal.end()) {
        return false;
    }
    return it_a->second == it_b->second;
}

} // namespace


// ==============================================================================
// Single-level embedded blueprint instance — flattener resolves boundary
// crossing directly to bridge ext port, no sim_export rewrite needed.
//
// Topology:
//   root: bat ──v_out──→ [inst].vin
//   inst: vin(bridge_port) ──ext──→ r1.v_in
//
// Expected runtime devices: bat, inst:r1
// Expected connectivity: bat.v_out, inst.vin, inst:r1.v_in all on same signal
// ==============================================================================

TEST(ExportFlattenerParity, SingleLevelEmbeddedBridgeRewrite) {
    core::StringInterner I;
    bp2::PathArena arena(I);
    bp2::BlueprintLibrary library;

    // Build inner blueprint for the instance: bridge(vin) → r1
    bp2::Blueprint inner;
    inner = inner.with_interface(bp2::Interface({in_port(I, "vin")}));

    inner = inner.with_node(make_bridge_node(I, "vin", "vin", true));

    // Leaf resistor
    inner = inner.with_node(make_node(I, "r1", "Resistor", {
        in_port(I, "v_in"),
        out_port(I, "v_out"),
    }));

    // Inner wire: vin.ext → r1.v_in
    bp2::Blueprint::Wire iw;
    iw.id = I.intern("iw1");
    iw.source = {I.intern("vin"), I.intern("ext")};
    iw.target = {I.intern("r1"), I.intern("v_in")};
    iw.domain = Domain::Electrical;
    inner = inner.with_wire(std::move(iw));

    // Build root blueprint
    bp2::Blueprint root;

    // Battery node
    root = root.with_node(make_node(I, "bat", "Battery", {
        out_port(I, "v_out"),
    }));

    // Blueprint-instance node "inst"
    bp2::Blueprint::Node inst;
    inst.semantic.id = I.intern("inst");
    inst.semantic.type = I.intern("EmbeddedType");
    inst.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(inner.with_id(I.intern("EmbeddedType"))))
    };
    root = root.with_node(std::move(inst));

    // Root wire: bat.v_out → inst.vin
    bp2::Blueprint::Wire rw;
    rw.id = I.intern("rw1");
    rw.source = {I.intern("bat"), I.intern("v_out")};
    rw.target = {I.intern("inst"), I.intern("vin")};
    rw.domain = Domain::Electrical;
    root = root.with_wire(std::move(rw));

    // Flatten and elaborate
    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(root, arena);
    auto jit_input = bp2::elaboration::elaborate_for_jit(netlist, arena, I, parity_registry());

    // Verify devices exist
    auto devices = collect_device_names(jit_input);
    EXPECT_TRUE(devices.count("bat")) << "Missing device: bat";
    EXPECT_TRUE(devices.count("inst:r1")) << "Missing device: inst:r1";

    // Verify connectivity — all three endpoints must be on the same signal.
    std::string bat_v_out = signal_key::make_node_port_key("bat", "v_out");
    std::string inst_vin = signal_key::make_node_port_key("inst", "vin");
    std::string inst_r1_v_in = signal_key::make_node_port_key("inst:r1", "v_in");

    EXPECT_TRUE(connected_on_same_signal(jit_input, bat_v_out, inst_vin))
        << "bat.v_out and inst.vin must share the same signal";

    EXPECT_TRUE(connected_on_same_signal(jit_input, inst_vin, inst_r1_v_in))
        << "inst.vin and inst:r1.v_in must share the same signal";

    EXPECT_TRUE(connected_on_same_signal(jit_input, bat_v_out, inst_r1_v_in))
        << "bat.v_out and inst:r1.v_in must share the same signal (transitively)";
}


// ==============================================================================
// 3-level nesting: root → mid(embedded) → sub(embedded) → leaf
//
// Topology:
//   root: bat ──v_out──→ [mid].vin
//   mid:  vin(bridge) ──ext──→ [sub].pin
//   sub:  pin(bridge) ──ext──→ r1.v_in
//
// Expected runtime components: bat, mid:sub:r1
// Expected connectivity: bat.v_out, mid.vin, mid:sub.pin, mid:sub:r1.v_in
//                         all on same signal
// ==============================================================================

TEST(ExportFlattenerParity, ThreeLevelNestedBridgeRewrite) {
    core::StringInterner I;
    bp2::PathArena arena(I);
    bp2::BlueprintLibrary library;

    // ---- innermost "sub" blueprint: pin(bridge) → r1 ----
    bp2::Blueprint sub_bp;
    sub_bp = sub_bp.with_interface(bp2::Interface({in_port(I, "pin")}));

    sub_bp = sub_bp.with_node(make_bridge_node(I, "pin", "pin", true));

    sub_bp = sub_bp.with_node(make_node(I, "r1", "Resistor", {
        in_port(I, "v_in"),
        out_port(I, "v_out"),
    }));

    bp2::Blueprint::Wire sw;
    sw.id = I.intern("sw1");
    sw.source = {I.intern("pin"), I.intern("ext")};
    sw.target = {I.intern("r1"), I.intern("v_in")};
    sw.domain = Domain::Electrical;
    sub_bp = sub_bp.with_wire(std::move(sw));

    // ---- middle "mid" blueprint: vin(bridge) → [sub].pin ----
    bp2::Blueprint mid_bp;
    mid_bp = mid_bp.with_interface(bp2::Interface({in_port(I, "vin")}));

    mid_bp = mid_bp.with_node(make_bridge_node(I, "vin", "vin", true));

    // Embedded sub instance
    bp2::Blueprint::Node sub_inst;
    sub_inst.semantic.id = I.intern("sub");
    sub_inst.semantic.type = I.intern("SubType");
    sub_inst.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(sub_bp.with_id(I.intern("SubType"))))
    };
    mid_bp = mid_bp.with_node(std::move(sub_inst));

    bp2::Blueprint::Wire mw;
    mw.id = I.intern("mw1");
    mw.source = {I.intern("vin"), I.intern("ext")};
    mw.target = {I.intern("sub"), I.intern("pin")};
    mw.domain = Domain::Electrical;
    mid_bp = mid_bp.with_wire(std::move(mw));

    // ---- root blueprint: bat → [mid].vin ----
    bp2::Blueprint root;

    root = root.with_node(make_node(I, "bat", "Battery", {
        out_port(I, "v_out"),
    }));

    bp2::Blueprint::Node mid_inst;
    mid_inst.semantic.id = I.intern("mid");
    mid_inst.semantic.type = I.intern("MidType");
    mid_inst.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(mid_bp.with_id(I.intern("MidType"))))
    };
    root = root.with_node(std::move(mid_inst));

    bp2::Blueprint::Wire rw;
    rw.id = I.intern("rw1");
    rw.source = {I.intern("bat"), I.intern("v_out")};
    rw.target = {I.intern("mid"), I.intern("vin")};
    rw.domain = Domain::Electrical;
    root = root.with_wire(std::move(rw));

    // Flatten and elaborate
    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(root, arena);
    auto jit_input = bp2::elaboration::elaborate_for_jit(netlist, arena, I, parity_registry());

    // Verify devices
    auto devices = collect_device_names(jit_input);
    EXPECT_TRUE(devices.count("bat")) << "Missing device: bat";
    EXPECT_TRUE(devices.count("mid:sub:r1")) << "Missing device: mid:sub:r1";

    // Verify connectivity with bridge resolution at each level
    std::string bat_v_out = signal_key::make_node_port_key("bat", "v_out");
    std::string mid_vin = signal_key::make_node_port_key("mid", "vin");
    std::string mid_sub_pin = signal_key::make_node_port_key("mid:sub", "pin");
    std::string mid_sub_r1_v_in = signal_key::make_node_port_key("mid:sub:r1", "v_in");

    // bat.v_out ↔ mid.vin  (root→mid boundary)
    EXPECT_TRUE(connected_on_same_signal(jit_input, bat_v_out, mid_vin))
        << "Level-1 boundary: bat.v_out and mid.vin must share the same signal";

    // mid.vin ↔ mid:sub.pin  (mid→sub boundary)
    EXPECT_TRUE(connected_on_same_signal(jit_input, mid_vin, mid_sub_pin))
        << "Level-2 boundary: mid.vin and mid:sub.pin must share the same signal";

    // mid:sub.pin ↔ mid:sub:r1.v_in  (inner direct connection)
    EXPECT_TRUE(connected_on_same_signal(jit_input, mid_sub_pin, mid_sub_r1_v_in))
        << "Inner connection: mid:sub.pin and mid:sub:r1.v_in must share the same signal";
}


// ==============================================================================
// Output bridge_port: output side of a blueprint instance.
//
// Topology:
//   inst: r1.v_out → vout(bridge_port).ext
//   root: [inst].vout → led.v_in
//
// Expected connectivity: inst:vout.ext and led.v_in on same signal
// ==============================================================================

TEST(ExportFlattenerParity, OutputBridgeRewrite) {
    core::StringInterner I;
    bp2::PathArena arena(I);
    bp2::BlueprintLibrary library;

    // Inner blueprint: r1 → vout(bridge output)
    bp2::Blueprint inner;
    inner = inner.with_interface(bp2::Interface({out_port(I, "vout")}));

    inner = inner.with_node(make_node(I, "r1", "Resistor", {
        in_port(I, "v_in"),
        out_port(I, "v_out"),
    }));

    inner = inner.with_node(make_bridge_node(I, "vout", "vout", false));

    bp2::Blueprint::Wire iw;
    iw.id = I.intern("iw1");
    iw.source = {I.intern("r1"), I.intern("v_out")};
    iw.target = {I.intern("vout"), I.intern("ext")};
    iw.domain = Domain::Electrical;
    inner = inner.with_wire(std::move(iw));

    // Root blueprint
    bp2::Blueprint root;

    bp2::Blueprint::Node inst;
    inst.semantic.id = I.intern("inst");
    inst.semantic.type = I.intern("EmbeddedType");
    inst.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(inner.with_id(I.intern("EmbeddedType"))))
    };
    root = root.with_node(std::move(inst));

    root = root.with_node(make_node(I, "led", "LED", {
        in_port(I, "v_in"),
    }));

    bp2::Blueprint::Wire rw;
    rw.id = I.intern("rw1");
    rw.source = {I.intern("inst"), I.intern("vout")};
    rw.target = {I.intern("led"), I.intern("v_in")};
    rw.domain = Domain::Electrical;
    root = root.with_wire(std::move(rw));

    // Flatten and elaborate
    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(root, arena);
    auto jit_input = bp2::elaboration::elaborate_for_jit(netlist, arena, I, parity_registry());

    // inst:vout.ext must connect to led.v_in (bridge resolution on output side)
    std::string inst_vout_ext = signal_key::make_node_port_key("inst:vout", "ext");
    std::string led_v_in = signal_key::make_node_port_key("led", "v_in");

    EXPECT_TRUE(connected_on_same_signal(jit_input, inst_vout_ext, led_v_in))
        << "Output bridge resolution failed: inst:vout.ext and led.v_in must share the same signal";
}

TEST(ExportFlattenerParity, StructuralOutputBridgeExposesCanonicalInterfacePortKey) {
    core::StringInterner I;
    bp2::PathArena arena(I);
    bp2::BlueprintLibrary library;

    bp2::Blueprint inner;
    inner = inner.with_interface(bp2::Interface({out_port(I, "out", PortType::RPM)}));

    inner = inner.with_node(make_node(I, "rotor", "InertiaNode", {
        out_port(I, "rpm_out", PortType::RPM),
    }));

    inner = inner.with_node(make_bridge_node(I, "bp_out_1", "out", false, PortType::RPM));

    bp2::Blueprint::Wire iw;
    iw.id = I.intern("iw_rpm");
    iw.source = {I.intern("rotor"), I.intern("rpm_out")};
    iw.target = {I.intern("bp_out_1"), I.intern("ext")};
    iw.domain = Domain::Mechanical;
    inner = inner.with_wire(std::move(iw));

    bp2::Blueprint root;
    bp2::Blueprint::Node inst;
    inst.semantic.id = I.intern("extract_inst_4");
    inst.semantic.type = I.intern("RPMIntertial");
inst.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(inner.with_id(I.intern("RPMIntertial"))))
    };
    root = root.with_node(std::move(inst));

    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(root, arena);
    auto jit_input = bp2::elaboration::elaborate_for_jit(netlist, arena, I, parity_registry());

    const core::InternedId key_out = jit_input.signal_key_interner.lookup("extract_inst_4.out");
    const core::InternedId key_ext = jit_input.signal_key_interner.lookup("extract_inst_4:bp_out_1.ext");
    ASSERT_FALSE(key_out.empty());
    ASSERT_FALSE(key_ext.empty());
    ASSERT_EQ(jit_input.port_to_signal.count(key_out), 1u);
    ASSERT_EQ(jit_input.port_to_signal.count(key_ext), 1u);
    EXPECT_EQ(jit_input.port_to_signal.at(key_out),
              jit_input.port_to_signal.at(key_ext));
}
