#include <gtest/gtest.h>
#include <set>
#include "ui/core/interned_id.h"
#include "blueprint_v2/flattener/flat_netlist.h"
#include "blueprint_v2/flattener/flattener.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/library/blueprint_library.h"
#include "blueprint_v2/path/path.h"
#include "../bp2_test_helpers.h"

// ==================================================================
// Helper: Create test library with standard components
// ==================================================================

static bp2::BlueprintLibrary make_test_library(ui::StringInterner& interner) {
    bp2::BlueprintLibrary library;
    
    bp2::Blueprint bat;
    bat = bat.with_interface(bp2::Interface({
        {interner.intern("v_in"), Domain::Electrical, bp2::Direction::Input},
        {interner.intern("v_out"), Domain::Electrical, bp2::Direction::Output},
    }));
    library.add(interner.intern("Battery"), bat);
    
    bp2::Blueprint res;
    res = res.with_interface(bp2::Interface({
        {interner.intern("in"), Domain::Electrical, bp2::Direction::Input},
        {interner.intern("out"), Domain::Electrical, bp2::Direction::Output},
    }));
    library.add(interner.intern("Resistor"), res);
    
    bp2::Blueprint gnd;
    gnd = gnd.with_interface(bp2::Interface({
        {interner.intern("gnd"), Domain::Electrical, bp2::Direction::InOut},
    }));
    library.add(interner.intern("Ground"), gnd);
    
    bp2::Blueprint led;
    led = led.with_interface(bp2::Interface({
        {interner.intern("v_in"), Domain::Electrical, bp2::Direction::Input},
        {interner.intern("ground"), Domain::Electrical, bp2::Direction::InOut},
    }));
    library.add(interner.intern("LED"), led);
    
    return library;
}

static bp2::Blueprint::Node make_bridge_node(ui::StringInterner& I,
                                             const char* id,
                                             bool input_side,
                                             Domain domain,
                                             PortType type = PortType::V) {
    bp2::Blueprint::Node bridge;
    bridge.semantic.id = I.intern(id);
    bridge.semantic.type = I.intern("BridgePort");
    bridge.view.name = id;
    bridge.content = bp2::Blueprint::Node::BridgePortData{
        I.intern(id),
        input_side ? bp2::Blueprint::Node::BridgePortSide::Input
                   : bp2::Blueprint::Node::BridgePortSide::Output,
        type,
        input_side
            ? bp2::Interface({
                make_port(I, "ext", domain, bp2::Direction::Input, type),
                make_port(I, "port", domain, bp2::Direction::Output, type),
            })
            : bp2::Interface({
                make_port(I, "port", domain, bp2::Direction::Input, type),
                make_port(I, "ext", domain, bp2::Direction::Output, type),
            })
    };
    return bridge;
}

// ==================================================================
// Step 6.2: FlatNetlist data structures
// ==================================================================

TEST(FlatNetlist, EmptyByDefault) {
    bp2::FlatNetlist netlist;
    EXPECT_TRUE(netlist.components.empty());
    EXPECT_EQ(netlist.signal_count, 0u);
}

TEST(FlatNetlist, ComponentStruct) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    bp2::FlatNetlist::Component comp;
    comp.path = arena.make_node(arena.root(), interner.intern("bat1"));
    comp.type = interner.intern("Battery");
    comp.port_signals.push_back({interner.intern("v_out"), 0});
    comp.port_signals.push_back({interner.intern("v_in"), 1});

    EXPECT_EQ(interner.resolve(comp.type), "Battery");
    EXPECT_EQ(comp.port_signals.size(), 2u);
    EXPECT_EQ(comp.port_signals[0].second, 0u);
}

// ==================================================================
// Step 6.3: Flatten single node, no wires
// ==================================================================

TEST(Flattener, SingleNodeNoWires) {
    ui::StringInterner interner;
    auto library = make_test_library(interner);
    bp2::PathArena arena(interner);

    bp2::Blueprint bp;
    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("bat1");
    node.semantic.type = interner.intern("Battery");
    node.component().iface = library.find(interner.intern("Battery"))->iface();
    bp = bp.with_node(std::move(node));

    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(bp, arena);

    EXPECT_EQ(netlist.components.size(), 1u);
    EXPECT_EQ(interner.resolve(netlist.components[0].type), "Battery");
    EXPECT_EQ(netlist.components[0].port_signals.size(), 2u);
    EXPECT_GE(netlist.signal_count, 2u);
}

// ==================================================================
// Step 6.4: Flatten two nodes with one wire
// ==================================================================

TEST(Flattener, TwoNodesOneWire) {
    ui::StringInterner interner;
    auto library = make_test_library(interner);
    bp2::PathArena arena(interner);

    bp2::Blueprint bp;

    bp2::Blueprint::Node bat;
    bat.semantic.id = interner.intern("bat1");
    bat.semantic.type = interner.intern("Battery");
    bat.component().iface = library.find(interner.intern("Battery"))->iface();
    bp = bp.with_node(std::move(bat));

    bp2::Blueprint::Node res;
    res.semantic.id = interner.intern("r1");
    res.semantic.type = interner.intern("Resistor");
    res.component().iface = library.find(interner.intern("Resistor"))->iface();
    bp = bp.with_node(std::move(res));

    bp2::Blueprint::Wire w;
    w.id = interner.intern("w1");
    w.source = bp2::WireEndpoint{interner.intern("bat1"), interner.intern("v_out")};
    w.target = bp2::WireEndpoint{interner.intern("r1"), interner.intern("in")};
    w.domain = Domain::Electrical;
    bp = bp.with_wire(std::move(w));

    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(bp, arena);

    EXPECT_EQ(netlist.components.size(), 2u);

    bp2::SignalIndex bat_vout = 0xFFFFFFFF;
    bp2::SignalIndex r1_in = 0xFFFFFFFF;
    for (auto const& comp : netlist.components) {
        for (auto const& [port_name, sig] : comp.port_signals) {
            if (interner.resolve(port_name) == "v_out" &&
                interner.resolve(comp.type) == "Battery") {
                bat_vout = sig;
            }
            if (interner.resolve(port_name) == "in" &&
                interner.resolve(comp.type) == "Resistor") {
                r1_in = sig;
            }
        }
    }
    EXPECT_NE(bat_vout, 0xFFFFFFFFu);
    EXPECT_EQ(bat_vout, r1_in);
}

// ==================================================================
// Step 6.5: Three nodes chained, verify signal count
// ==================================================================

TEST(Flattener, ThreeNodesChainedSignalCount) {
    ui::StringInterner interner;
    auto library = make_test_library(interner);
    bp2::PathArena arena(interner);

    bp2::Blueprint bp;

    bp2::Blueprint::Node bat;
    bat.semantic.id = interner.intern("bat1");
    bat.semantic.type = interner.intern("Battery");
    bat.component().iface = library.find(interner.intern("Battery"))->iface();
    bp = bp.with_node(std::move(bat));

    bp2::Blueprint::Node res;
    res.semantic.id = interner.intern("r1");
    res.semantic.type = interner.intern("Resistor");
    res.component().iface = library.find(interner.intern("Resistor"))->iface();
    bp = bp.with_node(std::move(res));

    bp2::Blueprint::Node led;
    led.semantic.id = interner.intern("led1");
    led.semantic.type = interner.intern("LED");
    led.component().iface = library.find(interner.intern("LED"))->iface();
    bp = bp.with_node(std::move(led));

    bp2::Blueprint::Wire w1;
    w1.id = interner.intern("w1");
    w1.source = bp2::WireEndpoint{interner.intern("bat1"), interner.intern("v_out")};
    w1.target = bp2::WireEndpoint{interner.intern("r1"), interner.intern("in")};
    w1.domain = Domain::Electrical;
    bp = bp.with_wire(std::move(w1));

    bp2::Blueprint::Wire w2;
    w2.id = interner.intern("w2");
    w2.source = bp2::WireEndpoint{interner.intern("r1"), interner.intern("out")};
    w2.target = bp2::WireEndpoint{interner.intern("led1"), interner.intern("v_in")};
    w2.domain = Domain::Electrical;
    bp = bp.with_wire(std::move(w2));

    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(bp, arena);

    EXPECT_EQ(netlist.components.size(), 3u);

    std::set<bp2::SignalIndex> unique_sigs;
    for (auto const& comp : netlist.components) {
        for (auto const& [_, sig] : comp.port_signals) {
            unique_sigs.insert(sig);
        }
    }
    EXPECT_EQ(unique_sigs.size(), 4u);
}

// ==================================================================
// Step 6.6: Nested blueprint expansion (one level)
// ==================================================================


// ==================================================================
// Step 6.8: Params preserved through flattening
// ==================================================================

TEST(Flattener, ParamsPreserved) {
    ui::StringInterner interner;
    auto library = make_test_library(interner);
    bp2::PathArena arena(interner);

    bp2::Blueprint bp;
    bp2::Blueprint::Node bat;
    bat.semantic.id = interner.intern("bat1");
    bat.semantic.type = interner.intern("Battery");
    bat.component().iface = library.find(interner.intern("Battery"))->iface();
    bat.semantic.params[interner.intern("v_nominal")] = 28.0f;
    bat.semantic.params[interner.intern("capacity")] = 24.0f;
    bp = bp.with_node(std::move(bat));

    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(bp, arena);

    ASSERT_EQ(netlist.components.size(), 1u);
    auto v_nom_id = interner.intern("v_nominal");
    auto cap_id = interner.intern("capacity");
    EXPECT_FLOAT_EQ(netlist.components[0].params.at(v_nom_id), 28.0f);
    EXPECT_FLOAT_EQ(netlist.components[0].params.at(cap_id), 24.0f);
}

// ==================================================================
// Empty blueprint produces empty netlist
// ==================================================================

TEST(Flattener, FlattenEmptyBlueprint) {
    ui::StringInterner interner;
    auto library = make_test_library(interner);
    bp2::PathArena arena(interner);

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("empty"));

    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(bp, arena);

    EXPECT_TRUE(netlist.components.empty());
    EXPECT_EQ(netlist.signal_count, 0u);
}

// ==================================================================
// Regression test for #57: unresolved nested blueprint must throw
// ==================================================================



// ==================================================================
// Regression: composite-within-composite must not emit host as leaf
// ==================================================================

// ==================================================================
// Helper for #112 regression tests
// ==================================================================

static bp2::PortDescriptor make_port(ui::StringInterner& I, const char* name,
                                     Domain domain, bp2::Direction dir) {
    return {I.intern(name), domain, dir};
}

/// Verify the IR self-consistency invariant: every path in Signal::connected_ports
/// must have a parent node path that corresponds to an emitted component.
static void assert_no_phantom_paths(
    const bp2::FlatNetlist& netlist,
    bp2::PathArena& arena,
    const ui::StringInterner& interner) {

    // Collect all emitted component node paths
    std::set<std::string> emitted;
    for (auto const& comp : netlist.components) {
        emitted.insert(arena.to_string(comp.path));
    }

    for (auto const& sig : netlist.signals) {
        for (auto port_path : sig.connected_ports) {
            if (port_path.kind() != bp2::PathKind::Port) continue;
            bp2::Path parent = arena.parent(port_path);
            std::string parent_str = arena.to_string(parent);
            EXPECT_TRUE(emitted.count(parent_str))
                << "Phantom path detected: signal " << sig.index
                << " contains port path '" << arena.to_string(port_path)
                << "' whose parent node '" << parent_str
                << "' is NOT an emitted device";
        }
    }
}

// ==================================================================
// #112 Regression: single-level embedded instance — no phantom paths
// ==================================================================

TEST(Flattener, Regression112_SingleLevelNoPhantomPaths) {
    ui::StringInterner I;
    bp2::PathArena arena(I);
    bp2::BlueprintLibrary library;

    // Inner blueprint: bridge(vin) → r1
    bp2::Blueprint inner;
    inner = inner.with_interface(bp2::Interface({
        make_port(I, "vin", Domain::Electrical, bp2::Direction::Input),
    }));

    bp2::Blueprint::Node bridge = make_bridge_node(I, "vin", true, Domain::Electrical);
    inner = inner.with_node(std::move(bridge));

    bp2::Blueprint::Node r1;
    r1.semantic.id = I.intern("r1");
    r1.semantic.type = I.intern("Resistor");
    r1.component().iface = bp2::Interface({
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input),
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output),
    });
    inner = inner.with_node(std::move(r1));

    bp2::Blueprint::Wire iw;
    iw.id = I.intern("iw1");
    iw.source = {I.intern("vin"), I.intern("ext")};
    iw.target = {I.intern("r1"), I.intern("v_in")};
    iw.domain = Domain::Electrical;
    inner = inner.with_wire(std::move(iw));

    // Root: bat → [inst].vin
    bp2::Blueprint root;
    bp2::Blueprint::Node bat;
    bat.semantic.id = I.intern("bat");
    bat.semantic.type = I.intern("Battery");
    bat.component().iface = bp2::Interface({
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output),
    });
    root = root.with_node(std::move(bat));

    bp2::Blueprint::Node inst;
    inst.semantic.id = I.intern("inst");
    inst.semantic.type = I.intern("Embedded");
    inst.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(inner.with_id(I.intern("Embedded"))))
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

    // Must emit bat, inst:vin, inst:r1 — NOT "inst" as leaf
    EXPECT_EQ(netlist.components.size(), 3u);

    std::set<std::string> emitted;
    for (auto const& comp : netlist.components) {
        emitted.insert(arena.to_string(comp.path));
    }
    EXPECT_TRUE(emitted.count("/bat"));
    EXPECT_TRUE(emitted.count("/inst/vin"));
    EXPECT_TRUE(emitted.count("/inst/r1"));

    // Core invariant: no phantom paths
    assert_no_phantom_paths(netlist, arena, I);

    // Verify signal connectivity: bat.v_out and inst:vin.ext on same signal
    bp2::SignalIndex bat_vout = UINT32_MAX, vin_ext = UINT32_MAX, r1_vin = UINT32_MAX;
    for (auto const& comp : netlist.components) {
        for (auto const& [pname, sig] : comp.port_signals) {
            std::string cpath = arena.to_string(comp.path);
            std::string pstr(I.resolve(pname));
            if (cpath == "/bat" && pstr == "v_out") bat_vout = sig;
            if (cpath == "/inst/vin" && pstr == "ext") vin_ext = sig;
            if (cpath == "/inst/r1" && pstr == "v_in") r1_vin = sig;
        }
    }
    EXPECT_NE(bat_vout, UINT32_MAX);
    EXPECT_NE(vin_ext, UINT32_MAX);
    EXPECT_NE(r1_vin, UINT32_MAX);
    EXPECT_EQ(bat_vout, vin_ext) << "bat.v_out and inst:vin.ext must be on same signal";
    EXPECT_EQ(vin_ext, r1_vin) << "inst:vin.ext and inst:r1.v_in must be on same signal";
}

// ==================================================================
// #112 Regression: v1-style bridge matching by label (bp_in_N)
// ==================================================================

TEST(Flattener, Regression112_V1LabelBasedBridgeMatch) {
    ui::StringInterner I;
    bp2::PathArena arena(I);
    bp2::BlueprintLibrary library;

    // Inner blueprint: bp_in_1 (label="feedback") → leaf
    bp2::Blueprint inner;
    inner = inner.with_interface(bp2::Interface({
        make_port(I, "feedback", Domain::Logical, bp2::Direction::Input),
    }));

    bp2::Blueprint::Node bridge = make_bridge_node(I, "bp_in_1", true, Domain::Logical);
    bridge.view.name = "feedback";  // v1 label-based matching
    inner = inner.with_node(std::move(bridge));

    bp2::Blueprint::Node leaf;
    leaf.semantic.id = I.intern("pi_1");
    leaf.semantic.type = I.intern("PI");
    leaf.component().iface = bp2::Interface({
        make_port(I, "feedback", Domain::Logical, bp2::Direction::Input),
        make_port(I, "output", Domain::Logical, bp2::Direction::Output),
    });
    inner = inner.with_node(std::move(leaf));

    bp2::Blueprint::Wire iw;
    iw.id = I.intern("iw1");
    iw.source = {I.intern("bp_in_1"), I.intern("port")};
    iw.target = {I.intern("pi_1"), I.intern("feedback")};
    iw.domain = Domain::Logical;
    inner = inner.with_wire(std::move(iw));

    // Root: value → [exciter].feedback
    bp2::Blueprint root;
    bp2::Blueprint::Node val;
    val.semantic.id = I.intern("value_1");
    val.semantic.type = I.intern("Value");
    val.component().iface = bp2::Interface({
        make_port(I, "o", Domain::Logical, bp2::Direction::Output),
    });
    root = root.with_node(std::move(val));

    bp2::Blueprint::Node exciter;
    exciter.semantic.id = I.intern("extract_inst_1");
    exciter.semantic.type = I.intern("RN-180-Exciter");
    exciter.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(inner.with_id(I.intern("RN-180-Exciter"))))
    };
    root = root.with_node(std::move(exciter));

    bp2::Blueprint::Wire rw;
    rw.id = I.intern("rw1");
    rw.source = {I.intern("value_1"), I.intern("o")};
    rw.target = {I.intern("extract_inst_1"), I.intern("feedback")};
    rw.domain = Domain::Logical;
    root = root.with_wire(std::move(rw));

    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(root, arena);

    // No phantom paths
    assert_no_phantom_paths(netlist, arena, I);

    // The bridge path should be /extract_inst_1/bp_in_1, not /extract_inst_1
    std::set<std::string> emitted;
    for (auto const& comp : netlist.components) {
        emitted.insert(arena.to_string(comp.path));
    }
    EXPECT_TRUE(emitted.count("/extract_inst_1/bp_in_1"));
    EXPECT_TRUE(emitted.count("/extract_inst_1/pi_1"));
    EXPECT_FALSE(emitted.count("/extract_inst_1")) << "Instance node must not be emitted as leaf";

    // Verify signal connectivity
    bp2::SignalIndex val_o = UINT32_MAX, bridge_ext = UINT32_MAX, pi_feedback = UINT32_MAX;
    for (auto const& comp : netlist.components) {
        for (auto const& [pname, sig] : comp.port_signals) {
            std::string cpath = arena.to_string(comp.path);
            std::string pstr(I.resolve(pname));
            if (cpath == "/value_1" && pstr == "o") val_o = sig;
            if (cpath == "/extract_inst_1/bp_in_1" && pstr == "ext") bridge_ext = sig;
            if (cpath == "/extract_inst_1/pi_1" && pstr == "feedback") pi_feedback = sig;
        }
    }
    EXPECT_EQ(val_o, bridge_ext)
        << "value_1.o and extract_inst_1:bp_in_1.ext must be on same signal";
    EXPECT_EQ(bridge_ext, pi_feedback)
        << "bp_in_1.ext and pi_1.feedback must be on same signal (via ext/port unification)";
}

// ==================================================================
// #112 Regression: 3-level nesting — no phantom paths at any level
// ==================================================================

TEST(Flattener, Regression112_ThreeLevelNesting_NoPhantomPaths) {
    ui::StringInterner I;
    bp2::PathArena arena(I);
    bp2::BlueprintLibrary library;

    // Innermost: pin(bridge) → r1
    bp2::Blueprint inner;
    inner = inner.with_interface(bp2::Interface({
        make_port(I, "pin", Domain::Electrical, bp2::Direction::Input),
    }));

    bp2::Blueprint::Node inner_bridge = make_bridge_node(I, "pin", true, Domain::Electrical);
    inner = inner.with_node(std::move(inner_bridge));

    bp2::Blueprint::Node r1;
    r1.semantic.id = I.intern("r1");
    r1.semantic.type = I.intern("Resistor");
    r1.component().iface = bp2::Interface({
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input),
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output),
    });
    inner = inner.with_node(std::move(r1));

    bp2::Blueprint::Wire inner_w;
    inner_w.id = I.intern("iw");
    inner_w.source = {I.intern("pin"), I.intern("ext")};
    inner_w.target = {I.intern("r1"), I.intern("v_in")};
    inner_w.domain = Domain::Electrical;
    inner = inner.with_wire(std::move(inner_w));

    // Middle: vin(bridge) → [sub].pin
    bp2::Blueprint mid;
    mid = mid.with_interface(bp2::Interface({
        make_port(I, "vin", Domain::Electrical, bp2::Direction::Input),
    }));

    bp2::Blueprint::Node mid_bridge = make_bridge_node(I, "vin", true, Domain::Electrical);
    mid = mid.with_node(std::move(mid_bridge));

    bp2::Blueprint::Node sub_inst;
    sub_inst.semantic.id = I.intern("sub");
    sub_inst.semantic.type = I.intern("SubType");
    sub_inst.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(inner.with_id(I.intern("SubType"))))
    };
    mid = mid.with_node(std::move(sub_inst));

    bp2::Blueprint::Wire mid_w;
    mid_w.id = I.intern("mw");
    mid_w.source = {I.intern("vin"), I.intern("ext")};
    mid_w.target = {I.intern("sub"), I.intern("pin")};
    mid_w.domain = Domain::Electrical;
    mid = mid.with_wire(std::move(mid_w));

    // Root: bat → [mid_inst].vin
    bp2::Blueprint root;

    bp2::Blueprint::Node bat;
    bat.semantic.id = I.intern("bat");
    bat.semantic.type = I.intern("Battery");
    bat.component().iface = bp2::Interface({
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output),
    });
    root = root.with_node(std::move(bat));

    bp2::Blueprint::Node mid_inst;
    mid_inst.semantic.id = I.intern("mid");
    mid_inst.semantic.type = I.intern("MidType");
    mid_inst.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(mid.with_id(I.intern("MidType"))))
    };
    root = root.with_node(std::move(mid_inst));

    bp2::Blueprint::Wire rw;
    rw.id = I.intern("rw");
    rw.source = {I.intern("bat"), I.intern("v_out")};
    rw.target = {I.intern("mid"), I.intern("vin")};
    rw.domain = Domain::Electrical;
    root = root.with_wire(std::move(rw));

    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(root, arena);

    // Devices: bat, mid:vin, mid:sub:pin, mid:sub:r1
    EXPECT_EQ(netlist.components.size(), 4u);
    assert_no_phantom_paths(netlist, arena, I);

    // All endpoints on same signal
    bp2::SignalIndex bat_vout = UINT32_MAX, mid_vin_ext = UINT32_MAX;
    bp2::SignalIndex sub_pin_ext = UINT32_MAX, r1_vin = UINT32_MAX;
    for (auto const& comp : netlist.components) {
        for (auto const& [pname, sig] : comp.port_signals) {
            std::string cpath = arena.to_string(comp.path);
            std::string pstr(I.resolve(pname));
            if (cpath == "/bat" && pstr == "v_out") bat_vout = sig;
            if (cpath == "/mid/vin" && pstr == "ext") mid_vin_ext = sig;
            if (cpath == "/mid/sub/pin" && pstr == "ext") sub_pin_ext = sig;
            if (cpath == "/mid/sub/r1" && pstr == "v_in") r1_vin = sig;
        }
    }
    EXPECT_EQ(bat_vout, mid_vin_ext);
    EXPECT_EQ(mid_vin_ext, sub_pin_ext);
    EXPECT_EQ(sub_pin_ext, r1_vin);
}

// ==================================================================
// #112 Regression: output bridge resolves correctly
// ==================================================================

TEST(Flattener, Regression112_OutputBridge_NoPhantomPaths) {
    ui::StringInterner I;
    bp2::PathArena arena(I);
    bp2::BlueprintLibrary library;

    // Inner: r1 → vout(output bridge)
    bp2::Blueprint inner;
    inner = inner.with_interface(bp2::Interface({
        make_port(I, "vout", Domain::Electrical, bp2::Direction::Output),
    }));

    bp2::Blueprint::Node r1;
    r1.semantic.id = I.intern("r1");
    r1.semantic.type = I.intern("Resistor");
    r1.component().iface = bp2::Interface({
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input),
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output),
    });
    inner = inner.with_node(std::move(r1));

    bp2::Blueprint::Node bridge = make_bridge_node(I, "vout", false, Domain::Electrical);
    inner = inner.with_node(std::move(bridge));

    bp2::Blueprint::Wire iw;
    iw.id = I.intern("iw");
    iw.source = {I.intern("r1"), I.intern("v_out")};
    iw.target = {I.intern("vout"), I.intern("ext")};
    iw.domain = Domain::Electrical;
    inner = inner.with_wire(std::move(iw));

    // Root: [inst].vout → led
    bp2::Blueprint root;

    bp2::Blueprint::Node inst;
    inst.semantic.id = I.intern("inst");
    inst.semantic.type = I.intern("Embedded");
    inst.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(inner.with_id(I.intern("Embedded"))))
    };
    root = root.with_node(std::move(inst));

    bp2::Blueprint::Node led;
    led.semantic.id = I.intern("led");
    led.semantic.type = I.intern("LED");
    led.component().iface = bp2::Interface({
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input),
    });
    root = root.with_node(std::move(led));

    bp2::Blueprint::Wire rw;
    rw.id = I.intern("rw");
    rw.source = {I.intern("inst"), I.intern("vout")};
    rw.target = {I.intern("led"), I.intern("v_in")};
    rw.domain = Domain::Electrical;
    root = root.with_wire(std::move(rw));

    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(root, arena);

    assert_no_phantom_paths(netlist, arena, I);

    // inst:vout.ext and led.v_in on same signal
    bp2::SignalIndex vout_ext = UINT32_MAX, led_vin = UINT32_MAX;
    for (auto const& comp : netlist.components) {
        for (auto const& [pname, sig] : comp.port_signals) {
            std::string cpath = arena.to_string(comp.path);
            std::string pstr(I.resolve(pname));
            if (cpath == "/inst/vout" && pstr == "ext") vout_ext = sig;
            if (cpath == "/led" && pstr == "v_in") led_vin = sig;
        }
    }
    EXPECT_EQ(vout_ext, led_vin);
}

// ==================================================================
// #112 Regression: bidirectional bridge (input + output on same instance)
// ==================================================================

TEST(Flattener, Regression112_BidirectionalBridge) {
    ui::StringInterner I;
    bp2::PathArena arena(I);
    bp2::BlueprintLibrary library;

    // Inner: bridge_in → r1 → bridge_out
    bp2::Blueprint inner;
    inner = inner.with_interface(bp2::Interface({
        make_port(I, "vin", Domain::Electrical, bp2::Direction::Input),
        make_port(I, "vout", Domain::Electrical, bp2::Direction::Output),
    }));

    bp2::Blueprint::Node bridge_in = make_bridge_node(I, "vin", true, Domain::Electrical);
    inner = inner.with_node(std::move(bridge_in));

    bp2::Blueprint::Node bridge_out = make_bridge_node(I, "vout", false, Domain::Electrical);
    inner = inner.with_node(std::move(bridge_out));

    bp2::Blueprint::Node r1;
    r1.semantic.id = I.intern("r1");
    r1.semantic.type = I.intern("Resistor");
    r1.component().iface = bp2::Interface({
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input),
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output),
    });
    inner = inner.with_node(std::move(r1));

    bp2::Blueprint::Wire w1;
    w1.id = I.intern("w1");
    w1.source = {I.intern("vin"), I.intern("ext")};
    w1.target = {I.intern("r1"), I.intern("v_in")};
    w1.domain = Domain::Electrical;
    inner = inner.with_wire(std::move(w1));

    bp2::Blueprint::Wire w2;
    w2.id = I.intern("w2");
    w2.source = {I.intern("r1"), I.intern("v_out")};
    w2.target = {I.intern("vout"), I.intern("ext")};
    w2.domain = Domain::Electrical;
    inner = inner.with_wire(std::move(w2));

    // Root: bat → [inst].vin, [inst].vout → led
    bp2::Blueprint root;

    bp2::Blueprint::Node bat;
    bat.semantic.id = I.intern("bat");
    bat.semantic.type = I.intern("Battery");
    bat.component().iface = bp2::Interface({
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output),
    });
    root = root.with_node(std::move(bat));

    bp2::Blueprint::Node inst;
    inst.semantic.id = I.intern("inst");
    inst.semantic.type = I.intern("Embedded");
    inst.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(inner.with_id(I.intern("Embedded"))))
    };
    root = root.with_node(std::move(inst));

    bp2::Blueprint::Node led;
    led.semantic.id = I.intern("led");
    led.semantic.type = I.intern("LED");
    led.component().iface = bp2::Interface({
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input),
    });
    root = root.with_node(std::move(led));

    bp2::Blueprint::Wire rw1;
    rw1.id = I.intern("rw1");
    rw1.source = {I.intern("bat"), I.intern("v_out")};
    rw1.target = {I.intern("inst"), I.intern("vin")};
    rw1.domain = Domain::Electrical;
    root = root.with_wire(std::move(rw1));

    bp2::Blueprint::Wire rw2;
    rw2.id = I.intern("rw2");
    rw2.source = {I.intern("inst"), I.intern("vout")};
    rw2.target = {I.intern("led"), I.intern("v_in")};
    rw2.domain = Domain::Electrical;
    root = root.with_wire(std::move(rw2));

    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(root, arena);

    // 5 components: bat, inst:vin, inst:r1, inst:vout, led
    EXPECT_EQ(netlist.components.size(), 5u);
    assert_no_phantom_paths(netlist, arena, I);

    // bat.v_out → inst:vin.ext → inst:r1.v_in (same signal)
    // inst:r1.v_out → inst:vout.ext → led.v_in (same signal)
    bp2::SignalIndex bat_vout = UINT32_MAX, vin_ext = UINT32_MAX;
    bp2::SignalIndex r1_vin = UINT32_MAX, r1_vout = UINT32_MAX;
    bp2::SignalIndex vout_ext = UINT32_MAX, led_vin = UINT32_MAX;
    for (auto const& comp : netlist.components) {
        for (auto const& [pname, sig] : comp.port_signals) {
            std::string cpath = arena.to_string(comp.path);
            std::string pstr(I.resolve(pname));
            if (cpath == "/bat" && pstr == "v_out") bat_vout = sig;
            if (cpath == "/inst/vin" && pstr == "ext") vin_ext = sig;
            if (cpath == "/inst/r1" && pstr == "v_in") r1_vin = sig;
            if (cpath == "/inst/r1" && pstr == "v_out") r1_vout = sig;
            if (cpath == "/inst/vout" && pstr == "ext") vout_ext = sig;
            if (cpath == "/led" && pstr == "v_in") led_vin = sig;
        }
    }
    EXPECT_EQ(bat_vout, vin_ext);
    EXPECT_EQ(vin_ext, r1_vin);
    EXPECT_EQ(r1_vout, vout_ext);
    EXPECT_EQ(vout_ext, led_vin);
    // The two signal groups must be different
    EXPECT_NE(bat_vout, r1_vout);
}

// ==================================================================
// #112 Regression: missing bridge node throws
// ==================================================================

TEST(Flattener, Regression112_MissingBridgeThrows) {
    ui::StringInterner I;
    bp2::PathArena arena(I);
    bp2::BlueprintLibrary library;

    // Inner blueprint with NO bridge nodes, but interface declares "vin"
    bp2::Blueprint inner;
    inner = inner.with_interface(bp2::Interface({
        make_port(I, "vin", Domain::Electrical, bp2::Direction::Input),
    }));
    // Only a leaf node, no bridge
    bp2::Blueprint::Node r1;
    r1.semantic.id = I.intern("r1");
    r1.semantic.type = I.intern("Resistor");
    r1.component().iface = bp2::Interface({
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input),
    });
    inner = inner.with_node(std::move(r1));

    bp2::Blueprint root;
    bp2::Blueprint::Node inst;
    inst.semantic.id = I.intern("inst");
    inst.semantic.type = I.intern("Embedded");
    inst.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(inner.with_id(I.intern("Embedded"))))
    };
    root = root.with_node(std::move(inst));

    bp2::Blueprint::Node bat;
    bat.semantic.id = I.intern("bat");
    bat.semantic.type = I.intern("Battery");
    bat.component().iface = bp2::Interface({
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output),
    });
    root = root.with_node(std::move(bat));

    bp2::Blueprint::Wire rw;
    rw.id = I.intern("rw");
    rw.source = {I.intern("bat"), I.intern("v_out")};
    rw.target = {I.intern("inst"), I.intern("vin")};
    rw.domain = Domain::Electrical;
    root = root.with_wire(std::move(rw));

    bp2::Flattener flattener(library);
    EXPECT_THROW(flattener.flatten(root, arena), std::logic_error);
}

// ==================================================================
// #112 Regression: library reference mode instances resolve correctly
// ==================================================================

TEST(Flattener, Regression112_LibraryReferenceInstance) {
    ui::StringInterner I;
    bp2::PathArena arena(I);
    bp2::BlueprintLibrary library;

    // Register a blueprint in the library (simulating v3 library composites)
    bp2::Blueprint lib_bp;
    lib_bp = lib_bp.with_interface(bp2::Interface({
        make_port(I, "in", Domain::Logical, bp2::Direction::Input),
        make_port(I, "out", Domain::Logical, bp2::Direction::Output),
    }));

    // Bridge node "in" with ID matching port name (v3 style)
    bp2::Blueprint::Node bridge_in = make_bridge_node(I, "in", true, Domain::Logical);
    lib_bp = lib_bp.with_node(std::move(bridge_in));

    bp2::Blueprint::Node bridge_out = make_bridge_node(I, "out", false, Domain::Logical);
    lib_bp = lib_bp.with_node(std::move(bridge_out));

    bp2::Blueprint::Node leaf;
    leaf.semantic.id = I.intern("acc");
    leaf.semantic.type = I.intern("Accumulator");
    leaf.component().iface = bp2::Interface({
        make_port(I, "in", Domain::Logical, bp2::Direction::Input),
        make_port(I, "out", Domain::Logical, bp2::Direction::Output),
    });
    lib_bp = lib_bp.with_node(std::move(leaf));

    bp2::Blueprint::Wire lw1;
    lw1.id = I.intern("lw1");
    lw1.source = {I.intern("in"), I.intern("ext")};
    lw1.target = {I.intern("acc"), I.intern("in")};
    lw1.domain = Domain::Logical;
    lib_bp = lib_bp.with_wire(std::move(lw1));

    bp2::Blueprint::Wire lw2;
    lw2.id = I.intern("lw2");
    lw2.source = {I.intern("acc"), I.intern("out")};
    lw2.target = {I.intern("out"), I.intern("ext")};
    lw2.domain = Domain::Logical;
    lib_bp = lib_bp.with_wire(std::move(lw2));

    library.add(I.intern("MyComposite"), lib_bp);

    // Root: val → [lag].in, [lag].out → sink
    bp2::Blueprint root;

    bp2::Blueprint::Node val;
    val.semantic.id = I.intern("val");
    val.semantic.type = I.intern("Value");
    val.component().iface = bp2::Interface({
        make_port(I, "o", Domain::Logical, bp2::Direction::Output),
    });
    root = root.with_node(std::move(val));

    bp2::Blueprint::Node lag;
    lag.semantic.id = I.intern("lag");
    lag.semantic.type = I.intern("MyComposite");
    lag.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_reference(
        I.intern("MyComposite"))
    };
    root = root.with_node(std::move(lag));

    bp2::Blueprint::Node sink;
    sink.semantic.id = I.intern("sink");
    sink.semantic.type = I.intern("Value");
    sink.component().iface = bp2::Interface({
        make_port(I, "i", Domain::Logical, bp2::Direction::Input),
    });
    root = root.with_node(std::move(sink));

    bp2::Blueprint::Wire rw1;
    rw1.id = I.intern("rw1");
    rw1.source = {I.intern("val"), I.intern("o")};
    rw1.target = {I.intern("lag"), I.intern("in")};
    rw1.domain = Domain::Logical;
    root = root.with_wire(std::move(rw1));

    bp2::Blueprint::Wire rw2;
    rw2.id = I.intern("rw2");
    rw2.source = {I.intern("lag"), I.intern("out")};
    rw2.target = {I.intern("sink"), I.intern("i")};
    rw2.domain = Domain::Logical;
    root = root.with_wire(std::move(rw2));

    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(root, arena);

    assert_no_phantom_paths(netlist, arena, I);

    // Devices: val, lag:in, lag:acc, lag:out, sink
    EXPECT_EQ(netlist.components.size(), 5u);

    bp2::SignalIndex val_o = UINT32_MAX, lag_in_ext = UINT32_MAX;
    bp2::SignalIndex acc_in = UINT32_MAX, acc_out = UINT32_MAX;
    bp2::SignalIndex lag_out_ext = UINT32_MAX, sink_i = UINT32_MAX;
    for (auto const& comp : netlist.components) {
        for (auto const& [pname, sig] : comp.port_signals) {
            std::string cpath = arena.to_string(comp.path);
            std::string pstr(I.resolve(pname));
            if (cpath == "/val" && pstr == "o") val_o = sig;
            if (cpath == "/lag/in" && pstr == "ext") lag_in_ext = sig;
            if (cpath == "/lag/acc" && pstr == "in") acc_in = sig;
            if (cpath == "/lag/acc" && pstr == "out") acc_out = sig;
            if (cpath == "/lag/out" && pstr == "ext") lag_out_ext = sig;
            if (cpath == "/sink" && pstr == "i") sink_i = sig;
        }
    }
    EXPECT_EQ(val_o, lag_in_ext);
    EXPECT_EQ(lag_in_ext, acc_in);
    EXPECT_EQ(acc_out, lag_out_ext);
    EXPECT_EQ(lag_out_ext, sink_i);
}

// ==================================================================
// #112 Regression: unwired instance — no crash, no phantom paths
// ==================================================================

TEST(Flattener, Regression112_UnwiredInstance_NoCrash) {
    ui::StringInterner I;
    bp2::PathArena arena(I);
    bp2::BlueprintLibrary library;

    // Inner blueprint with a bridge
    bp2::Blueprint inner;
    inner = inner.with_interface(bp2::Interface({
        make_port(I, "vin", Domain::Electrical, bp2::Direction::Input),
    }));
    bp2::Blueprint::Node bridge = make_bridge_node(I, "vin", true, Domain::Electrical);
    inner = inner.with_node(std::move(bridge));

    // Root with instance but no wires connecting to it
    bp2::Blueprint root;
    bp2::Blueprint::Node inst;
    inst.semantic.id = I.intern("inst");
    inst.semantic.type = I.intern("Embedded");
    inst.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(inner.with_id(I.intern("Embedded"))))
    };
    root = root.with_node(std::move(inst));

    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(root, arena);

    // Bridge node still emitted even without outer wires
    EXPECT_EQ(netlist.components.size(), 1u);
    assert_no_phantom_paths(netlist, arena, I);
}
