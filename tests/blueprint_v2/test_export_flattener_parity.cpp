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

TEST(ExportParity, EmbeddedBridgeRewritesCollapsedPort) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    bp2::Blueprint inner;
    inner = inner.with_node(make_node(I, "vin", "BlueprintInput", {
        out_port(I, "ext"),
    }));
    inner = inner.with_node(make_node(I, "load", "Resistor", {
        in_port(I, "v_in"),
        out_port(I, "v_out"),
    }));

    bp2::Blueprint::Wire iw;
    iw.id = I.intern("iw");
    iw.source = arena.make_port(arena.make_node(arena.root(), I.intern("vin")), I.intern("ext"));
    iw.target = arena.make_port(arena.make_node(arena.root(), I.intern("load")), I.intern("v_in"));
    iw.domain = Domain::Electrical;
    inner = inner.with_wire(std::move(iw));

    bp2::Blueprint root;
    root = root.with_node(make_node(I, "src", "Battery", {
        out_port(I, "v_out"),
    }));
    root = root.with_node(make_node(I, "comp1", "Composite", {
        in_port(I, "vin"),
    }));

    auto nested = bp2::Blueprint::Nested::make_embedded(
        I.intern("comp1"), I.intern("Composite"), std::make_unique<bp2::Blueprint>(inner));
    root = root.with_nested(std::move(nested));

    bp2::Blueprint::Wire rw;
    rw.id = I.intern("rw");
    rw.source = arena.make_port(arena.make_node(arena.root(), I.intern("src")), I.intern("v_out"));
    rw.target = arena.make_port(arena.make_node(arena.root(), I.intern("comp1")), I.intern("vin"));
    rw.domain = Domain::Electrical;
    root = root.with_wire(std::move(rw));

    bp2::BlueprintLibrary lib;
    bp2::Flattener flattener(lib);
    auto netlist = flattener.flatten(root, arena);
    auto exported = bp2::elaboration::to_simulation_export(netlist, arena, I, nullptr);

    auto edges = collect_conn_edges(exported.connections);
    EXPECT_TRUE(edges.count("src.v_out->comp1:vin.ext") > 0)
        << "collapsed composite port must rewrite to child bridge ext endpoint";
}

TEST(ExportParity, ReferenceNestedUsesLibraryAndExportsNestedDevices) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    bp2::Blueprint ref_bp;
    ref_bp = ref_bp.with_interface(bp2::Interface({
        in_port(I, "v_in"),
        out_port(I, "v_out"),
    }));
    ref_bp = ref_bp.with_node(make_node(I, "r1", "Resistor", {
        in_port(I, "v_in"),
        out_port(I, "v_out"),
    }));

    bp2::Blueprint::Wire rw1;
    rw1.id = I.intern("rw1");
    rw1.source = arena.make_port(arena.root(), I.intern("v_in"));
    rw1.target = arena.make_port(arena.make_node(arena.root(), I.intern("r1")), I.intern("v_in"));
    rw1.domain = Domain::Electrical;
    ref_bp = ref_bp.with_wire(std::move(rw1));

    bp2::Blueprint::Wire rw2;
    rw2.id = I.intern("rw2");
    rw2.source = arena.make_port(arena.make_node(arena.root(), I.intern("r1")), I.intern("v_out"));
    rw2.target = arena.make_port(arena.root(), I.intern("v_out"));
    rw2.domain = Domain::Electrical;
    ref_bp = ref_bp.with_wire(std::move(rw2));

    bp2::Blueprint root;
    root = root.with_node(make_node(I, "src", "Battery", { out_port(I, "v_out") }));
    root = root.with_node(make_node(I, "sink", "LED", { in_port(I, "v_in") }));

    auto ref_nested = bp2::Blueprint::Nested::make_reference(
        I.intern("ref1"),
        I.intern("RefType"),
        bp2::Interface({
            in_port(I, "v_in"),
            out_port(I, "v_out"),
        }));
    root = root.with_nested(std::move(ref_nested));

    bp2::Blueprint::Wire w1;
    w1.id = I.intern("w1");
    w1.source = arena.make_port(arena.make_node(arena.root(), I.intern("src")), I.intern("v_out"));
    w1.target = arena.make_port(arena.make_nested(arena.root(), I.intern("ref1")), I.intern("v_in"));
    w1.domain = Domain::Electrical;
    root = root.with_wire(std::move(w1));

    bp2::Blueprint::Wire w2;
    w2.id = I.intern("w2");
    w2.source = arena.make_port(arena.make_nested(arena.root(), I.intern("ref1")), I.intern("v_out"));
    w2.target = arena.make_port(arena.make_node(arena.root(), I.intern("sink")), I.intern("v_in"));
    w2.domain = Domain::Electrical;
    root = root.with_wire(std::move(w2));

    bp2::BlueprintLibrary lib;
    lib.add(I.intern("RefType"), ref_bp);

    bp2::Flattener flattener(lib);
    auto netlist = flattener.flatten(root, arena);
    auto exported = bp2::elaboration::to_simulation_export(netlist, arena, I, nullptr);

    bool found_ref_child = false;
    for (const auto& d : exported.devices) {
        const std::string name = d.at("name").get<std::string>();
        if (name == "ref1:r1") {
            found_ref_child = true;
            break;
        }
    }
    EXPECT_TRUE(found_ref_child) << "reference nested blueprint should be exported via library flattening";

    auto edges = collect_conn_edges(exported.connections);
    EXPECT_TRUE(edges.count("src.v_out->ref1:r1.v_in") > 0);
    EXPECT_TRUE(edges.count("ref1:r1.v_out->sink.v_in") > 0);
}

// ==============================================================================
// 3-level nesting: root → mid(embedded) → sub(embedded) → leaf
//
// Topology:
//   root: bat ──v_out──→ [mid].vin
//   mid:  vin(bridge) ──ext──→ [sub].pin
//   sub:  pin(bridge) ──ext──→ r1.v_in
//
// Expected flat components: bat, mid, mid:vin, mid:sub, mid:sub:pin, mid:sub:r1
// Expected connections after bridge rewrite:
//   bat.v_out       → mid:vin.ext       (root-level bridge rewrite)
//   mid:vin.ext     → mid:sub:pin.ext   (mid-level bridge rewrite)
//   mid:sub:pin.ext → mid:sub:r1.v_in   (inner direct connection)
// ==============================================================================

TEST(ExportParity, ThreeLevelNestedBridgeRewrite) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    // === Innermost blueprint (sub): bridge "pin" → resistor "r1" ===
    bp2::Blueprint inner;
    inner = inner.with_node(make_node(I, "pin", "BlueprintInput", {
        out_port(I, "ext"),
    }));
    inner = inner.with_node(make_node(I, "r1", "Resistor", {
        in_port(I, "v_in"),
        out_port(I, "v_out"),
    }));
    {
        bp2::Blueprint::Wire w;
        w.id = I.intern("inner_w");
        w.source = arena.make_port(arena.make_node(arena.root(), I.intern("pin")), I.intern("ext"));
        w.target = arena.make_port(arena.make_node(arena.root(), I.intern("r1")), I.intern("v_in"));
        w.domain = Domain::Electrical;
        inner = inner.with_wire(std::move(w));
    }

    // === Middle blueprint (mid): bridge "vin" → collapsed node "sub" ===
    bp2::Blueprint mid;
    mid = mid.with_node(make_node(I, "vin", "BlueprintInput", {
        out_port(I, "ext"),
    }));
    mid = mid.with_node(make_node(I, "sub", "Composite", {
        in_port(I, "pin"),
    }));
    {
        bp2::Blueprint::Wire w;
        w.id = I.intern("mid_w");
        w.source = arena.make_port(arena.make_node(arena.root(), I.intern("vin")), I.intern("ext"));
        w.target = arena.make_port(arena.make_node(arena.root(), I.intern("sub")), I.intern("pin"));
        w.domain = Domain::Electrical;
        mid = mid.with_wire(std::move(w));
    }
    auto sub_nested = bp2::Blueprint::Nested::make_embedded(
        I.intern("sub"), I.intern("Composite"), std::make_unique<bp2::Blueprint>(inner));
    mid = mid.with_nested(std::move(sub_nested));

    // === Root blueprint: bat → collapsed node "mid" ===
    bp2::Blueprint root;
    root = root.with_node(make_node(I, "bat", "Battery", {
        out_port(I, "v_out"),
    }));
    root = root.with_node(make_node(I, "mid", "Composite", {
        in_port(I, "vin"),
    }));
    {
        bp2::Blueprint::Wire w;
        w.id = I.intern("root_w");
        w.source = arena.make_port(arena.make_node(arena.root(), I.intern("bat")), I.intern("v_out"));
        w.target = arena.make_port(arena.make_node(arena.root(), I.intern("mid")), I.intern("vin"));
        w.domain = Domain::Electrical;
        root = root.with_wire(std::move(w));
    }
    auto mid_nested = bp2::Blueprint::Nested::make_embedded(
        I.intern("mid"), I.intern("Composite"), std::make_unique<bp2::Blueprint>(mid));
    root = root.with_nested(std::move(mid_nested));

    // === Flatten + export ===
    bp2::BlueprintLibrary lib;
    bp2::Flattener flattener(lib);
    auto netlist = flattener.flatten(root, arena);
    auto exported = bp2::elaboration::to_simulation_export(netlist, arena, I, nullptr);

    // --- Verify devices ---
    auto devices = collect_device_names(exported.devices);
    EXPECT_TRUE(devices.count("bat"))          << "root leaf node";
    EXPECT_TRUE(devices.count("mid:vin"))      << "mid-level bridge node";
    EXPECT_TRUE(devices.count("mid:sub:pin"))  << "inner bridge node";
    EXPECT_TRUE(devices.count("mid:sub:r1"))   << "innermost leaf node";

    // --- Verify connections (bridge rewrites at each level) ---
    auto edges = collect_conn_edges(exported.connections);

    EXPECT_TRUE(edges.count("bat.v_out->mid:vin.ext") > 0)
        << "root→mid bridge rewrite: collapsed node 'mid.vin' must map to 'mid:vin.ext'";

    EXPECT_TRUE(edges.count("mid:vin.ext->mid:sub:pin.ext") > 0)
        << "mid→sub bridge rewrite: collapsed node 'mid:sub.pin' must map to 'mid:sub:pin.ext'";

    EXPECT_TRUE(edges.count("mid:sub:pin.ext->mid:sub:r1.v_in") > 0)
        << "inner direct connection: bridge 'pin.ext' to leaf 'r1.v_in'";
}
