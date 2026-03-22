#include <gtest/gtest.h>
#include "ui/core/interned_id.h"
#include "blueprint_v2/bridge/blueprint_bridge.h"
#include "editor/data/flat_blueprint.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/path/path.h"

TEST(BlueprintBridge, FromFlatEmpty) {
    ui::StringInterner interner;
    FlatBlueprint flat;

    bp2::Blueprint bp = bp2::BlueprintBridge::from_flat(flat, interner);
    EXPECT_TRUE(bp.nodes().empty());
    EXPECT_TRUE(bp.wires().empty());
}

TEST(BlueprintBridge, FromFlatWithNodes) {
    ui::StringInterner interner;
    FlatBlueprint flat;
    flat.meta.name = "test_bp";

    FlatNode node;
    node.type = "Battery";
    node.pos = {100.0f, 200.0f};
    node.params["v_nominal"] = "28.0";
    flat.nodes["bat1"] = node;

    bp2::Blueprint bp = bp2::BlueprintBridge::from_flat(flat, interner);
    EXPECT_EQ(interner.resolve(bp.id()), "test_bp");
    ASSERT_EQ(bp.nodes().size(), 1u);

    auto const& n = bp.nodes()[0];
    EXPECT_EQ(interner.resolve(n.id), "bat1");
    EXPECT_EQ(interner.resolve(n.type), "Battery");
    EXPECT_FLOAT_EQ(n.x, 100.0f);
    EXPECT_FLOAT_EQ(n.y, 200.0f);
}

TEST(BlueprintBridge, FromFlatWithWires) {
    ui::StringInterner interner;
    FlatBlueprint flat;

    FlatNode n1, n2;
    n1.type = "Battery";
    n2.type = "Resistor";
    flat.nodes["b1"] = n1;
    flat.nodes["r1"] = n2;

    FlatWire wire;
    wire.id = "w1";
    wire.from.node = "b1";
    wire.from.port = "v_out";
    wire.to.node = "r1";
    wire.to.port = "in";
    flat.wires.push_back(wire);

    bp2::Blueprint bp = bp2::BlueprintBridge::from_flat(flat, interner);
    ASSERT_EQ(bp.wires().size(), 1u);
    EXPECT_EQ(interner.resolve(bp.wires()[0].id), "w1");
}

TEST(BlueprintBridge, FromFlatWithNested) {
    ui::StringInterner interner;
    FlatBlueprint flat;

    FlatSubBlueprint sub;
    sub.type_name = "power_system";
    sub.pos = {50.0f, 75.0f};
    sub.collapsed = true;
    flat.sub_blueprints["sub1"] = sub;

    bp2::Blueprint bp = bp2::BlueprintBridge::from_flat(flat, interner);
    ASSERT_EQ(bp.nested().size(), 1u);

    auto const& n = bp.nested()[0];
    EXPECT_EQ(interner.resolve(n.id), "sub1");
    EXPECT_EQ(interner.resolve(n.blueprint_id), "power_system");
    EXPECT_FALSE(n.embedded);
    EXPECT_FLOAT_EQ(n.x, 50.0f);
    EXPECT_FLOAT_EQ(n.y, 75.0f);
}

TEST(BlueprintBridge, FromFlatWithInterface) {
    ui::StringInterner interner;
    FlatBlueprint flat;

    FlatPort port_in, port_out;
    port_in.direction = "In";
    port_out.direction = "Out";
    flat.exposes["v_in"] = port_in;
    flat.exposes["v_out"] = port_out;

    bp2::Blueprint bp = bp2::BlueprintBridge::from_flat(flat, interner);
    EXPECT_EQ(bp.iface().size(), 2u);
}

TEST(BlueprintBridge, ToFlatRoundTrip) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("test"));

    bp2::Blueprint::Node node;
    node.id = interner.intern("n1");
    node.type = interner.intern("Battery");
    node.x = 100.0f;
    node.y = 200.0f;
    node.params[interner.intern("v_nominal")] = 28.0f;
    bp = bp.with_node(std::move(node));

    bp2::Blueprint::Wire wire;
    wire.id = interner.intern("w1");
    wire.source = arena.make_port(
        arena.make_node(arena.root(), interner.intern("n1")),
        interner.intern("v_out"));
    wire.target = arena.make_port(
        arena.make_node(arena.root(), interner.intern("n2")),
        interner.intern("in"));
    bp = bp.with_wire(std::move(wire));

    FlatBlueprint flat = bp2::BlueprintBridge::to_flat(bp, interner, arena);

    EXPECT_EQ(flat.meta.name, "test");
    ASSERT_EQ(flat.nodes.size(), 1u);
    EXPECT_EQ(flat.nodes.begin()->first, "n1");
    EXPECT_EQ(flat.nodes.begin()->second.type, "Battery");

    ASSERT_EQ(flat.wires.size(), 1u);
    EXPECT_EQ(flat.wires[0].from.node, "n1");
    EXPECT_EQ(flat.wires[0].from.port, "v_out");
    EXPECT_EQ(flat.wires[0].to.node, "n2");
    EXPECT_EQ(flat.wires[0].to.port, "in");
}

TEST(BlueprintBridge, ToFlatWithInterface) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Blueprint bp;

    bp2::Interface iface({
        {interner.intern("v_in"), Domain::Electrical, bp2::Direction::Input},
        {interner.intern("v_out"), Domain::Electrical, bp2::Direction::Output},
    });
    bp = bp.with_interface(iface);

    FlatBlueprint flat = bp2::BlueprintBridge::to_flat(bp, interner, arena);
    ASSERT_EQ(flat.exposes.size(), 2u);
    EXPECT_EQ(flat.exposes.at("v_in").direction, "In");
    EXPECT_EQ(flat.exposes.at("v_out").direction, "Out");
}

TEST(BlueprintBridge, ToFlatWithNested) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Blueprint bp;

    bp2::Blueprint::Nested nested;
    nested.id = interner.intern("sub1");
    nested.blueprint_id = interner.intern("power_system");
    nested.embedded = false;
    nested.x = 100.0f;
    nested.y = 150.0f;
    bp = bp.with_nested(std::move(nested));

    FlatBlueprint flat = bp2::BlueprintBridge::to_flat(bp, interner, arena);
    ASSERT_EQ(flat.sub_blueprints.size(), 1u);
    auto const& sub = flat.sub_blueprints.at("sub1");
    EXPECT_EQ(sub.type_name, "power_system");
    EXPECT_TRUE(sub.collapsed);
    EXPECT_FLOAT_EQ(sub.pos[0], 100.0f);
    EXPECT_FLOAT_EQ(sub.pos[1], 150.0f);
}
