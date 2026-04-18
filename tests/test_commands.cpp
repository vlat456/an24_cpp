#include <gtest/gtest.h>
#include <set>

#include "editor/commands/commands.h"
#include "editor/commands/extract_blueprint.h"
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

#include "bp2_test_helpers.h"

static bp2::Blueprint::Node make_node(ui::StringInterner& I,
                                      const char* id,
                                      float x = 0.0f,
                                      float y = 0.0f) {
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

static bp2::Blueprint::Wire make_wire(ui::StringInterner& I,
                                      bp2::PathArena& arena,
                                      const char* wire_id,
                                      const char* src_node,
                                      const char* src_port,
                                      const char* dst_node,
                                      const char* dst_port) {
    (void)arena;
    bp2::Blueprint::Wire w;
    w.id = I.intern(wire_id);
    w.source = bp2::WireEndpoint{I.intern(src_node), I.intern(src_port)};
    w.target = bp2::WireEndpoint{I.intern(dst_node), I.intern(dst_port)};
    return w;
}

static bp2::Blueprint make_extract_fixture_node_owned(ui::StringInterner& I, bp2::PathArena& arena) {
    bp2::Blueprint bp;
    bp = bp.with_id(I.intern("bp_extract"));
    bp = bp.with_name("ExtractFixture");

    auto ext_in = make_node(I, "ext_in");
    ext_in.semantic.type = I.intern("NodeExtIn");
    set_iface(ext_in, {make_port(I.intern("out"), bp2::Direction::Output, PortType::V)});

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
    set_iface(ext_out, {make_port(I.intern("in"), bp2::Direction::Input, PortType::V)});

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

static bp2::Blueprint make_extract_with_bridge_node_fixture_node_owned(ui::StringInterner& I, bp2::PathArena& arena) {
    bp2::Blueprint bp;
    bp = bp.with_id(I.intern("bp_extract_bridge_node"));
    bp = bp.with_name("ExtractBridgeNode");

    auto bridge = make_node(I, "bridge_in");
    bridge.semantic.type = I.intern("BridgePort");
    bridge.content = bp2::Blueprint::Node::BridgePortData{
        I.intern("bridge_in"),
        bp2::Blueprint::Node::BridgePortSide::Input,
        PortType::V,
        bp2::Interface({
            make_port(I.intern("ext"), bp2::Direction::Input, PortType::V),
            make_port(I.intern("port"), bp2::Direction::Output, PortType::V),
        })
    };

    auto a = make_node(I, "a");
    a.semantic.type = I.intern("NodeA");
    set_iface(a, {
        make_port(I.intern("in"), bp2::Direction::Input, PortType::V),
        make_port(I.intern("out"), bp2::Direction::Output, PortType::V),
    });

    auto ext = make_node(I, "ext");
    ext.semantic.type = I.intern("Sink");
    set_iface(ext, {make_port(I.intern("in"), bp2::Direction::Input, PortType::V)});

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

static bp2::Blueprint make_extract_typed_boundary_fixture_node_owned(ui::StringInterner& I, bp2::PathArena& arena) {
    bp2::Blueprint bp;
    bp = bp.with_id(I.intern("bp_extract_typed_boundary"));
    bp = bp.with_name("ExtractTypedBoundary");

    auto ext_in = make_node(I, "ext_in");
    ext_in.semantic.type = I.intern("TypedExtIn");
    set_iface(ext_in, {make_port(I.intern("out"), bp2::Direction::Output, PortType::I)});

    auto a = make_node(I, "a");
    a.semantic.type = I.intern("TypedA");
    set_iface(a, {
        make_port(I.intern("in"), bp2::Direction::Input, PortType::I),
        make_port(I.intern("out"), bp2::Direction::Output, PortType::I),
    });

    auto b = make_node(I, "b");
    b.semantic.type = I.intern("TypedB");
    set_iface(b, {
        make_port(I.intern("in"), bp2::Direction::Input, PortType::I),
        make_port(I.intern("out"), bp2::Direction::Output, PortType::I),
    });

    auto ext_out = make_node(I, "ext_out");
    ext_out.semantic.type = I.intern("TypedExtOut");
    set_iface(ext_out, {make_port(I.intern("in"), bp2::Direction::Input, PortType::I)});

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

static bp2::Blueprint make_extract_iface_collision_fixture_node_owned(ui::StringInterner& I, bp2::PathArena& arena) {
    bp2::Blueprint bp;
    bp = bp.with_id(I.intern("bp_extract_collision"));
    bp = bp.with_name("ExtractIfaceCollision");

    auto ext_in = make_node(I, "ext_in");
    ext_in.semantic.type = I.intern("Source");
    set_iface(ext_in, {make_port(I.intern("out"), bp2::Direction::Output, PortType::V)});

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
    set_iface(ext_out, {make_port(I.intern("in"), bp2::Direction::Input, PortType::V)});

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

static bp2::Blueprint make_extract_fixture_with_existing_blueprint_name_node_owned(ui::StringInterner& I,
                                                                                   bp2::PathArena& arena) {
    bp2::Blueprint bp = make_extract_fixture_node_owned(I, arena);

    bp2::Blueprint::Node existing;
    existing.semantic.id = I.intern("existing_inst");
    existing.semantic.type = I.intern("extracted_blueprint_1");
    existing.view.name = "existing_inst";
    existing.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        I.intern("extracted_blueprint_1"),
        std::make_unique<bp2::Blueprint>(bp2::Blueprint{}
            .with_id(I.intern("extracted_blueprint_1"))
            .with_name("extracted_blueprint_1")
            .with_interface(bp2::Interface{})))
    };
    bp = bp.with_node(std::move(existing));
    return bp;
}

class ExtractToBlueprintNodeOwnedTest : public ::testing::Test {
protected:
    ui::StringInterner interner;
    TypeRegistry parser_registry = make_command_test_registry();
};

TEST_F(ExtractToBlueprintNodeOwnedTest, BasicAtomicCreatesCollapsedEmbeddedNode) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_fixture_node_owned(interner, arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a"), interner.intern("b")},
        "extracted_blueprint_1",
        WindowScopeId::root(),
        interner,
        arena,
        parser_registry,
        &err,
        false);

    ASSERT_TRUE(updated.has_value()) << err;
    EXPECT_EQ(updated->find_node(interner.intern("a")), nullptr);
    EXPECT_EQ(updated->find_node(interner.intern("b")), nullptr);

    const auto* collapsed = updated->find_blueprint_instance(interner.lookup("extract_inst_1"));
    ASSERT_NE(collapsed, nullptr);
    ASSERT_TRUE(collapsed->has_embedded_blueprint());
    ASSERT_NE(collapsed->blueprint_instance().source.inline_def(), nullptr);
    EXPECT_EQ(std::string(interner.resolve(collapsed->blueprint_instance().source.blueprint_id())), "extracted_blueprint_1");

    const bp2::Blueprint& inner = *collapsed->blueprint_instance().source.inline_def();
    EXPECT_NE(inner.find_node(interner.intern("a")), nullptr);
    EXPECT_NE(inner.find_node(interner.intern("b")), nullptr);
    EXPECT_EQ(inner.iface().size(), 2u);
    EXPECT_TRUE(inner.iface().has(interner.intern("in")));
    EXPECT_TRUE(inner.iface().has(interner.intern("out")));

    EXPECT_NE(updated->find_node(interner.intern("extract_inst_1:in")), nullptr);
    EXPECT_NE(updated->find_node(interner.intern("extract_inst_1:out")), nullptr);
}

TEST_F(ExtractToBlueprintNodeOwnedTest, PreviewReportsBasicBoundaryShape) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_fixture_node_owned(interner, arena);

    std::string err;
    auto preview = editor::commands::build_extract_to_blueprint_preview(
        source,
        {interner.intern("a"), interner.intern("b")},
        "extracted_blueprint_1",
        WindowScopeId::root(),
        interner,
        arena,
        &err,
        false);

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

TEST_F(ExtractToBlueprintNodeOwnedTest, RejectsBridgeNodeSelection) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_with_bridge_node_fixture_node_owned(interner, arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("bridge_in"), interner.intern("a")},
        "extracted_blueprint_1",
        WindowScopeId::root(),
        interner,
        arena,
        parser_registry,
        &err,
        false);

    EXPECT_FALSE(updated.has_value());
    EXPECT_NE(err.find("bridge port nodes"), std::string::npos);
}

TEST_F(ExtractToBlueprintNodeOwnedTest, RejectsSmallSelection) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_fixture_node_owned(interner, arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a")},
        "extracted_blueprint_1",
        WindowScopeId::root(),
        interner,
        arena,
        parser_registry,
        &err,
        false);

    EXPECT_FALSE(updated.has_value());
    EXPECT_NE(err.find("at least 2"), std::string::npos);
}

TEST_F(ExtractToBlueprintNodeOwnedTest, PreservesTypedBoundaryPorts) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_typed_boundary_fixture_node_owned(interner, arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a"), interner.intern("b")},
        "extracted_blueprint_1",
        WindowScopeId::root(),
        interner,
        arena,
        parser_registry,
        &err,
        false);

    ASSERT_TRUE(updated.has_value()) << err;
    const auto* collapsed = updated->find_blueprint_instance(interner.lookup("extract_inst_1"));
    ASSERT_NE(collapsed, nullptr);
    auto collapsed_iface = updated->effective_node_iface(*collapsed);
    ASSERT_EQ(count_inputs(collapsed_iface), 1u);
    ASSERT_EQ(count_outputs(collapsed_iface), 1u);
    EXPECT_EQ(get_input_type(collapsed_iface, 0), PortType::I);
    EXPECT_EQ(get_output_type(collapsed_iface, 0), PortType::I);

    const bp2::Blueprint& inner = *collapsed->blueprint_instance().source.inline_def();
    const bp2::Blueprint::Node* bp_in_node = nullptr;
    const bp2::Blueprint::Node* bp_out_node = nullptr;
    for (const auto& n : inner.nodes()) {
        if (n.is_bridge_port() && n.bridge_port().side == bp2::Blueprint::Node::BridgePortSide::Input) {
            bp_in_node = &n;
        }
        if (n.is_bridge_port() && n.bridge_port().side == bp2::Blueprint::Node::BridgePortSide::Output) {
            bp_out_node = &n;
        }
    }
    ASSERT_NE(bp_in_node, nullptr);
    ASSERT_NE(bp_out_node, nullptr);
    EXPECT_EQ(get_input_type(inner.effective_node_iface(*bp_in_node), 0), PortType::I);
    EXPECT_EQ(get_output_type(inner.effective_node_iface(*bp_in_node), 0), PortType::I);
    EXPECT_EQ(get_input_type(inner.effective_node_iface(*bp_out_node), 0), PortType::I);
    EXPECT_EQ(get_output_type(inner.effective_node_iface(*bp_out_node), 0), PortType::I);
}

TEST_F(ExtractToBlueprintNodeOwnedTest, PreviewRejectsEmptyName) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_fixture_node_owned(interner, arena);

    std::string err;
    auto preview = editor::commands::build_extract_to_blueprint_preview(
        source,
        {interner.intern("a"), interner.intern("b")},
        "",
        WindowScopeId::root(),
        interner,
        arena,
        &err,
        false);

    EXPECT_FALSE(preview.has_value());
    EXPECT_NE(err.find("non-empty"), std::string::npos);
}

TEST_F(ExtractToBlueprintNodeOwnedTest, PreviewRejectsDuplicateBlueprintName) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_fixture_with_existing_blueprint_name_node_owned(interner, arena);

    std::string err;
    auto preview = editor::commands::build_extract_to_blueprint_preview(
        source,
        {interner.intern("a"), interner.intern("b")},
        "extracted_blueprint_1",
        WindowScopeId::root(),
        interner,
        arena,
        &err,
        false);

    EXPECT_FALSE(preview.has_value());
    EXPECT_NE(err.find("already exists"), std::string::npos);
}

TEST_F(ExtractToBlueprintNodeOwnedTest, PreviewReportsIfaceCollision) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_iface_collision_fixture_node_owned(interner, arena);

    std::string err;
    auto preview = editor::commands::build_extract_to_blueprint_preview(
        source,
        {interner.intern("a"), interner.intern("b")},
        "extracted_blueprint_1",
        WindowScopeId::root(),
        interner,
        arena,
        &err,
        false);

    ASSERT_TRUE(preview.has_value()) << err;
    ASSERT_EQ(preview->iface_collision_names.size(), 1u);
    EXPECT_EQ(preview->iface_collision_names[0], "sig");
}

TEST_F(ExtractToBlueprintNodeOwnedTest, AtomicRejectsIfaceCollision) {
    bp2::PathArena arena(interner);
    bp2::Blueprint source = make_extract_iface_collision_fixture_node_owned(interner, arena);

    std::string err;
    auto updated = editor::commands::build_extracted_blueprint_atomic(
        source,
        {interner.intern("a"), interner.intern("b")},
        "extracted_blueprint_1",
        WindowScopeId::root(),
        interner,
        arena,
        parser_registry,
        &err,
        false);

    EXPECT_FALSE(updated.has_value());
    EXPECT_NE(err.find("collision"), std::string::npos);
}

TEST_F(ExtractToBlueprintNodeOwnedTest, AtomicEncodingIsDeterministic) {
    auto run_once = [this]() {
        ui::StringInterner local_interner;
        bp2::PathArena local_arena(local_interner);
        bp2::Blueprint source = make_extract_fixture_node_owned(local_interner, local_arena);

        std::string err;
        auto updated = editor::commands::build_extracted_blueprint_atomic(
            source,
            {local_interner.intern("a"), local_interner.intern("b")},
            "extracted_blueprint_1",
            WindowScopeId::root(),
            local_interner,
            local_arena,
            parser_registry,
            &err,
            false);

        EXPECT_TRUE(updated.has_value()) << err;
        if (!updated.has_value()) {
            return std::string();
        }

        const auto* collapsed = updated->find_blueprint_instance(local_interner.lookup("extract_inst_1"));
        EXPECT_NE(collapsed, nullptr);
        if (!collapsed || !collapsed->has_embedded_blueprint() || !collapsed->blueprint_instance().source.inline_def()) {
            return std::string();
        }

        const auto& inner = *collapsed->blueprint_instance().source.inline_def();
        std::vector<std::string> iface_names;
        for (const auto& port : inner.iface().ports()) {
            iface_names.push_back(std::string(local_interner.resolve(port.name)));
        }
        std::sort(iface_names.begin(), iface_names.end());

        std::string summary;
        summary += std::string(local_interner.resolve(collapsed->blueprint_instance().source.blueprint_id()));
        summary += "|nodes=" + std::to_string(inner.nodes().size());
        summary += "|wires=" + std::to_string(inner.wires().size());
        summary += "|iface=";
        for (const auto& name : iface_names) {
            summary += name + ",";
        }
        summary += "|root_wires=" + std::to_string(updated->wires().size());
        return summary;
    };

    EXPECT_EQ(run_once(), run_once());
}
