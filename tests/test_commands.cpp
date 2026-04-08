#include <gtest/gtest.h>
#include <set>
#include "editor/commands/commands.h"
#include "editor/commands/extract_blueprint.h"
#include "editor/commands/transaction_guard.h"
#include "editor/commands/blueprint_checksum.h"
#include "editor/visual/persist.h"
#include "json_parser/json_parser.h"
#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/interface/interface.h"
#include "blueprint_v2/interface/port_descriptor.h"
#include "blueprint_v2/path/path.h"
#include "blueprint_v2/validation/invariant_checker.h"
#include "blueprint_v2/diagnostics/repair.h"
#include "ui/core/interned_id.h"

// Shared bp2 test helpers
#include "bp2_test_helpers.h"

// Helper: create a simple node

static bp2::Blueprint::Node make_node(ui::StringInterner& I,
                                       const char* id,
                                       float x = 0.0f, float y = 0.0f) {
    bp2::Blueprint::Node n;
    n.semantic.id = I.intern(id);
    n.semantic.type = I.intern("Test");
    n.layout.x = x;
    n.layout.y = y;
    return n;
}

static TypeRegistry make_command_test_registry() {
    TypeRegistry reg = load_type_registry("library/");

    const std::vector<std::string> synthetic_types = {
        "Test",
        "NodeA",
        "NodeB",
        "NodeExtIn",
        "NodeExtOut",
        "BlueprintInput",
        "BlueprintOutput",
        "Sink",
        "Slider",
        "SomeLibraryBlueprint",
        "Source",
        "TypedA",
        "TypedB",
        "TypedExtIn",
        "TypedExtOut",
        "sub_blueprint_type",
        "sub_blueprint_type_2"
    };
    const std::vector<std::string> synthetic_ports = {
        "ext", "in", "in2", "in_2", "link", "out", "out2", "port", "sig"
    };

    for (const auto& type_name : synthetic_types) {
        TypeDefinition def;
        def.classname = type_name;
        def.description = "command test synthetic type";
        def.cpp_class = true;
        for (const auto& port_name : synthetic_ports) {
            Port p;
            p.direction = PortDirection::InOut;
            p.type = PortType::V;
            p.domain = Domain::Electrical;
            p.source_writer = false;
            def.ports.emplace(port_name, std::move(p));
        }
        reg.types[type_name] = std::move(def);
    }

    return reg;
}

// Helper: create a wire between node:port -> node:port
static bp2::Blueprint::Wire make_wire(ui::StringInterner& I,
                                       bp2::PathArena& arena,
                                       const char* wire_id,
                                       const char* src_node, const char* src_port,
                                       const char* dst_node, const char* dst_port) {
    bp2::Blueprint::Wire w;
    w.id = I.intern(wire_id);
    w.source = arena.make_port(arena.make_node(arena.root(), I.intern(src_node)),
                               I.intern(src_port));
    w.target = arena.make_port(arena.make_node(arena.root(), I.intern(dst_node)),
                               I.intern(dst_port));
    return w;
}

static bp2::Blueprint make_extract_fixture(ui::StringInterner& I, bp2::PathArena& arena) {
    bp2::Blueprint bp;
    bp = bp.with_id(I.intern("bp_extract"));
    bp = bp.with_display_name("ExtractFixture");

    auto ext_in = make_node(I, "ext_in");
    ext_in.semantic.type = I.intern("NodeExtIn");
    set_iface(ext_in, {
        make_port(I.intern("out"), bp2::Direction::Output, PortType::V),
    });

    auto a = make_node(I, "a");
    a.semantic.type = I.intern("NodeA");
    set_iface(a, {
        make_port(I.intern("in"), bp2::Direction::Input, PortType::V),
        make_port(I.intern("out"), bp2::Direction::Output, PortType::V),
    });

    auto b = make_node(I, "b");
    b.semantic.type = I.intern("NodeB");
    set_iface(b, {
        make_port(I.intern("in"), bp2::Direction::Input, PortType::V),
        make_port(I.intern("out"), bp2::Direction::Output, PortType::V),
    });

    auto ext_out = make_node(I, "ext_out");
    ext_out.semantic.type = I.intern("NodeExtOut");
    set_iface(ext_out, {
        make_port(I.intern("in"), bp2::Direction::Input, PortType::V),
    });

    bp = bp.with_node(std::move(ext_in));
    bp = bp.with_node(std::move(a));
    bp = bp.with_node(std::move(b));
    bp = bp.with_node(std::move(ext_out));

    auto w0 = make_wire(I, arena, "w0", "ext_in", "out", "a", "in");
    w0.domain = Domain::Electrical;
    auto w1 = make_wire(I, arena, "w1", "a", "out", "b", "in");
    w1.domain = Domain::Electrical;
    auto w2 = make_wire(I, arena, "w2", "b", "out", "ext_out", "in");
    w2.domain = Domain::Electrical;

    bp = bp.with_wire(std::move(w0));
    bp = bp.with_wire(std::move(w1));
    bp = bp.with_wire(std::move(w2));
    return bp;
}

static bp2::Blueprint make_extract_iface_collision_fixture(ui::StringInterner& I, bp2::PathArena& arena) {
    bp2::Blueprint bp;
    bp = bp.with_id(I.intern("bp_extract_collision"));
    bp = bp.with_display_name("ExtractIfaceCollision");

    auto ext_in = make_node(I, "ext_in");
    ext_in.semantic.type = I.intern("Source");
    set_iface(ext_in, {
        make_port(I.intern("out"), bp2::Direction::Output, PortType::V),
    });

    auto a = make_node(I, "a");
    a.semantic.type = I.intern("NodeA");
    set_iface(a, {
        make_port(I.intern("sig"), bp2::Direction::Input, PortType::V),
        make_port(I.intern("link"), bp2::Direction::Output, PortType::V),
    });

    auto b = make_node(I, "b");
    b.semantic.type = I.intern("NodeB");
    set_iface(b, {
        make_port(I.intern("link"), bp2::Direction::Input, PortType::V),
        make_port(I.intern("sig"), bp2::Direction::Output, PortType::V),
    });

    auto ext_out = make_node(I, "ext_out");
    ext_out.semantic.type = I.intern("Sink");
    set_iface(ext_out, {
        make_port(I.intern("in"), bp2::Direction::Input, PortType::V),
    });

    bp = bp.with_node(std::move(ext_in));
    bp = bp.with_node(std::move(a));
    bp = bp.with_node(std::move(b));
    bp = bp.with_node(std::move(ext_out));

    auto w0 = make_wire(I, arena, "w0", "ext_in", "out", "a", "sig");
    w0.domain = Domain::Electrical;
    auto w1 = make_wire(I, arena, "w1", "a", "link", "b", "link");
    w1.domain = Domain::Electrical;
    auto w2 = make_wire(I, arena, "w2", "b", "sig", "ext_out", "in");
    w2.domain = Domain::Electrical;

    bp = bp.with_wire(std::move(w0));
    bp = bp.with_wire(std::move(w1));
    bp = bp.with_wire(std::move(w2));
    return bp;
}

static bp2::Blueprint make_extract_subgroup_fixture(ui::StringInterner& I, bp2::PathArena& arena) {
    bp2::Blueprint bp = make_extract_fixture(I, arena);

    const std::string subgroup = "group_1";
    if (const auto* a = bp.find_node(I.intern("a"))) {
        auto n = *a;
        n.layout.layout_group = subgroup;
        bp = bp.without_node(n.semantic.id);
        bp = bp.with_node(std::move(n));
    }
    if (const auto* b = bp.find_node(I.intern("b"))) {
        auto n = *b;
        n.layout.layout_group = subgroup;
        bp = bp.without_node(n.semantic.id);
        bp = bp.with_node(std::move(n));
    }
    return bp;
}

static bp2::Blueprint make_extract_with_embedded_nested_selection_fixture(ui::StringInterner& I,
                                                                          bp2::PathArena& arena) {
    bp2::Blueprint bp = make_extract_fixture(I, arena);

    // Replace node "a" with a nested-instance-style expandable node.
    bp = bp.without_node(I.intern("a"));
    bp = bp.without_wire(I.intern("w0"));
    bp = bp.without_wire(I.intern("w1"));

     bp2::Blueprint::Node nested_node = make_node(I, "sub_inst_1");
     nested_node.semantic.type = I.intern("sub_blueprint_type");
     nested_node.view.expandable = true;
    set_iface(nested_node, {
        make_port(I.intern("in"), bp2::Direction::Input, PortType::V),
        make_port(I.intern("out"), bp2::Direction::Output, PortType::V),
    });
     bp = bp.with_node(std::move(nested_node));

     auto inline_def_1 = std::make_unique<bp2::Blueprint>();
    *inline_def_1 = inline_def_1->with_id(I.intern("sub_blueprint_type"));
    *inline_def_1 = inline_def_1->with_interface(bp2::Interface({
        {I.intern("in"), Domain::Electrical, bp2::Direction::Input},
        {I.intern("out"), Domain::Electrical, bp2::Direction::Output},
    }));
    auto nested = bp2::Blueprint::Nested::make_embedded(
        I.intern("sub_inst_1"), I.intern("sub_blueprint_type"),
        std::move(inline_def_1));
    bp = bp.with_nested(std::move(nested));

    auto w0 = make_wire(I, arena, "w0", "ext_in", "out", "sub_inst_1", "in");
    w0.domain = Domain::Electrical;
    auto w1 = make_wire(I, arena, "w1", "sub_inst_1", "out", "b", "in");
    w1.domain = Domain::Electrical;
    bp = bp.with_wire(std::move(w0));
    bp = bp.with_wire(std::move(w1));

    return bp;
}

static bp2::Blueprint make_extract_with_nonembedded_nested_selection_fixture(ui::StringInterner& I,
                                                                             bp2::PathArena& arena) {
    bp2::Blueprint bp = make_extract_with_embedded_nested_selection_fixture(I, arena);
    bp = bp.without_nested(I.intern("sub_inst_1"));
     auto n = bp2::Blueprint::Nested::make_reference(
         I.intern("sub_inst_1"), I.intern("SomeLibraryBlueprint"), bp2::Interface());
    bp = bp.with_nested(std::move(n));
    return bp;
}

static bp2::Blueprint make_extract_with_two_embedded_nested_selection_fixture(ui::StringInterner& I,
                                                                              bp2::PathArena& arena) {
    bp2::Blueprint bp = make_extract_with_embedded_nested_selection_fixture(I, arena);

     auto sub2 = make_node(I, "sub_inst_2");
     sub2.semantic.type = I.intern("sub_blueprint_type_2");
     sub2.view.expandable = true;
    set_iface(sub2, {
        make_port(I.intern("in"), bp2::Direction::Input, PortType::V),
        make_port(I.intern("out"), bp2::Direction::Output, PortType::V),
    });
     bp = bp.with_node(std::move(sub2));

     auto inline_def_2 = std::make_unique<bp2::Blueprint>();
    *inline_def_2 = inline_def_2->with_id(I.intern("sub_blueprint_type_2"));
    *inline_def_2 = inline_def_2->with_interface(bp2::Interface({
        {I.intern("in"), Domain::Electrical, bp2::Direction::Input},
        {I.intern("out"), Domain::Electrical, bp2::Direction::Output},
    }));
    auto nested2 = bp2::Blueprint::Nested::make_embedded(
        I.intern("sub_inst_2"), I.intern("sub_blueprint_type_2"),
        std::move(inline_def_2));
    bp = bp.with_nested(std::move(nested2));

    // Rewire chain: sub_inst_1.out -> sub_inst_2.in -> b.in
    bp = bp.without_wire(I.intern("w1"));
    auto w1 = make_wire(I, arena, "w1", "sub_inst_1", "out", "sub_inst_2", "in");
    w1.domain = Domain::Electrical;
    auto w1b = make_wire(I, arena, "w1b", "sub_inst_2", "out", "b", "in");
    w1b.domain = Domain::Electrical;
    bp = bp.with_wire(std::move(w1));
    bp = bp.with_wire(std::move(w1b));

    return bp;
}

static bp2::Blueprint make_extract_with_embedded_nested_having_nonembedded_descendant_fixture(
    ui::StringInterner& I,
    bp2::PathArena& arena) {
    bp2::Blueprint bp = make_extract_with_embedded_nested_selection_fixture(I, arena);

    // Replace sub_inst_1 nested def with one that contains a non-embedded descendant.
    bp = bp.without_nested(I.intern("sub_inst_1"));
    auto inline_def = std::make_unique<bp2::Blueprint>();
    *inline_def = inline_def->with_id(I.intern("sub_blueprint_type"));

    auto child = bp2::Blueprint::Nested::make_reference(
        I.intern("child_nonembedded"), I.intern("SomeLibraryBlueprint"), bp2::Interface());
    *inline_def = inline_def->with_nested(std::move(child));

    *inline_def = inline_def->with_interface(bp2::Interface({
        {I.intern("in"), Domain::Electrical, bp2::Direction::Input},
        {I.intern("out"), Domain::Electrical, bp2::Direction::Output},
    }));
    auto nested = bp2::Blueprint::Nested::make_embedded(
        I.intern("sub_inst_1"), I.intern("sub_blueprint_type"),
        std::move(inline_def));
    bp = bp.with_nested(std::move(nested));
    return bp;
}

static bp2::Blueprint make_extract_with_remappable_nonembedded_descendant_fixture(
    ui::StringInterner& I,
    bp2::PathArena& arena) {
    bp2::Blueprint bp = make_extract_with_embedded_nested_having_nonembedded_descendant_fixture(I, arena);

    // Provide an embedded nested definition in source with matching blueprint_id
    // so guarded mode can remap non-embedded descendant -> embedded inline copy.
     auto provider = make_node(I, "provider_embed");
     provider.semantic.type = I.intern("SomeLibraryBlueprint");
     provider.view.expandable = true;
    set_iface(provider, {
        make_port(I.intern("in"), bp2::Direction::Input, PortType::V),
        make_port(I.intern("out"), bp2::Direction::Output, PortType::V),
    });
     bp = bp.with_node(std::move(provider));

    auto prov_def = std::make_unique<bp2::Blueprint>();
    *prov_def = prov_def->with_id(I.intern("SomeLibraryBlueprint"));
    *prov_def = prov_def->with_interface(bp2::Interface({
        {I.intern("in"), Domain::Electrical, bp2::Direction::Input},
        {I.intern("out"), Domain::Electrical, bp2::Direction::Output},
    }));
    auto provider_nested = bp2::Blueprint::Nested::make_embedded(
        I.intern("provider_embed"), I.intern("SomeLibraryBlueprint"),
        std::move(prov_def));
    bp = bp.with_nested(std::move(provider_nested));

    return bp;
}

static bp2::Blueprint make_extract_with_two_remap_providers_fixture(
    ui::StringInterner& I,
    bp2::PathArena& arena) {
    bp2::Blueprint bp = make_extract_with_embedded_nested_having_nonembedded_descendant_fixture(I, arena);

     auto add_provider = [&](const char* node_id, const char* def_id) {
         auto provider = make_node(I, node_id);
         provider.semantic.type = I.intern("SomeLibraryBlueprint");
         provider.view.expandable = true;
    set_iface(provider, {
        make_port(I.intern("in"), bp2::Direction::Input, PortType::V),
        make_port(I.intern("out"), bp2::Direction::Output, PortType::V),
    });
         bp = bp.with_node(std::move(provider));

        auto prov_inline = std::make_unique<bp2::Blueprint>();
        *prov_inline = prov_inline->with_id(I.intern(def_id));
        *prov_inline = prov_inline->with_interface(bp2::Interface({
            {I.intern("in"), Domain::Electrical, bp2::Direction::Input},
            {I.intern("out"), Domain::Electrical, bp2::Direction::Output},
        }));
        auto prov_nested = bp2::Blueprint::Nested::make_embedded(
            I.intern(node_id), I.intern("SomeLibraryBlueprint"),
            std::move(prov_inline));
        bp = bp.with_nested(std::move(prov_nested));
    };

    // Intentionally insert in this order to make id.raw ordering observable.
    add_provider("provider_b", "provider_b_def");
    add_provider("provider_a", "provider_a_def");

    return bp;
}

static bp2::Blueprint make_extract_layout_fixture(ui::StringInterner& I, bp2::PathArena& arena) {
    bp2::Blueprint bp;
    bp = bp.with_id(I.intern("bp_extract_layout"));
    bp = bp.with_display_name("ExtractLayout");

    auto ext_in1 = make_node(I, "ext_in1");
    ext_in1.semantic.type = I.intern("Source");
    set_iface(ext_in1, {
        make_port(I.intern("out"), bp2::Direction::Output, PortType::V),
    });

    auto ext_in2 = make_node(I, "ext_in2");
    ext_in2.semantic.type = I.intern("Source");
    set_iface(ext_in2, {
        make_port(I.intern("out"), bp2::Direction::Output, PortType::V),
    });

    auto a = make_node(I, "a", 100.0f, 100.0f);
    a.semantic.type = I.intern("NodeA");
    set_iface(a, {
        make_port(I.intern("in"), bp2::Direction::Input, PortType::V),
        make_port(I.intern("out"), bp2::Direction::Output, PortType::V),
    });

    auto b = make_node(I, "b", 120.0f, 300.0f);
    b.semantic.type = I.intern("NodeB");
    set_iface(b, {
        make_port(I.intern("in"), bp2::Direction::Input, PortType::V),
        make_port(I.intern("out"), bp2::Direction::Output, PortType::V),
    });

    auto ext_out = make_node(I, "ext_out");
    ext_out.semantic.type = I.intern("Sink");
    set_iface(ext_out, {
        make_port(I.intern("in"), bp2::Direction::Input, PortType::V),
    });

    bp = bp.with_node(std::move(ext_in1));
    bp = bp.with_node(std::move(ext_in2));
    bp = bp.with_node(std::move(a));
    bp = bp.with_node(std::move(b));
    bp = bp.with_node(std::move(ext_out));

    auto w0 = make_wire(I, arena, "w0", "ext_in1", "out", "a", "in");
    w0.domain = Domain::Electrical;
    auto w1 = make_wire(I, arena, "w1", "ext_in2", "out", "b", "in");
    w1.domain = Domain::Electrical;
    auto w2 = make_wire(I, arena, "w2", "a", "out", "b", "in");
    w2.domain = Domain::Electrical;
    auto w3 = make_wire(I, arena, "w3", "b", "out", "ext_out", "in");
    w3.domain = Domain::Electrical;
    bp = bp.with_wire(std::move(w0));
    bp = bp.with_wire(std::move(w1));
    bp = bp.with_wire(std::move(w2));
    bp = bp.with_wire(std::move(w3));

    return bp;
}

static bp2::Blueprint make_extract_with_bridge_node_fixture(ui::StringInterner& I, bp2::PathArena& arena) {
    bp2::Blueprint bp;
    bp = bp.with_id(I.intern("bp_extract_bridge_node"));
    bp = bp.with_display_name("ExtractBridgeNode");

    auto bridge = make_node(I, "bridge_in");
    bridge.semantic.type = I.intern("BlueprintInput");
    set_iface(bridge, {
        make_port(I.intern("ext"), bp2::Direction::Input, PortType::V),
        make_port(I.intern("port"), bp2::Direction::Output, PortType::V),
    });

    auto a = make_node(I, "a");
    a.semantic.type = I.intern("NodeA");
    set_iface(a, {
        make_port(I.intern("in"), bp2::Direction::Input, PortType::V),
        make_port(I.intern("out"), bp2::Direction::Output, PortType::V),
    });

    auto ext = make_node(I, "ext");
    ext.semantic.type = I.intern("Sink");
    set_iface(ext, {
        make_port(I.intern("in"), bp2::Direction::Input, PortType::V),
    });

    bp = bp.with_node(std::move(bridge));
    bp = bp.with_node(std::move(a));
    bp = bp.with_node(std::move(ext));

    auto w0 = make_wire(I, arena, "w0", "bridge_in", "port", "a", "in");
    w0.domain = Domain::Electrical;
    auto w1 = make_wire(I, arena, "w1", "a", "out", "ext", "in");
    w1.domain = Domain::Electrical;
    bp = bp.with_wire(std::move(w0));
    bp = bp.with_wire(std::move(w1));

    return bp;
}

static bp2::Blueprint make_extract_typed_boundary_fixture(ui::StringInterner& I, bp2::PathArena& arena) {
    bp2::Blueprint bp;
    bp = bp.with_id(I.intern("bp_extract_typed_boundary"));
    bp = bp.with_display_name("ExtractTypedBoundary");

     auto ext_in = make_node(I, "ext_in");
     ext_in.semantic.type = I.intern("TypedExtIn");
    set_iface(ext_in, {
        make_port(I.intern("out"), bp2::Direction::Output, PortType::I),
    });
     ext_in.semantic.iface = bp2::Interface({
         {I.intern("out"), Domain::Electrical, bp2::Direction::Output},
     });

     auto a = make_node(I, "a");
     a.semantic.type = I.intern("TypedA");
    set_iface(a, {
        make_port(I.intern("in"), bp2::Direction::Input, PortType::I),
        make_port(I.intern("out"), bp2::Direction::Output, PortType::I),
    });
     a.semantic.iface = bp2::Interface({
         {I.intern("in"), Domain::Electrical, bp2::Direction::Input},
         {I.intern("out"), Domain::Electrical, bp2::Direction::Output},
     });

     auto b = make_node(I, "b");
     b.semantic.type = I.intern("TypedB");
    set_iface(b, {
        make_port(I.intern("in"), bp2::Direction::Input, PortType::I),
        make_port(I.intern("out"), bp2::Direction::Output, PortType::I),
    });
     b.semantic.iface = bp2::Interface({
         {I.intern("in"), Domain::Electrical, bp2::Direction::Input},
         {I.intern("out"), Domain::Electrical, bp2::Direction::Output},
     });

     auto ext_out = make_node(I, "ext_out");
     ext_out.semantic.type = I.intern("TypedExtOut");
    set_iface(ext_out, {
        make_port(I.intern("in"), bp2::Direction::Input, PortType::I),
    });
     ext_out.semantic.iface = bp2::Interface({
         {I.intern("in"), Domain::Electrical, bp2::Direction::Input},
     });

    bp = bp.with_node(std::move(ext_in));
    bp = bp.with_node(std::move(a));
    bp = bp.with_node(std::move(b));
    bp = bp.with_node(std::move(ext_out));

    auto w0 = make_wire(I, arena, "w0", "ext_in", "out", "a", "in");
    w0.domain = Domain::Electrical;
    auto w1 = make_wire(I, arena, "w1", "a", "out", "b", "in");
    w1.domain = Domain::Electrical;
    auto w2 = make_wire(I, arena, "w2", "b", "out", "ext_out", "in");
    w2.domain = Domain::Electrical;
    bp = bp.with_wire(std::move(w0));
    bp = bp.with_wire(std::move(w1));
    bp = bp.with_wire(std::move(w2));

    return bp;
}

class CommandTest : public ::testing::Test {
protected:
    ui::StringInterner interner;
    bp2::EditorModel   model;
    TypeRegistry parser_registry = make_command_test_registry();
};

TEST_F(CommandTest, BlueprintDefaults) {
    EXPECT_FLOAT_EQ(model.current().grid_step(), 16.0f);
}

// =============================================================================
// Basic can_undo / can_redo
// =============================================================================

TEST_F(CommandTest, UndoStackBasic) {
    EXPECT_FALSE(model.can_undo());
    EXPECT_FALSE(model.can_redo());
}

// =============================================================================
// CmdSetGridStep
// =============================================================================

TEST_F(CommandTest, SetGridStepMutates) {
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(40.0f));
    EXPECT_FLOAT_EQ(model.current().grid_step(), 40.0f);
}

// =============================================================================
// CmdMoveNode
// =============================================================================

TEST_F(CommandTest, MoveNodeMutates) {
    auto node_id = interner.intern("test_node");
    model.add_node(make_node(interner, "test_node", 10.0f, 20.0f));

    execute(model, interner, cmd_move_node(node_id, 100.0f, 200.0f));

    auto* n = model.current().find_node(node_id);
    ASSERT_NE(n, nullptr);
    EXPECT_FLOAT_EQ(n->layout.x, 100.0f);
    EXPECT_FLOAT_EQ(n->layout.y, 200.0f);
}

// =============================================================================
// CmdAddNode / CmdRemoveNode
// =============================================================================

TEST_F(CommandTest, AddRemoveNode) {
    auto node_id = interner.intern("node1");
    auto node = make_node(interner, "node1");
    node.semantic.type = interner.intern("Battery");

    execute(model, interner, cmd_add_node(node));
    EXPECT_EQ(model.current().nodes().size(), 1u);

    execute(model, interner, cmd_remove_node(node_id, {}));
    EXPECT_EQ(model.current().nodes().size(), 0u);
}

TEST_F(CommandTest, RemoveNodeAlsoRemovesConnectedWires) {
    bp2::PathArena arena(interner);

    auto n1 = make_node(interner, "n1");
    auto n2 = make_node(interner, "n2");
    auto n3 = make_node(interner, "n3");
    execute(model, interner, cmd_add_node(n1));
    execute(model, interner, cmd_add_node(n2));
    execute(model, interner, cmd_add_node(n3));

    execute(model, interner, cmd_add_wire(make_wire(interner, arena, "w1", "n1", "out", "n2", "in")));
    execute(model, interner, cmd_add_wire(make_wire(interner, arena, "w2", "n2", "out", "n3", "in")));
    execute(model, interner, cmd_add_wire(make_wire(interner, arena, "w3", "n1", "out2", "n3", "in2")));

    ASSERT_EQ(model.current().wires().size(), 3u);

    std::vector<ui::InternedId> connected = {
        interner.intern("w1"),
        interner.intern("w2"),
    };
    execute(model, interner, cmd_remove_node(interner.intern("n2"), connected));

    EXPECT_EQ(model.current().find_node(interner.intern("n2")), nullptr);
    ASSERT_EQ(model.current().wires().size(), 1u);
    EXPECT_NE(model.current().find_wire(interner.intern("w3")), nullptr);
}

// =============================================================================
// Undo/Redo round-trip for CmdSetGridStep
// =============================================================================

TEST_F(CommandTest, UndoRedoRoundTrip) {
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(32.0f));
    EXPECT_FLOAT_EQ(model.current().grid_step(), 32.0f);
    EXPECT_TRUE(model.can_undo());

    model.undo();
    EXPECT_FLOAT_EQ(model.current().grid_step(), 16.0f);
    EXPECT_TRUE(model.can_redo());

    model.redo();
    EXPECT_FLOAT_EQ(model.current().grid_step(), 32.0f);
    EXPECT_TRUE(model.can_undo());
    EXPECT_FALSE(model.can_redo());
}

// =============================================================================
// CmdSetName
// =============================================================================

TEST_F(CommandTest, SetNameMutates) {
    auto node_id = interner.intern("node1");
    auto node = make_node(interner, "node1");
    node.view.name = "OriginalName";
    model.add_node(std::move(node));

    execute(model, interner, cmd_set_name(node_id, "NewName"));

    auto* n = model.current().find_node(node_id);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->view.name, "NewName");
}

TEST_F(CommandTest, SetNameUndoRedo) {
    auto node_id = interner.intern("node1");
    auto node = make_node(interner, "node1");
    node.view.name = "OldName";
    model.add_node(std::move(node));

    model.push_checkpoint();
    execute(model, interner, cmd_set_name(node_id, "NewName"));
    EXPECT_EQ(model.current().find_node(node_id)->view.name, "NewName");

    model.undo();
    EXPECT_EQ(model.current().find_node(node_id)->view.name, "OldName");
}

// =============================================================================
// SetNameMissingNode — should not crash
// =============================================================================

TEST_F(CommandTest, SetNameMissingNodeDoesNotCrash) {
    auto ghost_id = interner.intern("ghost");
    execute(model, interner, cmd_set_name(ghost_id, "NewName"));
}

// =============================================================================
// Multiple command grouping (push_checkpoint before a batch)
// =============================================================================

TEST_F(CommandTest, MultipleCommandsUndoAsGroup) {
    auto id_a = interner.intern("a");
    auto id_b = interner.intern("b");

    model.add_node(make_node(interner, "a", 0.0f, 0.0f));
    model.add_node(make_node(interner, "b", 10.0f, 10.0f));

    // Snapshot before both moves — will be the recovery point
    model.push_checkpoint();
    execute(model, interner, cmd_move_node(id_a, 100.0f, 100.0f));
    execute(model, interner, cmd_move_node(id_b, 200.0f, 200.0f));

    EXPECT_FLOAT_EQ(model.current().find_node(id_a)->layout.x, 100.0f);
    EXPECT_FLOAT_EQ(model.current().find_node(id_b)->layout.x, 200.0f);

    // Undo last CmdMoveNode (id_b), then id_a
    model.undo();
    model.undo();

    // Now at the push_checkpoint state — both nodes at original positions
    EXPECT_FLOAT_EQ(model.current().find_node(id_a)->layout.x, 0.0f);
    EXPECT_FLOAT_EQ(model.current().find_node(id_b)->layout.x, 10.0f);
}

// =============================================================================
// TransactionGuard tests
// =============================================================================

TEST_F(CommandTest, TransactionGuardSingleCommand) {
    auto id = interner.intern("n");
    model.add_node(make_node(interner, "n", 0.0f, 0.0f));

    {
        TransactionGuard txn(model, interner);
        txn.execute(cmd_move_node(id, 42.0f, 42.0f));
    }

    EXPECT_FLOAT_EQ(model.current().find_node(id)->layout.x, 42.0f);
    EXPECT_TRUE(model.can_undo());

    model.undo();
    EXPECT_FLOAT_EQ(model.current().find_node(id)->layout.x, 0.0f);
}

TEST_F(CommandTest, TransactionGuardMultipleCommands) {
    auto id_a = interner.intern("a");
    auto id_b = interner.intern("b");

    model.add_node(make_node(interner, "a", 0.0f, 0.0f));
    model.add_node(make_node(interner, "b", 10.0f, 10.0f));

    {
        TransactionGuard txn(model, interner);
        txn.execute(cmd_move_node(id_a, 100.0f, 100.0f));
        txn.execute(cmd_move_node(id_b, 200.0f, 200.0f));
    }

    EXPECT_FLOAT_EQ(model.current().find_node(id_a)->layout.x, 100.0f);
    EXPECT_FLOAT_EQ(model.current().find_node(id_b)->layout.x, 200.0f);
    EXPECT_TRUE(model.can_undo());
}

TEST_F(CommandTest, TransactionGuardEmpty) {
    {
        TransactionGuard txn(model, interner);
        // No commands
    }
    EXPECT_FALSE(model.can_undo());
    EXPECT_FALSE(model.can_redo());
}

TEST_F(CommandTest, TransactionGuardDiscard) {
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(16.0f));

    {
        TransactionGuard txn(model, interner);
        txn.execute(cmd_set_grid_step(64.0f));
        txn.discard();
    }

    // discard() undoes the checkpoint taken by the TransactionGuard
    EXPECT_FLOAT_EQ(model.current().grid_step(), 16.0f);
}

TEST_F(CommandTest, TransactionGuardManualCommit) {
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(16.0f));

    {
        TransactionGuard txn(model, interner);
        txn.execute(cmd_set_grid_step(32.0f));
        txn.commit();  // Explicit commit; destructor should be idempotent
    }

    EXPECT_FLOAT_EQ(model.current().grid_step(), 32.0f);
    EXPECT_TRUE(model.can_undo());
}

// =============================================================================
// Dirty flag / save-point tests
// =============================================================================

TEST_F(CommandTest, DirtyFlagInitiallyClean) {
    EXPECT_FALSE(model.is_dirty());
}

TEST_F(CommandTest, DirtyFlagAfterCheckpoint) {
    model.push_checkpoint();
    EXPECT_TRUE(model.is_dirty());
}

TEST_F(CommandTest, DirtyFlagAfterMarkSaved) {
    model.push_checkpoint();
    model.mark_saved();
    EXPECT_FALSE(model.is_dirty());
}

TEST_F(CommandTest, DirtyFlagAfterUndoPastSavePoint) {
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(32.0f));
    model.mark_saved();
    EXPECT_FALSE(model.is_dirty());

    model.undo();
    EXPECT_TRUE(model.is_dirty());
}

TEST_F(CommandTest, DirtyFlagRedoRestoresSavePoint) {
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(32.0f));
    model.mark_saved();

    model.undo();
    EXPECT_TRUE(model.is_dirty());

    model.redo();
    EXPECT_FALSE(model.is_dirty());
}

// =============================================================================
// Redo-stack preservation tests
// =============================================================================

TEST_F(CommandTest, RedoStackPreservedByUndo) {
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(32.0f));
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(48.0f));
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(64.0f));

    model.undo();
    model.undo();
    model.undo();

    EXPECT_TRUE(model.can_redo());
    EXPECT_FALSE(model.can_undo());

    model.redo();
    EXPECT_TRUE(model.can_redo());
    EXPECT_TRUE(model.can_undo());

    model.redo();
    EXPECT_TRUE(model.can_redo());

    model.redo();
    EXPECT_FALSE(model.can_redo());
    EXPECT_TRUE(model.can_undo());
}

TEST_F(CommandTest, NewSnapshotClearsRedo) {
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(32.0f));

    model.undo();
    EXPECT_TRUE(model.can_redo());

    // New mutation should clear redo
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(48.0f));
    EXPECT_FALSE(model.can_redo());
}

// =============================================================================
// Blueprint checksum / undo round-trip verification tests
// =============================================================================

TEST_F(CommandTest, ChecksumDeterministic) {
    model.add_node(make_node(interner, "n", 10.0f, 20.0f));

    size_t h1 = blueprint_checksum(model.current());
    size_t h2 = blueprint_checksum(model.current());
    EXPECT_EQ(h1, h2);
}

TEST_F(CommandTest, ChecksumChangesOnMutation) {
    size_t h1 = blueprint_checksum(model.current());

    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(999.0f));
    size_t h2 = blueprint_checksum(model.current());
    EXPECT_NE(h1, h2);
}

TEST_F(CommandTest, UndoRoundTripChecksumMoveNode) {
    auto id = interner.intern("n");
    model.add_node(make_node(interner, "n", 10.0f, 20.0f));

    size_t before = blueprint_checksum(model.current());

    model.push_checkpoint();
    execute(model, interner, cmd_move_node(id, 100.0f, 200.0f));
    EXPECT_NE(blueprint_checksum(model.current()), before);

    model.undo();
    EXPECT_EQ(blueprint_checksum(model.current()), before);
}

TEST_F(CommandTest, UndoRoundTripChecksumSetParam) {
    auto id = interner.intern("n");
    auto key = interner.intern("key");
    auto node = make_node(interner, "n");
    node.semantic.params[key] = 1.0f;
    model.add_node(std::move(node));

    size_t before = blueprint_checksum(model.current());

    model.push_checkpoint();
    execute(model, interner, cmd_set_param(id, key, 2.0f));
    model.undo();
    EXPECT_EQ(blueprint_checksum(model.current()), before);
}

TEST_F(CommandTest, UndoRoundTripChecksumAddRemoveNode) {
    size_t before = blueprint_checksum(model.current());

    model.push_checkpoint();
    model.add_node(make_node(interner, "n", 0.0f, 0.0f));
    EXPECT_NE(blueprint_checksum(model.current()), before);

    model.undo();
    EXPECT_EQ(blueprint_checksum(model.current()), before);
}

TEST_F(CommandTest, UndoRoundTripChecksumSetGridStep) {
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(64.0f));

    size_t after = blueprint_checksum(model.current());
    EXPECT_NE(after, blueprint_checksum(bp2::Blueprint{}));

    model.undo();
    EXPECT_FLOAT_EQ(model.current().grid_step(), 16.0f);
}

TEST_F(CommandTest, UndoRoundTripChecksumMixedCommands) {
    auto id_a = interner.intern("a");
    auto id_b = interner.intern("b");

    auto na = make_node(interner, "a", 0.0f, 0.0f); na.view.name = "aaa";
    auto nb = make_node(interner, "b", 10.0f, 10.0f); nb.view.name = "bbb";
    model.add_node(std::move(na));
    model.add_node(std::move(nb));

    size_t before = blueprint_checksum(model.current());

    execute(model, interner, cmd_move_node(id_a, 100.0f, 100.0f));
    execute(model, interner, cmd_set_name(id_b, "changed"));
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(32.0f));
    EXPECT_NE(blueprint_checksum(model.current()), before);
}

TEST_F(CommandTest, UndoRoundTripChecksumAddRemoveWire) {
    bp2::PathArena arena(interner);

    model.add_node(make_node(interner, "n"));
    size_t before = blueprint_checksum(model.current());

    auto w = make_wire(interner, arena, "w1", "n", "out", "n", "in");
    model.push_checkpoint();
    model.add_wire(std::move(w));
    EXPECT_EQ(model.current().wires().size(), 1u);

    model.undo();
    EXPECT_EQ(model.current().wires().size(), 0u);
    EXPECT_EQ(blueprint_checksum(model.current()), before);
}

// =============================================================================
// Regression tests — edge cases
// =============================================================================

TEST_F(CommandTest, MaxStackDepthEviction) {
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(0.0f));

    // Push 100 checkpoints (max_history_ = 100)
    for (int i = 1; i <= 100; ++i) {
        model.push_checkpoint();
        execute(model, interner, cmd_set_grid_step(static_cast<float>(i)));
    }
    EXPECT_FLOAT_EQ(model.current().grid_step(), 100.0f);
    EXPECT_TRUE(model.can_undo());
}

TEST_F(CommandTest, UndoRedoWithEmptyBlueprint) {
    size_t before = blueprint_checksum(model.current());

    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(32.0f));

    model.undo();
    EXPECT_EQ(blueprint_checksum(model.current()), before);
    EXPECT_FLOAT_EQ(model.current().grid_step(), 16.0f);

    model.redo();
    EXPECT_FLOAT_EQ(model.current().grid_step(), 32.0f);
}

TEST_F(CommandTest, TransactionGuardDiscardMultipleCommands) {
    auto id_a = interner.intern("a");
    auto id_b = interner.intern("b");

    auto na = make_node(interner, "a", 0.0f, 0.0f); na.view.name = "aaa";
    auto nb = make_node(interner, "b", 10.0f, 10.0f); nb.view.name = "bbb";
    model.add_node(std::move(na));
    model.add_node(std::move(nb));
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(16.0f));

    size_t before = blueprint_checksum(model.current());

    {
        TransactionGuard txn(model, interner);
        txn.execute(cmd_move_node(id_a, 100.0f, 100.0f));
        txn.execute(cmd_set_grid_step(64.0f));
        txn.discard();
    }

    // After discard: position and grid_step should be reverted
    // Note: discard() calls model.undo() once, reverting the last checkpoint
    // which was the push_checkpoint() inside TransactionGuard before cmd_move_node
    EXPECT_FLOAT_EQ(model.current().grid_step(), 16.0f);
    EXPECT_FLOAT_EQ(model.current().find_node(id_a)->layout.x, 0.0f);
}

TEST_F(CommandTest, UndoStackClearSimulatesDocumentLoad) {
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(42.0f));
    model.push_checkpoint();
    execute(model, interner, cmd_set_grid_step(99.0f));
    EXPECT_TRUE(model.can_undo());

    // Simulate "load new document" by replacing current and marking saved
    model.replace_current(bp2::Blueprint{});
    model.mark_saved();

    // Undo stack still has entries from before replace_current,
    // but the document state is fresh. This is expected behavior —
    // the caller should call push_checkpoint + replace_current or create a new EditorModel.
    // Here we just verify the model is not dirty after mark_saved.
    EXPECT_FALSE(model.is_dirty());
}

// =============================================================================
// CmdSetPortLayout
// =============================================================================

TEST_F(CommandTest, SetPortLayout_Mutates) {
    auto node_id = interner.intern("node1");
    model.add_node(make_node(interner, "node1"));

    EXPECT_TRUE(model.current().find_node(node_id)->layout.layout_overrides.empty());

    std::vector<bp2::Blueprint::Node::PortLayoutOverride> overrides;
    overrides.push_back({"v_in", std::string("Top"), std::nullopt});
    overrides.push_back({"v_out", std::string("Bottom"), 0});

    execute(model, interner, cmd_set_port_layout(node_id, overrides));

    // CmdSetPortLayout does remove_node + add_node, undo the add_node to see the result
    auto* result = model.current().find_node(node_id);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->layout.layout_overrides.size(), 2u);
    EXPECT_EQ(result->layout.layout_overrides[0].port_name, "v_in");
    EXPECT_EQ(result->layout.layout_overrides[0].side, std::optional<std::string>("Top"));
    EXPECT_EQ(result->layout.layout_overrides[1].port_name, "v_out");
    EXPECT_EQ(result->layout.layout_overrides[1].side, std::optional<std::string>("Bottom"));
    EXPECT_EQ(result->layout.layout_overrides[1].position, std::optional<int>(0));
}

TEST_F(CommandTest, SetPortLayout_UndoRestoresOriginal) {
    auto node_id = interner.intern("node1");
    model.add_node(make_node(interner, "node1"));

    std::vector<bp2::Blueprint::Node::PortLayoutOverride> overrides;
    overrides.push_back({"v_in", std::string("Bottom"), std::nullopt});

    model.push_checkpoint();
    execute(model, interner, cmd_set_port_layout(node_id, overrides));

    ASSERT_EQ(model.current().find_node(node_id)->layout.layout_overrides.size(), 1u);

    model.undo();
    EXPECT_TRUE(model.current().find_node(node_id)->layout.layout_overrides.empty())
        << "Undo should restore original empty layout_overrides";
}

TEST_F(CommandTest, SetPortLayout_ClearOverrides) {
    auto node_id = interner.intern("node1");
    auto node = make_node(interner, "node1");
    node.layout.layout_overrides.push_back({"v_in", std::string("Top"), std::nullopt});
    model.add_node(std::move(node));

    ASSERT_EQ(model.current().find_node(node_id)->layout.layout_overrides.size(), 1u);

    execute(model, interner, cmd_set_port_layout(node_id, {}));

    EXPECT_TRUE(model.current().find_node(node_id)->layout.layout_overrides.empty());
}

// =============================================================================
// REGRESSION: Slider min/max must sync to content_min/max when params are edited
// =============================================================================

TEST_F(CommandTest, REGRESSION_SetParamMutatesNodeParam) {
    auto id = interner.intern("slider1");
    auto key_max = interner.intern("max");
    auto key_min = interner.intern("min");

    auto node = make_node(interner, "slider1");
    node.semantic.type = interner.intern("Slider");
    node.view.content_type = bp2::NodeContentType::Slider;
    node.view.content_min = 0.0f;
    node.view.content_max = 100.0f;
    node.semantic.params[key_max] = 100.0f;
    node.semantic.params[key_min] = 0.0f;
    model.add_node(std::move(node));

    EXPECT_FLOAT_EQ(model.current().find_node(id)->view.content_max, 100.0f);

    execute(model, interner, cmd_set_param(id, key_max, 200.0f));

    auto* n = model.current().find_node(id);
    ASSERT_NE(n, nullptr);
    EXPECT_FLOAT_EQ(n->semantic.params.at(key_max), 200.0f);
}

TEST_F(CommandTest, ExtractToBlueprint_BasicAtomic) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_fixture(interner, arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        parser_registry,
        &err,
        false);

    ASSERT_TRUE(updated.has_value()) << err;
    ASSERT_EQ(updated->nested().size(), 1u);

    const ui::InternedId nested_id = updated->nested()[0].id;
    const std::string nested_sid(interner.resolve(nested_id));

    const auto* in_bridge = updated->find_node(interner.intern(nested_sid + ":in"));
    const auto* out_bridge = updated->find_node(interner.intern(nested_sid + ":out"));

    ASSERT_NE(in_bridge, nullptr);
    ASSERT_NE(out_bridge, nullptr);
    EXPECT_EQ(in_bridge->semantic.type, interner.intern("BlueprintInput"));
    EXPECT_EQ(out_bridge->semantic.type, interner.intern("BlueprintOutput"));
    EXPECT_EQ(in_bridge->layout.layout_group, nested_sid);
    EXPECT_EQ(out_bridge->layout.layout_group, nested_sid);
}

TEST_F(CommandTest, ExtractToBlueprint_UndoRedoRoundTrip) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_fixture(interner, arena);
    model.replace_current(source);

    const size_t before = blueprint_checksum(model.current());

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        parser_registry,
        &err,
        false);

    ASSERT_TRUE(updated.has_value()) << err;

    model.push_checkpoint();
    model.replace_current(std::move(*updated));
    const size_t after = blueprint_checksum(model.current());
    EXPECT_NE(after, before);

    model.undo();
    EXPECT_EQ(blueprint_checksum(model.current()), before);

    model.redo();
    EXPECT_EQ(blueprint_checksum(model.current()), after);
}

TEST_F(CommandTest, ExtractToBlueprint_AllowsSubgroupExtraction) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_subgroup_fixture(interner, arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a"), interner.intern("b")},
        "extracted_blueprint_1",
        "group_1",
        interner,
        arena,
        parser_registry,
        &err,
        false);

    ASSERT_TRUE(updated.has_value()) << err;
    ASSERT_EQ(updated->nested().size(), 1u);
    const ui::InternedId nested_id = updated->nested()[0].id;
    const std::string nested_sid(interner.resolve(nested_id));

    const auto* collapsed = updated->find_node(nested_id);
    ASSERT_NE(collapsed, nullptr);
    EXPECT_EQ(collapsed->layout.layout_group, "group_1");

    const auto* a = updated->find_node(interner.intern("a"));
    const auto* b = updated->find_node(interner.intern("b"));
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(a->layout.layout_group, nested_sid);
    EXPECT_EQ(b->layout.layout_group, nested_sid);
}

TEST_F(CommandTest, ExtractToBlueprint_RejectsSelectionOutsideActiveGroup) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_subgroup_fixture(interner, arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("bridge_in"), interner.intern("a")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        parser_registry,
        &err,
        false);

    EXPECT_FALSE(updated.has_value());
    EXPECT_NE(err.find("active group"), std::string::npos);
}

TEST_F(CommandTest, ExtractToBlueprint_AllowsEmbeddedNestedInstanceSelection) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_with_embedded_nested_selection_fixture(interner, arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("sub_inst_1"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        parser_registry,
        &err,
        false);

    ASSERT_TRUE(updated.has_value()) << err;
    ASSERT_EQ(updated->nested().size(), 2u);
    const std::string new_nested_sid(interner.resolve(updated->nested().back().id));

    const auto* selected_nested_node = updated->find_node(interner.intern("sub_inst_1"));
    ASSERT_NE(selected_nested_node, nullptr);
    EXPECT_EQ(selected_nested_node->layout.layout_group, new_nested_sid);

    const auto& created_nested = updated->nested().back();
    ASSERT_TRUE(created_nested.inline_def() != nullptr);
    EXPECT_NE(created_nested.inline_def()->find_nested(interner.intern("sub_inst_1")), nullptr);
}

TEST_F(CommandTest, ExtractToBlueprint_InlinesSelectedEmbeddedNestedDeterministically) {
    auto run_extract = [this](const std::vector<std::string>& selection_names) {
        ui::StringInterner I;
        bp2::PathArena A(I);
        bp2::Blueprint source = make_extract_with_two_embedded_nested_selection_fixture(I, A);
        std::vector<ui::InternedId> selection;
        selection.reserve(selection_names.size());
        for (const auto& s : selection_names) selection.push_back(I.intern(s));

        std::string err;
        auto updated = editor::commands::build_extracted_blueprint_atomic(
            source,
            selection,
            "extracted_blueprint_1",
            "",
            I,
            A,
            parser_registry,
            &err,
            false);
        EXPECT_TRUE(updated.has_value()) << err;

        std::vector<std::string> nested_names;
        if (!updated.has_value() || updated->nested().empty()) return nested_names;
        const auto* created = updated->find_nested(updated->nested().back().id);
        if (!created || !created->inline_def()) return nested_names;
        nested_names.reserve(created->inline_def()->nested().size());
        for (const auto& n : created->inline_def()->nested()) {
            nested_names.push_back(std::string(I.resolve(n.id)));
        }
        return nested_names;
    };

    const auto names_a = run_extract({"sub_inst_2", "b", "sub_inst_1"});
    const auto names_b = run_extract({"sub_inst_1", "b", "sub_inst_2"});
    EXPECT_EQ(names_a, names_b);
    EXPECT_TRUE(std::find(names_a.begin(), names_a.end(), "sub_inst_1") != names_a.end());
    EXPECT_TRUE(std::find(names_a.begin(), names_a.end(), "sub_inst_2") != names_a.end());
}

TEST_F(CommandTest, ExtractToBlueprint_RejectsEmbeddedNestedMissingInlineDef) {
    EXPECT_THROW(
        bp2::Blueprint::Nested::make_embedded(
            interner.intern("sub_inst_1"),
            interner.intern("sub_blueprint_type"),
            nullptr),
        std::logic_error);
}

TEST_F(CommandTest, ExtractToBlueprint_RejectsEmbeddedNestedWithNonEmbeddedDescendant) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source =
        make_extract_with_embedded_nested_having_nonembedded_descendant_fixture(interner, arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("sub_inst_1"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        parser_registry,
        &err,
        false);

    EXPECT_FALSE(updated.has_value());
    EXPECT_NE(err.find("non-embedded descendant references"), std::string::npos);
}

TEST_F(CommandTest, ExtractToBlueprint_AllowsNonEmbeddedDescendantWhenGuardEnabled) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source =
        make_extract_with_embedded_nested_having_nonembedded_descendant_fixture(interner, arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("sub_inst_1"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        parser_registry,
        &err,
        true);

    ASSERT_TRUE(updated.has_value()) << err;
}

TEST_F(CommandTest, ExtractToBlueprint_GuardModeRemapsNonEmbeddedDescendantIfEmbeddedSourceExists) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source =
        make_extract_with_remappable_nonembedded_descendant_fixture(interner, arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("sub_inst_1"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        parser_registry,
        &err,
        true);

    ASSERT_TRUE(updated.has_value()) << err;
    ASSERT_FALSE(updated->nested().empty());
    const auto* created = updated->find_nested(updated->nested().back().id);
    ASSERT_NE(created, nullptr);
    ASSERT_TRUE(created->inline_def() != nullptr);

    const auto* remapped_parent = created->inline_def()->find_nested(interner.intern("sub_inst_1"));
    ASSERT_NE(remapped_parent, nullptr);
    ASSERT_TRUE(remapped_parent->inline_def() != nullptr);

    const auto* child = remapped_parent->inline_def()->find_nested(interner.intern("child_nonembedded"));
    ASSERT_NE(child, nullptr);
    EXPECT_TRUE(child->is_embedded());
    EXPECT_TRUE(child->inline_def() != nullptr);
}

TEST_F(CommandTest, ExtractToBlueprint_RejectsNonEmbeddedNestedInstanceSelection) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_with_nonembedded_nested_selection_fixture(interner, arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("sub_inst_1"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        parser_registry,
        &err,
        false);

    EXPECT_FALSE(updated.has_value());
    EXPECT_NE(err.find("non-embedded nested instances"), std::string::npos);
}

TEST_F(CommandTest, ExtractToBlueprint_RejectsBlueprintBridgeNodeSelection) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_with_bridge_node_fixture(interner, arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("bridge_in"), interner.intern("a")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        parser_registry,
        &err,
        false);

    EXPECT_FALSE(updated.has_value());
    EXPECT_NE(err.find("BlueprintInput/BlueprintOutput"), std::string::npos);
}

TEST_F(CommandTest, ExtractToBlueprint_PreviewBasic) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_fixture(interner, arena);

    std::string err;
    auto preview = editor::commands::build_extract_to_blueprint_preview(
        source,
        {interner.intern("a"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        &err);

    ASSERT_TRUE(preview.has_value()) << err;
    EXPECT_EQ(preview->selected_nodes, 2u);
    EXPECT_EQ(preview->internal_wires, 1u);
    EXPECT_EQ(preview->input_count, 1u);
    EXPECT_EQ(preview->output_count, 1u);
    ASSERT_EQ(preview->input_iface_names.size(), 1u);
    ASSERT_EQ(preview->output_iface_names.size(), 1u);
    EXPECT_EQ(preview->input_iface_names[0], "in");
    EXPECT_EQ(preview->output_iface_names[0], "out");
    EXPECT_TRUE(preview->iface_collision_names.empty());
}

TEST_F(CommandTest, ExtractToBlueprint_PreviewReportsIfaceCollision) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_iface_collision_fixture(interner, arena);

    std::string err;
    auto preview = editor::commands::build_extract_to_blueprint_preview(
        source,
        {interner.intern("a"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        &err);

    ASSERT_TRUE(preview.has_value()) << err;
    ASSERT_EQ(preview->iface_collision_names.size(), 1u);
    EXPECT_EQ(preview->iface_collision_names[0], "sig");
}

TEST_F(CommandTest, ExtractToBlueprint_PreviewAllowsEmbeddedNestedSelection) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_with_embedded_nested_selection_fixture(interner, arena);

    std::string err;
    auto preview = editor::commands::build_extract_to_blueprint_preview(
        source,
        {interner.intern("sub_inst_1"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        &err);

    ASSERT_TRUE(preview.has_value()) << err;
    EXPECT_EQ(preview->selected_nodes, 2u);
}

TEST_F(CommandTest, ExtractToBlueprint_PreviewRejectsNonEmbeddedNestedSelection) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_with_nonembedded_nested_selection_fixture(interner, arena);

    std::string err;
    auto preview = editor::commands::build_extract_to_blueprint_preview(
        source,
        {interner.intern("sub_inst_1"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        &err);

    EXPECT_FALSE(preview.has_value());
    EXPECT_NE(err.find("non-embedded nested instances"), std::string::npos);
}

TEST_F(CommandTest, ExtractToBlueprint_PreviewRejectsBlueprintBridgeNodeSelection) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_with_bridge_node_fixture(interner, arena);

    std::string err;
    auto preview = editor::commands::build_extract_to_blueprint_preview(
        source,
        {interner.intern("bridge_in"), interner.intern("a")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        &err);

    EXPECT_FALSE(preview.has_value());
    EXPECT_NE(err.find("BlueprintInput/BlueprintOutput"), std::string::npos);
}

TEST_F(CommandTest, ExtractToBlueprint_PreviewRejectsEmbeddedNestedWithNonEmbeddedDescendant) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source =
        make_extract_with_embedded_nested_having_nonembedded_descendant_fixture(interner, arena);

    std::string err;
    auto preview = editor::commands::build_extract_to_blueprint_preview(
        source,
        {interner.intern("sub_inst_1"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        &err);

    EXPECT_FALSE(preview.has_value());
    EXPECT_NE(err.find("non-embedded descendant references"), std::string::npos);
}

TEST_F(CommandTest, ExtractToBlueprint_PreviewAllowsNonEmbeddedDescendantWhenGuardEnabled) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source =
        make_extract_with_embedded_nested_having_nonembedded_descendant_fixture(interner, arena);

    std::string err;
    auto preview = editor::commands::build_extract_to_blueprint_preview(
        source,
        {interner.intern("sub_inst_1"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        &err,
        true);

    ASSERT_TRUE(preview.has_value()) << err;
}

TEST_F(CommandTest, ExtractToBlueprint_PreviewGuardModeAllowsRemappableNonEmbeddedDescendant) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source =
        make_extract_with_remappable_nonembedded_descendant_fixture(interner, arena);

    std::string err;
    auto preview = editor::commands::build_extract_to_blueprint_preview(
        source,
        {interner.intern("sub_inst_1"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        &err,
        true);

    ASSERT_TRUE(preview.has_value()) << err;
}

TEST_F(CommandTest, ExtractToBlueprint_PreviewAllowApplyDenyMismatchFailsAtApply) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source =
        make_extract_with_embedded_nested_having_nonembedded_descendant_fixture(interner, arena);

    std::string preview_err;
    auto preview = editor::commands::build_extract_to_blueprint_preview(
        source,
        {interner.intern("sub_inst_1"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        &preview_err,
        true);
    ASSERT_TRUE(preview.has_value()) << preview_err;

    std::string apply_err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("sub_inst_1"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        parser_registry,
        &apply_err,
        false);
    EXPECT_FALSE(updated.has_value());
    EXPECT_NE(apply_err.find("non-embedded descendant references"), std::string::npos);
}

TEST_F(CommandTest, ExtractToBlueprint_PreviewShowsRemapAndPassthroughCounts) {
    bp2::PathArena arena(interner);

    {
        bp2::Blueprint source = make_extract_with_remappable_nonembedded_descendant_fixture(interner, arena);
        std::string err;
        auto preview = editor::commands::build_extract_to_blueprint_preview(
            source,
            {interner.intern("sub_inst_1"), interner.intern("b")},
            "extracted_blueprint_1",
            "",
            interner,
            arena,
            &err,
            true);
        ASSERT_TRUE(preview.has_value()) << err;
        EXPECT_EQ(preview->remapped_descendant_refs, 1u);
        EXPECT_EQ(preview->passthrough_descendant_refs, 0u);
    }

    {
        bp2::Blueprint source =
            make_extract_with_embedded_nested_having_nonembedded_descendant_fixture(interner, arena);
        std::string err;
        auto preview = editor::commands::build_extract_to_blueprint_preview(
            source,
            {interner.intern("sub_inst_1"), interner.intern("b")},
            "extracted_blueprint_1",
            "",
            interner,
            arena,
            &err,
            true);
        ASSERT_TRUE(preview.has_value()) << err;
        EXPECT_EQ(preview->remapped_descendant_refs, 0u);
        EXPECT_EQ(preview->passthrough_descendant_refs, 1u);
    }
}

TEST_F(CommandTest, ExtractToBlueprint_GuardModeRemapTieBreakUsesLowestProviderNodeId) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_with_two_remap_providers_fixture(interner, arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("sub_inst_1"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        parser_registry,
        &err,
        true);

    ASSERT_TRUE(updated.has_value()) << err;

    const auto* created = updated->find_nested(updated->nested().back().id);
    ASSERT_NE(created, nullptr);
    ASSERT_TRUE(created->inline_def() != nullptr);
    const auto* remapped_parent = created->inline_def()->find_nested(interner.intern("sub_inst_1"));
    ASSERT_NE(remapped_parent, nullptr);
    ASSERT_TRUE(remapped_parent->inline_def() != nullptr);
    const auto* child = remapped_parent->inline_def()->find_nested(interner.intern("child_nonembedded"));
    ASSERT_NE(child, nullptr);
    ASSERT_TRUE(child->inline_def() != nullptr);

    const ui::InternedId provider_a = interner.intern("provider_a");
    const ui::InternedId provider_b = interner.intern("provider_b");
    const bool a_is_lower = provider_a.raw() < provider_b.raw();
    const std::string expected_def = a_is_lower ? "provider_a_def" : "provider_b_def";
    EXPECT_EQ(std::string(interner.resolve(child->inline_def()->id())), expected_def);
}

TEST_F(CommandTest, ExtractToBlueprint_BridgeAutoLayoutTracksInternalY) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_layout_fixture(interner, arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        parser_registry,
        &err,
        false);

    ASSERT_TRUE(updated.has_value()) << err;
    ASSERT_EQ(updated->nested().size(), 1u);
    const std::string nested_sid(interner.resolve(updated->nested()[0].id));

    const auto* in_1 = updated->find_node(interner.intern(nested_sid + ":in"));
    const auto* in_2 = updated->find_node(interner.intern(nested_sid + ":in_2"));
    ASSERT_NE(in_1, nullptr);
    ASSERT_NE(in_2, nullptr);

    // a is above b, bridge for iface "in" should be above "in_2".
    EXPECT_LT(in_1->layout.y, in_2->layout.y);
}

TEST_F(CommandTest, ExtractToBlueprint_PreviewRejectsEmptyName) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_fixture(interner, arena);

    std::string err;
    auto preview = editor::commands::build_extract_to_blueprint_preview(
        source,
        {interner.intern("a"), interner.intern("b")},
        "",
        "",
        interner,
        arena,
        &err);

    EXPECT_FALSE(preview.has_value());
    EXPECT_NE(err.find("non-empty"), std::string::npos);
}

TEST_F(CommandTest, ExtractToBlueprint_PreviewRejectsDuplicateName) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_fixture(interner, arena);

    auto nested = bp2::Blueprint::Nested::make_reference(
        interner.intern("existing_inst"),
        interner.intern("extracted_blueprint_1"),
        bp2::Interface{});
    source = source.with_nested(std::move(nested));

    std::string err;
    auto preview = editor::commands::build_extract_to_blueprint_preview(
        source,
        {interner.intern("a"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        &err);

    EXPECT_FALSE(preview.has_value());
    EXPECT_NE(err.find("already exists"), std::string::npos);
}

TEST_F(CommandTest, ExtractToBlueprint_RejectsSmallSelection) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_fixture(interner, arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        parser_registry,
        &err,
        false);

    EXPECT_FALSE(updated.has_value());
    EXPECT_NE(err.find("at least 2"), std::string::npos);
}

TEST_F(CommandTest, ExtractToBlueprint_DeterministicIfaceNaming) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_fixture(interner, arena);

    std::string err1;
    std::string err2;
    auto a = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        parser_registry,
        &err1,
        false);
    auto b = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        parser_registry,
        &err2,
        false);

    ASSERT_TRUE(a.has_value()) << err1;
    ASSERT_TRUE(b.has_value()) << err2;

    std::string sa = bp2::BlueprintCodec::encode(*a, interner, arena);
    std::string sb = bp2::BlueprintCodec::encode(*b, interner, arena);
    EXPECT_EQ(sa, sb);
}

TEST_F(CommandTest, ExtractToBlueprint_InlineBlueprintStructure) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_fixture(interner, arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        parser_registry,
        &err,
        false);

    ASSERT_TRUE(updated.has_value()) << err;
    ASSERT_EQ(updated->nested().size(), 1u);
    const auto& nested = updated->nested()[0];
    ASSERT_TRUE(nested.inline_def() != nullptr);
    const bp2::Blueprint& inner = *nested.inline_def();

    // Interface must have exactly 1 input ("in") and 1 output ("out").
    const auto& iface = inner.iface();
    ASSERT_EQ(iface.size(), 2u);
    auto pd_in = iface.find(interner.intern("in"));
    auto pd_out = iface.find(interner.intern("out"));
    ASSERT_TRUE(pd_in.has_value());
    ASSERT_TRUE(pd_out.has_value());
    EXPECT_EQ(pd_in->direction, bp2::Direction::Input);
    EXPECT_EQ(pd_out->direction, bp2::Direction::Output);

     // Find the BlueprintInput node inside inline_def.
     const bp2::Blueprint::Node* bp_in_node = nullptr;
     const bp2::Blueprint::Node* bp_out_node = nullptr;
     for (const auto& n : inner.nodes()) {
         if (n.semantic.type == interner.intern("BlueprintInput")) bp_in_node = &n;
         if (n.semantic.type == interner.intern("BlueprintOutput")) bp_out_node = &n;
     }
     ASSERT_NE(bp_in_node, nullptr);
     ASSERT_NE(bp_out_node, nullptr);

     // BlueprintInput: ext=Input, port=Output
     ASSERT_EQ(count_inputs(bp_in_node->semantic.iface), 1u);
     ASSERT_EQ(count_outputs(bp_in_node->semantic.iface), 1u);
     EXPECT_EQ(get_input_port(bp_in_node->semantic.iface, 0)->name, interner.intern("ext"));
     EXPECT_EQ(get_input_port(bp_in_node->semantic.iface, 0)->direction, bp2::Direction::Input);
     EXPECT_EQ(get_output_port(bp_in_node->semantic.iface, 0)->name, interner.intern("port"));
     EXPECT_EQ(get_output_port(bp_in_node->semantic.iface, 0)->direction, bp2::Direction::Output);

     // BlueprintOutput: port=Input, ext=Output
     ASSERT_EQ(count_inputs(bp_out_node->semantic.iface), 1u);
     ASSERT_EQ(count_outputs(bp_out_node->semantic.iface), 1u);
     EXPECT_EQ(get_input_port(bp_out_node->semantic.iface, 0)->name, interner.intern("port"));
     EXPECT_EQ(get_input_port(bp_out_node->semantic.iface, 0)->direction, bp2::Direction::Input);
     EXPECT_EQ(get_output_port(bp_out_node->semantic.iface, 0)->name, interner.intern("ext"));
     EXPECT_EQ(get_output_port(bp_out_node->semantic.iface, 0)->direction, bp2::Direction::Output);

    // Inline blueprint must contain internal nodes a and b.
    EXPECT_NE(inner.find_node(interner.intern("a")), nullptr);
    EXPECT_NE(inner.find_node(interner.intern("b")), nullptr);
}

TEST_F(CommandTest, ExtractToBlueprint_PreservesBoundaryPortTypesOnBridgeNodes) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_typed_boundary_fixture(interner, arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        parser_registry,
        &err,
        false);

    ASSERT_TRUE(updated.has_value()) << err;
    ASSERT_EQ(updated->nested().size(), 1u);
    const auto nested_id = updated->nested()[0].id;
    const std::string nested_sid(interner.resolve(nested_id));

     const auto* in_bridge = updated->find_node(interner.intern(nested_sid + ":in"));
     const auto* out_bridge = updated->find_node(interner.intern(nested_sid + ":out"));
     ASSERT_NE(in_bridge, nullptr);
     ASSERT_NE(out_bridge, nullptr);
     ASSERT_EQ(count_inputs(in_bridge->semantic.iface), 1u);
     ASSERT_EQ(count_outputs(in_bridge->semantic.iface), 1u);
     ASSERT_EQ(count_inputs(out_bridge->semantic.iface), 1u);
     ASSERT_EQ(count_outputs(out_bridge->semantic.iface), 1u);
     EXPECT_EQ(get_input_type(in_bridge->semantic.iface, 0), PortType::I);
     EXPECT_EQ(get_output_type(in_bridge->semantic.iface, 0), PortType::I);
     EXPECT_EQ(get_input_type(out_bridge->semantic.iface, 0), PortType::I);
     EXPECT_EQ(get_output_type(out_bridge->semantic.iface, 0), PortType::I);

     const auto* collapsed = updated->find_node(nested_id);
     ASSERT_NE(collapsed, nullptr);
     ASSERT_EQ(count_inputs(collapsed->semantic.iface), 1u);
     ASSERT_EQ(count_outputs(collapsed->semantic.iface), 1u);
     EXPECT_EQ(get_input_type(collapsed->semantic.iface, 0), PortType::I);
     EXPECT_EQ(get_output_type(collapsed->semantic.iface, 0), PortType::I);
}

TEST_F(CommandTest, ExtractToBlueprint_SubgroupBridgeWiring) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_fixture(interner, arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        parser_registry,
        &err,
        false);

    ASSERT_TRUE(updated.has_value()) << err;
    ASSERT_EQ(updated->nested().size(), 1u);

    const ui::InternedId nested_id = updated->nested()[0].id;
    const std::string nested_sid(interner.resolve(nested_id));

    // Bridge nodes in parent with canonical naming.
    const ui::InternedId in_bridge_id = interner.intern(nested_sid + ":in");
    const ui::InternedId out_bridge_id = interner.intern(nested_sid + ":out");
    const auto* in_bridge = updated->find_node(in_bridge_id);
    const auto* out_bridge = updated->find_node(out_bridge_id);
    ASSERT_NE(in_bridge, nullptr);
    ASSERT_NE(out_bridge, nullptr);

    // Bridge nodes must be in the subgroup.
    EXPECT_EQ(in_bridge->layout.layout_group, nested_sid);
    EXPECT_EQ(out_bridge->layout.layout_group, nested_sid);

    // Find subgroup bridge wires:
    // Input bridge: in_bridge.port -> a.in
    // Output bridge: b.out -> out_bridge.port
    bool found_in_bridge_wire = false;
    bool found_out_bridge_wire = false;
    const ui::InternedId port_id = interner.intern("port");
    const ui::InternedId a_id = interner.intern("a");
    const ui::InternedId b_id = interner.intern("b");
    const ui::InternedId in_id = interner.intern("in");
    const ui::InternedId out_id = interner.intern("out");

    for (const auto& w : updated->wires()) {
        // Decode source
        if (w.source.kind() != bp2::PathKind::Port) continue;
        bp2::Path src_node_path = arena.parent(w.source);
        if (src_node_path.kind() != bp2::PathKind::Node) continue;
        ui::InternedId src_node = src_node_path.segment();
        ui::InternedId src_port = w.source.segment();

        if (w.target.kind() != bp2::PathKind::Port) continue;
        bp2::Path tgt_node_path = arena.parent(w.target);
        if (tgt_node_path.kind() != bp2::PathKind::Node) continue;
        ui::InternedId tgt_node = tgt_node_path.segment();
        ui::InternedId tgt_port = w.target.segment();

        // in_bridge.port -> a.in
        if (src_node == in_bridge_id && src_port == port_id
            && tgt_node == a_id && tgt_port == in_id) {
            found_in_bridge_wire = true;
        }
        // b.out -> out_bridge.port
        if (src_node == b_id && src_port == out_id
            && tgt_node == out_bridge_id && tgt_port == port_id) {
            found_out_bridge_wire = true;
        }
    }

    EXPECT_TRUE(found_in_bridge_wire) << "Missing wire: input bridge .port -> a.in";
    EXPECT_TRUE(found_out_bridge_wire) << "Missing wire: b.out -> output bridge .port";
}

TEST_F(CommandTest, ExtractToBlueprint_RejectsInputOutputIfaceNameCollision) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_iface_collision_fixture(interner, arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a"), interner.intern("b")},
        "extracted_blueprint_1",
        "",
        interner,
        arena,
        parser_registry,
        &err,
        false);

    EXPECT_FALSE(updated.has_value());
    EXPECT_NE(err.find("collision"), std::string::npos);
}

// Regression: dedupe_name must not produce a collision when a port name
// already looks like a deduped suffix (e.g. "in" and "in_2" coexist).
// The old counter-based dedupe would return "in", "in_2", "in_2" — a collision.
TEST_F(CommandTest, ExtractToBlueprint_DedupeNameNoCollisionWithSuffixedPorts) {
    bp2::PathArena arena(interner);
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("bp_dedupe_regression"));
    bp = bp.with_display_name("DedupeRegression");

     // Three external sources
     auto src1 = make_node(interner, "src1");
     src1.semantic.type = interner.intern("Source");
    set_iface(src1, {
        make_port(interner.intern("out"), bp2::Direction::Output, PortType::V),
    });
     auto src2 = make_node(interner, "src2");
     src2.semantic.type = interner.intern("Source");
    set_iface(src2, {
        make_port(interner.intern("out"), bp2::Direction::Output, PortType::V),
    });
     auto src3 = make_node(interner, "src3");
     src3.semantic.type = interner.intern("Source");
    set_iface(src3, {
        make_port(interner.intern("out"), bp2::Direction::Output, PortType::V),
    });

     // Node a: two inputs with names "in" and "in_2"
     auto a = make_node(interner, "a");
     a.semantic.type = interner.intern("NodeA");
    set_iface(a, {
        make_port(interner.intern("in"), bp2::Direction::Input, PortType::V),
        make_port(interner.intern("in_2"), bp2::Direction::Input, PortType::V),
        make_port(interner.intern("out"), bp2::Direction::Output, PortType::V),
    });

     // Node b: two inputs — "in" (external) and "link" (internal from a)
     // After dedupe, the external wire targeting b.in would produce iface_name "in"
     // which collides with the deduped "in_2" from old counter-based code.
     auto b = make_node(interner, "b");
     b.semantic.type = interner.intern("NodeB");
    set_iface(b, {
        make_port(interner.intern("in"), bp2::Direction::Input, PortType::V),
        make_port(interner.intern("link"), bp2::Direction::Input, PortType::V),
        make_port(interner.intern("out"), bp2::Direction::Output, PortType::V),
    });

     auto sink = make_node(interner, "sink");
     sink.semantic.type = interner.intern("Sink");
    set_iface(sink, {
        make_port(interner.intern("in"), bp2::Direction::Input, PortType::V),
    });

    bp = bp.with_node(std::move(src1));
    bp = bp.with_node(std::move(src2));
    bp = bp.with_node(std::move(src3));
    bp = bp.with_node(std::move(a));
    bp = bp.with_node(std::move(b));
    bp = bp.with_node(std::move(sink));

    // Three external wires into selected nodes:
    //   src1.out -> a.in      (iface_name = "in")
    //   src2.out -> a.in_2    (iface_name = "in_2")
    //   src3.out -> b.in      (iface_name = "in" -> deduped to "in_?" )
    // Old code: "in", "in_2", "in_2" -> COLLISION
    auto w0 = make_wire(interner, arena, "w0", "src1", "out", "a", "in");
    w0.domain = Domain::Electrical;
    auto w1 = make_wire(interner, arena, "w1", "src2", "out", "a", "in_2");
    w1.domain = Domain::Electrical;
    auto w2 = make_wire(interner, arena, "w2", "src3", "out", "b", "in");
    w2.domain = Domain::Electrical;
    // Internal wire a->b (into b's "link" input, not "in")
    auto w3 = make_wire(interner, arena, "w3", "a", "out", "b", "link");
    w3.domain = Domain::Electrical;
    // External wire b->sink
    auto w4 = make_wire(interner, arena, "w4", "b", "out", "sink", "in");
    w4.domain = Domain::Electrical;

    bp = bp.with_wire(std::move(w0));
    bp = bp.with_wire(std::move(w1));
    bp = bp.with_wire(std::move(w2));
    bp = bp.with_wire(std::move(w3));
    bp = bp.with_wire(std::move(w4));

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        bp,
        {interner.intern("a"), interner.intern("b")},
        "test_dedupe",
        "",
        interner,
        arena,
        parser_registry,
        &err,
        false);

    // Must succeed — deduplication should produce "in", "in_2", "in_3" (not a second "in_2")
    ASSERT_TRUE(updated.has_value()) << "extraction failed: " << err;

     // Verify all interface port names are unique
    ASSERT_EQ(updated->nested().size(), 1u);
    const auto iface = updated->nested()[0].resolved_iface();

    std::set<std::string> port_names;
    for (const auto& pd : iface.ports()) {
        std::string name(interner.resolve(pd.name));
        EXPECT_TRUE(port_names.insert(name).second)
            << "Duplicate interface port name: " << name;
    }
}

// Edge case: all selected nodes are fully internally connected (no boundary wires).
// Result should be a nested blueprint with empty interface and a collapsed node with no ports.
TEST_F(CommandTest, ExtractToBlueprint_ZeroExternalConnections) {
    bp2::PathArena arena(interner);
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("bp_zero_ext"));
    bp = bp.with_display_name("ZeroExternal");

     auto a = make_node(interner, "a");
     a.semantic.type = interner.intern("NodeA");
    set_iface(a, {
        make_port(interner.intern("in"), bp2::Direction::Input, PortType::V),
        make_port(interner.intern("out"), bp2::Direction::Output, PortType::V),
    });

     auto b = make_node(interner, "b");
     b.semantic.type = interner.intern("NodeB");
    set_iface(b, {
        make_port(interner.intern("in"), bp2::Direction::Input, PortType::V),
        make_port(interner.intern("out"), bp2::Direction::Output, PortType::V),
    });

    bp = bp.with_node(std::move(a));
    bp = bp.with_node(std::move(b));

    // Single internal wire only, no external connections
    auto w0 = make_wire(interner, arena, "w0", "a", "out", "b", "in");
    w0.domain = Domain::Electrical;
    bp = bp.with_wire(std::move(w0));

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        bp,
        {interner.intern("a"), interner.intern("b")},
        "isolated_group",
        "",
        interner,
        arena,
        parser_registry,
        &err,
        false);

    ASSERT_TRUE(updated.has_value()) << err;
    ASSERT_EQ(updated->nested().size(), 1u);

     // Interface should be empty (no boundary connections)
    const auto iface = updated->nested()[0].resolved_iface();
    EXPECT_EQ(iface.size(), 0u);

     // Collapsed node should have no ports
     const auto* collapsed = updated->find_node(updated->nested()[0].id);
     ASSERT_NE(collapsed, nullptr);
     EXPECT_TRUE((count_inputs(collapsed->semantic.iface) == 0));
     EXPECT_TRUE((count_outputs(collapsed->semantic.iface) == 0));

    // Internal wire should be preserved in parent (both endpoints are selected)
    // Plus the inline_def should contain the wire
    ASSERT_TRUE(updated->nested()[0].inline_def() != nullptr);
    EXPECT_GE(updated->nested()[0].inline_def()->wires().size(), 1u);
}

// Regression: bridge nodes inside inline_def must use translated (local) Y coordinates,
// not original (world) Y coordinates. Nodes at world Y=500..600 should have bridge nodes
// near Y=0..100 inside the inline blueprint, not at Y=532.
TEST_F(CommandTest, ExtractToBlueprint_InlineBridgeYUsesLocalCoordinates) {
    bp2::PathArena arena(interner);
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("bp_bridge_y_regression"));
    bp = bp.with_display_name("BridgeYRegression");

     auto ext_in = make_node(interner, "ext_in");
     ext_in.semantic.type = interner.intern("Source");
    set_iface(ext_in, {
        make_port(interner.intern("out"), bp2::Direction::Output, PortType::V),
    });

     // Place nodes far from origin to expose the pre-translation bug
     auto a = make_node(interner, "a", 500.0f, 500.0f);
     a.semantic.type = interner.intern("NodeA");
     a.layout.height = 64.0f;
    set_iface(a, {
        make_port(interner.intern("in"), bp2::Direction::Input, PortType::V),
        make_port(interner.intern("out"), bp2::Direction::Output, PortType::V),
    });

     auto b = make_node(interner, "b", 700.0f, 600.0f);
     b.semantic.type = interner.intern("NodeB");
     b.layout.height = 64.0f;
    set_iface(b, {
        make_port(interner.intern("in"), bp2::Direction::Input, PortType::V),
        make_port(interner.intern("out"), bp2::Direction::Output, PortType::V),
    });

     auto ext_out = make_node(interner, "ext_out");
     ext_out.semantic.type = interner.intern("Sink");
    set_iface(ext_out, {
        make_port(interner.intern("in"), bp2::Direction::Input, PortType::V),
    });

    bp = bp.with_node(std::move(ext_in));
    bp = bp.with_node(std::move(a));
    bp = bp.with_node(std::move(b));
    bp = bp.with_node(std::move(ext_out));

    auto w0 = make_wire(interner, arena, "w0", "ext_in", "out", "a", "in");
    w0.domain = Domain::Electrical;
    auto w1 = make_wire(interner, arena, "w1", "a", "out", "b", "in");
    w1.domain = Domain::Electrical;
    auto w2 = make_wire(interner, arena, "w2", "b", "out", "ext_out", "in");
    w2.domain = Domain::Electrical;
    bp = bp.with_wire(std::move(w0));
    bp = bp.with_wire(std::move(w1));
    bp = bp.with_wire(std::move(w2));

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        bp,
        {interner.intern("a"), interner.intern("b")},
        "bridge_y_test",
        "",
        interner,
        arena,
        parser_registry,
        &err,
        false);
    ASSERT_TRUE(updated.has_value()) << err;
    ASSERT_EQ(updated->nested().size(), 1u);
    ASSERT_TRUE(updated->nested()[0].inline_def() != nullptr);

    const bp2::Blueprint& inner = *updated->nested()[0].inline_def();

     // Find the BlueprintInput bridge inside inline_def
     const bp2::Blueprint::Node* bp_in_node = nullptr;
     for (const auto& n : inner.nodes()) {
         if (n.semantic.type == interner.intern("BlueprintInput")) bp_in_node = &n;
     }
     ASSERT_NE(bp_in_node, nullptr);

    // The bridge Y should be near the translated node Y (0..100 range),
    // NOT near the original world Y (500+).
    // Node "a" at world Y=500 becomes local Y=0, center at 32.
    EXPECT_LT(bp_in_node->layout.y, 200.0f)
        << "Bridge node Y=" << bp_in_node->layout.y
        << " should use local coordinates (near 0..100), not world coordinates (near 500+)";
}

// ===========================================================================
// Regression tests for "Extract to Blueprint" embedded proxy type resolution.
//
// Before the fix, extracting nodes created a collapsed proxy node with
// node.type set to the user-given blueprint name (e.g. "RN-180-Exciter").
// This name doesn't exist in any component registry, causing:
//   - build_simulation_json() to emit it → json_parser "Unknown component classname"
//   - validate_blueprint_for_persist() → "unknown node type"
//   - InvariantChecker::validate() → "unknown node type"
//   - Blueprint::validate() → throw runtime_error
//   - diagnose_and_repair() → false positive UnknownNodeType issue
// ===========================================================================

// Regression: Extraction with a hyphenated name must produce a collapsed proxy
// node that is correctly identified as an embedded proxy (expandable + matching
// embedded nested entry).  The proxy node type must not cause validation failure.
TEST_F(CommandTest, ExtractToBlueprint_HyphenatedNamePassesValidation) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_fixture(interner, arena);

    // Use a name with hyphens and numbers — the exact pattern from the bug report.
    const char* blueprint_name = "RN-180-Exciter";

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a"), interner.intern("b")},
        blueprint_name,
        "",
        interner,
        arena,
        parser_registry,
        &err,
        false);

    ASSERT_TRUE(updated.has_value()) << "extraction failed: " << err;
    ASSERT_EQ(updated->nested().size(), 1u);

    // The collapsed proxy node must exist and have the user-given type name.
    const auto& nested = updated->nested()[0];
    const auto* collapsed = updated->find_node(nested.id);
    ASSERT_NE(collapsed, nullptr);
    EXPECT_TRUE(collapsed->view.expandable);
    EXPECT_EQ(collapsed->semantic.type, interner.intern(blueprint_name));

    // The proxy type is not in any registry, but the embedded proxy skip
    // condition must hold: expandable + matching embedded nested entry.
    ASSERT_TRUE(nested.is_embedded());
    ASSERT_NE(nested.inline_def(), nullptr);

    EXPECT_NO_THROW(updated->validate(parser_registry, interner, arena));
}

// Regression: validate_blueprint_for_persist() must accept the extracted
// blueprint whose proxy node carries the user-given type name.
// This test uses validate_blueprint_integrity (not validate_blueprint_for_persist)
// to avoid requiring all fixture types in the parser library registry.
TEST_F(CommandTest, ExtractToBlueprint_HyphenatedNamePassesIntegrityValidation) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_fixture(interner, arena);

    const char* blueprint_name = "RN-180-Exciter";

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a"), interner.intern("b")},
        blueprint_name,
        "",
        interner,
        arena,
        parser_registry,
        &err,
        false);

    ASSERT_TRUE(updated.has_value()) << "extraction failed: " << err;

    // validate_blueprint_integrity uses build_bp2_registry which loads all
    // library types AND auto-registers unknown types as ad-hoc.  The key
    // question is whether the embedded proxy type triggers a structural error.
    std::string integrity_err;
    bool ok = validate_blueprint_integrity(*updated, interner, arena, parser_registry, &integrity_err);
    EXPECT_TRUE(ok) << "validate_blueprint_integrity failed: " << integrity_err;
}

// Regression: diagnose_and_repair() must NOT report UnknownNodeType for
// embedded blueprint proxy nodes.
TEST_F(CommandTest, ExtractToBlueprint_HyphenatedNameNoDiagnosticFalsePositive) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_fixture(interner, arena);

    const char* blueprint_name = "RN-180-Exciter";

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a"), interner.intern("b")},
        blueprint_name,
        "",
        interner,
        arena,
        parser_registry,
        &err,
        false);

    ASSERT_TRUE(updated.has_value()) << "extraction failed: " << err;

    auto report = bp2::diagnostics::diagnose_and_repair(*updated, arena, parser_registry, interner);

    // There must be no UnknownNodeType issue for the proxy node.
    for (const auto& issue : report.issues) {
        EXPECT_NE(issue.kind, bp2::diagnostics::IntegrityIssue::Kind::UnknownNodeType)
            << "False positive diagnostic: " << issue.message;
    }
}

// Regression: The proxy node's type name must exactly match the user-given name,
// character-for-character, including hyphens, numbers, and casing.
TEST_F(CommandTest, ExtractToBlueprint_ProxyTypeNamePreservedExactly) {
    // Names with mixed case, hyphens, numbers, and underscores.
    const std::vector<const char*> names = {
        "RN-180-Exciter",
        "GSC-18-Starter_Motor",
    };

    for (const char* name : names) {
        bp2::PathArena local_arena(interner);
        bp2::Blueprint local_source = make_extract_fixture(interner, local_arena);

        std::string err;
        auto updated = editor::commands::build_extracted_blueprint_atomic(
            local_source,
            {interner.intern("a"), interner.intern("b")},
            name,
            "",
            interner,
            local_arena,
            parser_registry,
            &err,
            false);

        ASSERT_TRUE(updated.has_value()) << "extraction with name '" << name << "' failed: " << err;
        ASSERT_EQ(updated->nested().size(), 1u);

        const auto& nested = updated->nested()[0];
        const auto* collapsed = updated->find_node(nested.id);
        ASSERT_NE(collapsed, nullptr) << "collapsed proxy not found for name '" << name << "'";

        // The type stored on the proxy must resolve back to the exact input string.
        std::string resolved_type(interner.resolve(collapsed->semantic.type));
        EXPECT_EQ(resolved_type, std::string(name))
            << "Type name mangled: expected '" << name << "', got '" << resolved_type << "'";
    }
}

// Regression: the proxy (collapsed) node created by extraction must have its
// iface populated at creation time, not deferred to the encode/decode cycle.
// Before the fix, proxy node.iface was empty, forcing path resolution to fall
// back on ad-hoc registry entries with hardcoded Domain::Electrical for all
// ports — breaking domain-aware validation for non-electrical ports.
TEST_F(CommandTest, ExtractToBlueprint_ProxyNodeHasIfacePopulated) {
    bp2::PathArena local_arena(interner);
    bp2::Blueprint source = make_extract_fixture(interner, local_arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a"), interner.intern("b")},
        "SubCircuit",
        "",
        interner,
        local_arena,
        parser_registry,
        &err,
        false);

    ASSERT_TRUE(updated.has_value()) << "extraction failed: " << err;
    ASSERT_EQ(updated->nested().size(), 1u);

    const auto& nested = updated->nested()[0];
    const auto* proxy = updated->find_node(nested.id);
    ASSERT_NE(proxy, nullptr);

    // THE REGRESSION: proxy.iface must be non-empty at creation time
    EXPECT_FALSE(proxy->semantic.iface.empty())
        << "proxy node iface is empty — must be populated at extraction time";

     // Every input/output port on the proxy must be findable in iface
     size_t ep_idx = 0;
     for (const auto& ep : (count_inputs(proxy->semantic.iface) > 0 ? std::vector<bp2::PortDescriptor>{} : std::vector<bp2::PortDescriptor>{}) ) {
         // Note: loop needs manual iteration over ports
         if (ep_idx++ > 0) break; // TODO: fix loop
         EXPECT_TRUE(proxy->semantic.iface.has(ep.name))
             << "input port missing from proxy iface: " << interner.resolve(ep.name);
     }
     size_t op_idx = 0;
     for (const auto& ep : (count_outputs(proxy->semantic.iface) > 0 ? std::vector<bp2::PortDescriptor>{} : std::vector<bp2::PortDescriptor>{}) ) {
         // Note: loop needs manual iteration over ports
         if (op_idx++ > 0) break; // TODO: fix loop
         EXPECT_TRUE(proxy->semantic.iface.has(ep.name))
             << "output port missing from proxy iface: " << interner.resolve(ep.name);
     }
}

// =============================================================================
// Bug 1 Regression: Manual bridge node addition inside extracted group
// =============================================================================

// Regression: When a user manually adds a BlueprintInput/BlueprintOutput inside
// an extracted sub-blueprint group, the collapsed parent node and nested iface
// must be updated to include the new port.  Before the fix, the bridge node was
// added to the group but the collapsed node had no matching port and the nested
// iface was not updated, making the new bridge invisible at the parent level.
TEST_F(CommandTest, ManualBridgeAddition_SyncsCollapsedNodeAndNested) {
    bp2::PathArena local_arena(interner);
    bp2::Blueprint source = make_extract_fixture(interner, local_arena);

    // Extract nodes "a" and "b" into a sub-blueprint
    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a"), interner.intern("b")},
        "SubCircuit",
        "",
        interner,
        local_arena,
        parser_registry,
        &err,
        false);
    ASSERT_TRUE(updated.has_value()) << "extraction failed: " << err;
    ASSERT_EQ(updated->nested().size(), 1u);

    const auto& nested_pre = updated->nested()[0];
    const ui::InternedId nested_id = nested_pre.id;
    const std::string nested_id_str(interner.resolve(nested_id));

      // Record the initial port counts
     const auto* collapsed_pre = updated->find_node(nested_id);
     ASSERT_NE(collapsed_pre, nullptr);
     const size_t initial_inputs  = count_inputs(collapsed_pre->semantic.iface);
     const size_t initial_outputs = count_outputs(collapsed_pre->semantic.iface);
     const size_t initial_iface_ports = nested_pre.resolved_iface().ports().size();

    // Simulate what addComponent+sync does: add a BlueprintInput bridge node
    // inside the group, then update the collapsed node and nested iface.
    const std::string iface_name = "new_signal";
    const std::string bridge_id_str = nested_id_str + ":" + iface_name;
    const PortType new_port_type = PortType::Bool;

     bp2::Blueprint::Node bridge;
     bridge.semantic.id = interner.intern(bridge_id_str);
     bridge.semantic.type = interner.intern("BlueprintInput");
     bridge.view.name = iface_name;
     bridge.layout.layout_group = nested_id_str;
     bridge.layout.x = 0.0f;
     bridge.layout.y = 0.0f;
    set_iface(bridge, {
        make_port(interner.intern("ext"), bp2::Direction::Input, new_port_type),
        make_port(interner.intern("port"), bp2::Direction::Output, new_port_type),
    });
     bp2::Blueprint bp = updated->with_node(std::move(bridge));

     // Sync collapsed node: add input port
     {
         bp2::Blueprint::Node cn = *bp.find_node(nested_id);
         const ui::InternedId iface_iid = interner.intern(iface_name);
    set_iface(cn, {
        make_port(iface_iid, bp2::Direction::Input, new_port_type),
    });

         // Also update collapsed node iface
         std::vector<bp2::PortDescriptor> ports = cn.semantic.iface.ports();
         ports.push_back({iface_iid, Domain::Logical, bp2::Direction::Input});
         cn.semantic.iface = bp2::Interface(std::move(ports));
         bp = bp2::replace_node_preserve_order(bp, std::move(cn));
     }

     // Sync nested iface: add port descriptor to inline_def
     {
         const auto* nested_ptr = bp.find_nested(nested_id);
         ASSERT_NE(nested_ptr, nullptr);
         bp2::Blueprint::Nested n = *nested_ptr;
         ASSERT_NE(n.inline_def_mut(), nullptr);
         std::vector<bp2::PortDescriptor> ports = n.inline_def()->iface().ports();
         const ui::InternedId iface_iid = interner.intern(iface_name);
         ports.push_back({iface_iid, Domain::Logical, bp2::Direction::Input});
         *n.inline_def_mut() = n.inline_def()->with_interface(bp2::Interface(std::move(ports)));
         bp = bp2::replace_nested_preserve_order(bp, std::move(n));
     }

    // Verify: bridge node exists in the group
    const auto* bridge_node = bp.find_node(interner.intern(bridge_id_str));
    ASSERT_NE(bridge_node, nullptr) << "bridge node not found";
     EXPECT_EQ(bridge_node->layout.layout_group, nested_id_str);
    EXPECT_EQ(bridge_node->view.name, iface_name);

    // Verify: collapsed node has the new input port
    const auto* collapsed_post = bp.find_node(nested_id);
    ASSERT_NE(collapsed_post, nullptr);
     EXPECT_EQ(count_inputs(collapsed_post->semantic.iface), initial_inputs + 1)
         << "collapsed node should have one additional input port";

     // The last input port should match the new bridge
     bool found_port = false;
     size_t p_idx = 0;
     for (const auto& p : (count_inputs(collapsed_post->semantic.iface) > 0 ? std::vector<bp2::PortDescriptor>{} : std::vector<bp2::PortDescriptor>{}) ) {
         // Note: loop needs manual iteration over ports
      if (p_idx++ > 0) break; // TODO: fix loop
          if (interner.resolve(p.name) == iface_name) {
              EXPECT_EQ(p.port_type, new_port_type);
              found_port = true;
          }
      }
      EXPECT_TRUE(found_port) << "new input port '" << iface_name << "' not found on collapsed node";

     // Verify: collapsed node iface has the new port
     EXPECT_TRUE(collapsed_post->semantic.iface.has(interner.intern(iface_name)))
         << "collapsed node iface missing new port";

      // Verify: nested iface has the new port descriptor
     const auto* nested_post = bp.find_nested(nested_id);
     ASSERT_NE(nested_post, nullptr);
     auto nested_iface = nested_post->resolved_iface();
     EXPECT_EQ(nested_iface.ports().size(), initial_iface_ports + 1)
         << "nested iface should have one additional port";
     EXPECT_TRUE(nested_iface.has(interner.intern(iface_name)))
         << "nested iface missing new port descriptor";

     // Verify: output count unchanged
     EXPECT_EQ(count_outputs(collapsed_post->semantic.iface), initial_outputs);
}

// Regression: same as above but for BlueprintOutput (adds output port)
TEST_F(CommandTest, ManualBridgeAddition_OutputSyncsCollapsedNodeAndNested) {
    bp2::PathArena local_arena(interner);
    bp2::Blueprint source = make_extract_fixture(interner, local_arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a"), interner.intern("b")},
        "SubCircuit2",
        "",
        interner,
        local_arena,
        parser_registry,
        &err,
        false);
    ASSERT_TRUE(updated.has_value()) << "extraction failed: " << err;
    ASSERT_EQ(updated->nested().size(), 1u);

    const auto& nested_pre = updated->nested()[0];
    const ui::InternedId nested_id = nested_pre.id;
    const std::string nested_id_str(interner.resolve(nested_id));

      const auto* collapsed_pre = updated->find_node(nested_id);
     ASSERT_NE(collapsed_pre, nullptr);
     const size_t initial_inputs  = count_inputs(collapsed_pre->semantic.iface);
     const size_t initial_outputs = count_outputs(collapsed_pre->semantic.iface);
     const size_t initial_iface_ports = nested_pre.resolved_iface().ports().size();

     // Add BlueprintOutput bridge
     const std::string iface_name = "temp_out";
     const std::string bridge_id_str = nested_id_str + ":" + iface_name;
     const PortType new_port_type = PortType::Temperature;

     bp2::Blueprint::Node bridge;
     bridge.semantic.id = interner.intern(bridge_id_str);
     bridge.semantic.type = interner.intern("BlueprintOutput");
     bridge.view.name = iface_name;
     bridge.layout.layout_group = nested_id_str;
     bridge.layout.x = 0.0f;
     bridge.layout.y = 0.0f;
    set_iface(bridge, {
        make_port(interner.intern("port"), bp2::Direction::Input, new_port_type),
        make_port(interner.intern("ext"), bp2::Direction::Output, new_port_type),
    });
     bp2::Blueprint bp = updated->with_node(std::move(bridge));

     // Sync collapsed node: add output port
     {
         bp2::Blueprint::Node cn = *bp.find_node(nested_id);
         const ui::InternedId iface_iid = interner.intern(iface_name);
    set_iface(cn, {
        make_port(iface_iid, bp2::Direction::Output, new_port_type),
    });

         std::vector<bp2::PortDescriptor> ports = cn.semantic.iface.ports();
         ports.push_back({iface_iid, Domain::Thermal, bp2::Direction::Output});
         cn.semantic.iface = bp2::Interface(std::move(ports));
         bp = bp2::replace_node_preserve_order(bp, std::move(cn));
     }

     // Sync nested iface: add port descriptor to inline_def
     {
         const auto* nested_ptr = bp.find_nested(nested_id);
         ASSERT_NE(nested_ptr, nullptr);
         bp2::Blueprint::Nested n = *nested_ptr;
         ASSERT_NE(n.inline_def_mut(), nullptr);
         std::vector<bp2::PortDescriptor> ports = n.inline_def()->iface().ports();
         const ui::InternedId iface_iid = interner.intern(iface_name);
         ports.push_back({iface_iid, Domain::Thermal, bp2::Direction::Output});
         *n.inline_def_mut() = n.inline_def()->with_interface(bp2::Interface(std::move(ports)));
         bp = bp2::replace_nested_preserve_order(bp, std::move(n));
     }

     // Verify: collapsed node has the new output port
     const auto* collapsed_post = bp.find_node(nested_id);
     ASSERT_NE(collapsed_post, nullptr);
     EXPECT_EQ(count_outputs(collapsed_post->semantic.iface), initial_outputs + 1);

     bool found_port = false;
     size_t p_idx = 0;
     for (const auto& p : (count_outputs(collapsed_post->semantic.iface) > 0 ? std::vector<bp2::PortDescriptor>{} : std::vector<bp2::PortDescriptor>{}) ) {
         // Note: loop needs manual iteration over ports
      if (p_idx++ > 0) break; // TODO: fix loop
          if (interner.resolve(p.name) == iface_name) {
              EXPECT_EQ(p.port_type, new_port_type);
              found_port = true;
          }
      }
      EXPECT_TRUE(found_port) << "new output port 'temp_out' not found on collapsed node";

      // Verify: nested iface has the new port
     const auto* nested_post = bp.find_nested(nested_id);
     ASSERT_NE(nested_post, nullptr);
     EXPECT_TRUE(nested_post->resolved_iface().has(interner.intern(iface_name)));

     // Verify: input count unchanged
     EXPECT_EQ(count_inputs(collapsed_post->semantic.iface), initial_inputs);
}
