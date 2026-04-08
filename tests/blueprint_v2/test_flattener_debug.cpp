#include <gtest/gtest.h>
#include <set>
#include <iostream>
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

TEST(Flattener, NestedDebug) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    bp2::Blueprint inner;
    inner = inner.with_id(interner.intern("sub_type"));
    inner = inner.with_interface(bp2::Interface({
        {interner.intern("in"), Domain::Electrical, bp2::Direction::Input},
        {interner.intern("out"), Domain::Electrical, bp2::Direction::Output},
    }));

    bp2::Blueprint::Node r1;
    r1.semantic.id = interner.intern("r1");
    r1.semantic.type = interner.intern("Resistor");
    r1.semantic.iface = bp2::Interface({
        {interner.intern("in"), Domain::Electrical, bp2::Direction::Input},
        {interner.intern("out"), Domain::Electrical, bp2::Direction::Output},
    });
    inner = inner.with_node(std::move(r1));

    bp2::Blueprint::Wire iw1;
    iw1.id = interner.intern("iw1");
    iw1.source = arena.make_port(arena.root(), interner.intern("in"));
    iw1.target = arena.make_port(
        arena.make_node(arena.root(), interner.intern("r1")),
        interner.intern("in"));
    iw1.domain = Domain::Electrical;
    inner = inner.with_wire(std::move(iw1));

    bp2::Blueprint::Wire iw2;
    iw2.id = interner.intern("iw2");
    iw2.source = arena.make_port(
        arena.make_node(arena.root(), interner.intern("r1")),
        interner.intern("out"));
    iw2.target = arena.make_port(arena.root(), interner.intern("out"));
    iw2.domain = Domain::Electrical;
    inner = inner.with_wire(std::move(iw2));

    auto library = make_test_library(interner);
    library.add(interner.intern("sub_type"), inner);

    bp2::Blueprint root;

    bp2::Blueprint::Node bat;
    bat.semantic.id = interner.intern("bat1");
    bat.semantic.type = interner.intern("Battery");
    bat.semantic.iface = library.find(interner.intern("Battery"))->iface();
    root = root.with_node(std::move(bat));

    auto nested = bp2::Blueprint::Nested::make_embedded(
        interner.intern("sub1"),
        ui::InternedId{},
        std::make_unique<bp2::Blueprint>(inner));
    root = root.with_nested(std::move(nested));

    bp2::Blueprint::Node lnode;
    lnode.semantic.id = interner.intern("led1");
    lnode.semantic.type = interner.intern("LED");
    lnode.semantic.iface = library.find(interner.intern("LED"))->iface();
    root = root.with_node(std::move(lnode));

    bp2::Blueprint::Wire w1;
    w1.id = interner.intern("w1");
    w1.source = arena.make_port(
        arena.make_node(arena.root(), interner.intern("bat1")),
        interner.intern("v_out"));
    w1.target = arena.make_port(
        arena.make_nested(arena.root(), interner.intern("sub1")),
        interner.intern("in"));
    w1.domain = Domain::Electrical;
    root = root.with_wire(std::move(w1));

    bp2::Blueprint::Wire w2;
    w2.id = interner.intern("w2");
    w2.source = arena.make_port(
        arena.make_nested(arena.root(), interner.intern("sub1")),
        interner.intern("out"));
    w2.target = arena.make_port(
        arena.make_node(arena.root(), interner.intern("led1")),
        interner.intern("v_in"));
    w2.domain = Domain::Electrical;
    root = root.with_wire(std::move(w2));

    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(root, arena);

    std::cerr << "=== Components ===" << std::endl;
    for (auto const& comp : netlist.components) {
        std::cerr << "  " << interner.resolve(comp.type) 
                  << " path=" << arena.to_string(comp.path) << std::endl;
        for (auto const& [port_name, sig] : comp.port_signals) {
            std::cerr << "    port=" << interner.resolve(port_name) 
                      << " signal=" << sig << std::endl;
        }
    }
    std::cerr << "=== Signals ===" << std::endl;
    for (auto const& sig : netlist.signals) {
        std::cerr << "  sig[" << sig.index << "] domain=" << (int)sig.domain 
                  << " ports=" << sig.connected_ports.size() << std::endl;
    }

    EXPECT_EQ(netlist.components.size(), 3u);
}
