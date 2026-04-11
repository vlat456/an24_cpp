#include <gtest/gtest.h>

#include "blueprint_v2/flattener/flattener.h"
#include "blueprint_v2/elaboration/sim_export.h"
#include "blueprint_v2/library/blueprint_library.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/interface/interface.h"
#include "blueprint_v2/interface/port_descriptor.h"
#include "blueprint_v2/path/path.h"
#include "ui/core/interned_id.h"

#include <set>
#include <string>

namespace {

bp2::Blueprint::Node make_node(ui::StringInterner& I,
                               const char* id,
                               const char* type,
                               std::initializer_list<bp2::PortDescriptor> ports) {
    bp2::Blueprint::Node n;
    n.semantic.id = I.intern(id);
    n.semantic.type = I.intern(type);
    n.semantic.iface = bp2::Interface(ports);
    return n;
}

bp2::PortDescriptor in_port(ui::StringInterner& I, const char* name, PortType t = PortType::V) {
    return {I.intern(name), ::domain_for_port_type(t), bp2::Direction::Input, t};
}

bp2::PortDescriptor out_port(ui::StringInterner& I, const char* name, PortType t = PortType::V) {
    return {I.intern(name), ::domain_for_port_type(t), bp2::Direction::Output, t};
}

std::set<std::string> collect_conn_edges(const nlohmann::json& connections) {
    std::set<std::string> out;
    for (const auto& c : connections) {
        out.insert(c.at("from").get<std::string>() + "->" + c.at("to").get<std::string>());
    }
    return out;
}

std::set<std::string> collect_device_names(const nlohmann::json& devices) {
    std::set<std::string> out;
    for (const auto& d : devices) {
        out.insert(d.at("name").get<std::string>());
    }
    return out;
}

} // namespace


// ==============================================================================
// Single-level embedded blueprint instance with bridge rewrite.
//
// Topology:
//   root: bat ──v_out──→ [inst].vin
//   inst: vin(BlueprintInput) ──ext──→ r1.v_in
//
// Expected flat devices: bat, inst:vin, inst:r1
// Expected connections after bridge rewrite:
//   bat.v_out → inst:vin.ext   (bridge rewrite: bat.vin → inst:vin.ext)
//   inst:vin.ext → inst:r1.v_in
// ==============================================================================

TEST(ExportFlattenerParity, SingleLevelEmbeddedBridgeRewrite) {
    ui::StringInterner I;
    bp2::PathArena arena(I);
    bp2::BlueprintLibrary library;

    // Build inner blueprint for the instance: bridge(vin) → r1
    bp2::Blueprint inner;
    inner = inner.with_interface(bp2::Interface({in_port(I, "vin")}));

    // BlueprintInput bridge node for "vin"
    bp2::Blueprint::Node bridge;
    bridge.semantic.id = I.intern("vin");
    bridge.semantic.type = I.intern("BlueprintInput");
    bridge.semantic.iface = bp2::Interface({
        in_port(I, "port"),
        out_port(I, "ext"),
    });
    inner = inner.with_node(std::move(bridge));

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
    inst.kind = bp2::Blueprint::Node::Kind::BlueprintInstance;
    inst.semantic.id = I.intern("inst");
    inst.semantic.type = I.intern("EmbeddedType");
    inst.source = bp2::Blueprint::Node::BlueprintSource::make_embedded(
        I.intern("EmbeddedType"),
        std::make_unique<bp2::Blueprint>(inner));
    root = root.with_node(std::move(inst));

    // Root wire: bat.v_out → inst.vin
    bp2::Blueprint::Wire rw;
    rw.id = I.intern("rw1");
    rw.source = {I.intern("bat"), I.intern("v_out")};
    rw.target = {I.intern("inst"), I.intern("vin")};
    rw.domain = Domain::Electrical;
    root = root.with_wire(std::move(rw));

    // Flatten and export
    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(root, arena);

    auto exported = bp2::elaboration::to_simulation_export(netlist, arena, I, nullptr);

    // Verify devices
    auto devices = collect_device_names(exported.devices);
    EXPECT_TRUE(devices.count("bat")) << "Missing device: bat";
    EXPECT_TRUE(devices.count("inst:vin")) << "Missing device: inst:vin";
    EXPECT_TRUE(devices.count("inst:r1")) << "Missing device: inst:r1";

    // Verify connections — the critical check is that bridge rewrite produces
    // "inst:vin.ext" rather than "inst.vin" for the boundary crossing.
    auto edges = collect_conn_edges(exported.connections);

    // bat.v_out must connect to inst:vin.ext (bridge rewrite)
    EXPECT_TRUE(edges.count("bat.v_out->inst:vin.ext")
             || edges.count("inst:vin.ext->bat.v_out"))
        << "Bridge rewrite failed: expected bat.v_out <-> inst:vin.ext connection";

    // inst:vin.ext must connect to inst:r1.v_in
    EXPECT_TRUE(edges.count("inst:vin.ext->inst:r1.v_in")
             || edges.count("inst:r1.v_in->inst:vin.ext"))
        << "Inner connection failed: expected inst:vin.ext <-> inst:r1.v_in";

    // Must NOT see the raw unresolved "inst.vin" in any connection
    for (const auto& edge : edges) {
        EXPECT_EQ(edge.find("inst.vin"), std::string::npos)
            << "Found raw unresolved bridge reference in: " << edge;
    }
}


// ==============================================================================
// 3-level nesting: root → mid(embedded) → sub(embedded) → leaf
//
// Topology:
//   root: bat ──v_out──→ [mid].vin
//   mid:  vin(bridge) ──ext──→ [sub].pin
//   sub:  pin(bridge) ──ext──→ r1.v_in
//
// Expected flat components: bat, mid:vin, mid:sub:pin, mid:sub:r1
// Expected connections after bridge rewrite:
//   bat.v_out       → mid:vin.ext       (root-level bridge rewrite)
//   mid:vin.ext     → mid:sub:pin.ext   (mid-level bridge rewrite)
//   mid:sub:pin.ext → mid:sub:r1.v_in   (inner direct connection)
// ==============================================================================

TEST(ExportFlattenerParity, ThreeLevelNestedBridgeRewrite) {
    ui::StringInterner I;
    bp2::PathArena arena(I);
    bp2::BlueprintLibrary library;

    // ---- innermost "sub" blueprint: pin(bridge) → r1 ----
    bp2::Blueprint sub_bp;
    sub_bp = sub_bp.with_interface(bp2::Interface({in_port(I, "pin")}));

    bp2::Blueprint::Node sub_bridge;
    sub_bridge.semantic.id = I.intern("pin");
    sub_bridge.semantic.type = I.intern("BlueprintInput");
    sub_bridge.semantic.iface = bp2::Interface({
        in_port(I, "port"),
        out_port(I, "ext"),
    });
    sub_bp = sub_bp.with_node(std::move(sub_bridge));

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

    bp2::Blueprint::Node mid_bridge;
    mid_bridge.semantic.id = I.intern("vin");
    mid_bridge.semantic.type = I.intern("BlueprintInput");
    mid_bridge.semantic.iface = bp2::Interface({
        in_port(I, "port"),
        out_port(I, "ext"),
    });
    mid_bp = mid_bp.with_node(std::move(mid_bridge));

    // Embedded sub instance
    bp2::Blueprint::Node sub_inst;
    sub_inst.kind = bp2::Blueprint::Node::Kind::BlueprintInstance;
    sub_inst.semantic.id = I.intern("sub");
    sub_inst.semantic.type = I.intern("SubType");
    sub_inst.source = bp2::Blueprint::Node::BlueprintSource::make_embedded(
        I.intern("SubType"),
        std::make_unique<bp2::Blueprint>(sub_bp));
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
    mid_inst.kind = bp2::Blueprint::Node::Kind::BlueprintInstance;
    mid_inst.semantic.id = I.intern("mid");
    mid_inst.semantic.type = I.intern("MidType");
    mid_inst.source = bp2::Blueprint::Node::BlueprintSource::make_embedded(
        I.intern("MidType"),
        std::make_unique<bp2::Blueprint>(mid_bp));
    root = root.with_node(std::move(mid_inst));

    bp2::Blueprint::Wire rw;
    rw.id = I.intern("rw1");
    rw.source = {I.intern("bat"), I.intern("v_out")};
    rw.target = {I.intern("mid"), I.intern("vin")};
    rw.domain = Domain::Electrical;
    root = root.with_wire(std::move(rw));

    // Flatten and export
    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(root, arena);

    auto exported = bp2::elaboration::to_simulation_export(netlist, arena, I, nullptr);

    // Verify devices
    auto devices = collect_device_names(exported.devices);
    EXPECT_TRUE(devices.count("bat")) << "Missing device: bat";
    EXPECT_TRUE(devices.count("mid:vin")) << "Missing device: mid:vin";
    EXPECT_TRUE(devices.count("mid:sub:pin")) << "Missing device: mid:sub:pin";
    EXPECT_TRUE(devices.count("mid:sub:r1")) << "Missing device: mid:sub:r1";

    // Verify connections with bridge rewrite at each level
    auto edges = collect_conn_edges(exported.connections);

    // bat.v_out → mid:vin.ext  (root→mid bridge rewrite)
    EXPECT_TRUE(edges.count("bat.v_out->mid:vin.ext")
             || edges.count("mid:vin.ext->bat.v_out"))
        << "Level-1 bridge rewrite failed: expected bat.v_out <-> mid:vin.ext";

    // mid:vin.ext → mid:sub:pin.ext  (mid→sub bridge rewrite)
    EXPECT_TRUE(edges.count("mid:vin.ext->mid:sub:pin.ext")
             || edges.count("mid:sub:pin.ext->mid:vin.ext"))
        << "Level-2 bridge rewrite failed: expected mid:vin.ext <-> mid:sub:pin.ext";

    // mid:sub:pin.ext → mid:sub:r1.v_in  (inner direct connection)
    EXPECT_TRUE(edges.count("mid:sub:pin.ext->mid:sub:r1.v_in")
             || edges.count("mid:sub:r1.v_in->mid:sub:pin.ext"))
        << "Inner connection failed: expected mid:sub:pin.ext <-> mid:sub:r1.v_in";

    // Must NOT see raw unresolved references like "mid.vin" or "mid:sub.pin"
    for (const auto& edge : edges) {
        EXPECT_EQ(edge.find("mid.vin"), std::string::npos)
            << "Found raw unresolved bridge reference 'mid.vin' in: " << edge;
        EXPECT_EQ(edge.find("mid:sub.pin"), std::string::npos)
            << "Found raw unresolved bridge reference 'mid:sub.pin' in: " << edge;
    }
}


// ==============================================================================
// BlueprintOutput bridge: output side of a blueprint instance.
//
// Topology:
//   inst: r1.v_out → vout(BlueprintOutput).ext
//   root: [inst].vout → led.v_in
//
// Expected bridge rewrite: inst:vout.ext connects to led.v_in
// ==============================================================================

TEST(ExportFlattenerParity, BlueprintOutputBridgeRewrite) {
    ui::StringInterner I;
    bp2::PathArena arena(I);
    bp2::BlueprintLibrary library;

    // Inner blueprint: r1 → vout(bridge output)
    bp2::Blueprint inner;
    inner = inner.with_interface(bp2::Interface({out_port(I, "vout")}));

    inner = inner.with_node(make_node(I, "r1", "Resistor", {
        in_port(I, "v_in"),
        out_port(I, "v_out"),
    }));

    bp2::Blueprint::Node bridge;
    bridge.semantic.id = I.intern("vout");
    bridge.semantic.type = I.intern("BlueprintOutput");
    bridge.semantic.iface = bp2::Interface({
        in_port(I, "ext"),
        out_port(I, "port"),
    });
    inner = inner.with_node(std::move(bridge));

    bp2::Blueprint::Wire iw;
    iw.id = I.intern("iw1");
    iw.source = {I.intern("r1"), I.intern("v_out")};
    iw.target = {I.intern("vout"), I.intern("ext")};
    iw.domain = Domain::Electrical;
    inner = inner.with_wire(std::move(iw));

    // Root blueprint
    bp2::Blueprint root;

    bp2::Blueprint::Node inst;
    inst.kind = bp2::Blueprint::Node::Kind::BlueprintInstance;
    inst.semantic.id = I.intern("inst");
    inst.semantic.type = I.intern("EmbeddedType");
    inst.source = bp2::Blueprint::Node::BlueprintSource::make_embedded(
        I.intern("EmbeddedType"),
        std::make_unique<bp2::Blueprint>(inner));
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

    // Flatten and export
    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(root, arena);

    auto exported = bp2::elaboration::to_simulation_export(netlist, arena, I, nullptr);

    auto edges = collect_conn_edges(exported.connections);

    // inst:vout.ext must connect to led.v_in (bridge rewrite on output side)
    EXPECT_TRUE(edges.count("inst:vout.ext->led.v_in")
             || edges.count("led.v_in->inst:vout.ext"))
        << "BlueprintOutput bridge rewrite failed: expected inst:vout.ext <-> led.v_in";

    // Must NOT see raw "inst.vout"
    for (const auto& edge : edges) {
        EXPECT_EQ(edge.find("inst.vout"), std::string::npos)
            << "Found raw unresolved bridge reference 'inst.vout' in: " << edge;
    }
}
