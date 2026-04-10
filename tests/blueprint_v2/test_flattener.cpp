#include <gtest/gtest.h>
#include <set>
#include "ui/core/interned_id.h"
#include "blueprint_v2/flattener/flat_netlist.h"
#include "blueprint_v2/flattener/flattener.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/library/blueprint_library.h"
#include "blueprint_v2/path/path.h"

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
    node.semantic.iface = library.find(interner.intern("Battery"))->iface();
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
    bat.semantic.iface = library.find(interner.intern("Battery"))->iface();
    bp = bp.with_node(std::move(bat));

    bp2::Blueprint::Node res;
    res.semantic.id = interner.intern("r1");
    res.semantic.type = interner.intern("Resistor");
    res.semantic.iface = library.find(interner.intern("Resistor"))->iface();
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
    bat.semantic.iface = library.find(interner.intern("Battery"))->iface();
    bp = bp.with_node(std::move(bat));

    bp2::Blueprint::Node res;
    res.semantic.id = interner.intern("r1");
    res.semantic.type = interner.intern("Resistor");
    res.semantic.iface = library.find(interner.intern("Resistor"))->iface();
    bp = bp.with_node(std::move(res));

    bp2::Blueprint::Node led;
    led.semantic.id = interner.intern("led1");
    led.semantic.type = interner.intern("LED");
    led.semantic.iface = library.find(interner.intern("LED"))->iface();
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
    bat.semantic.iface = library.find(interner.intern("Battery"))->iface();
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

