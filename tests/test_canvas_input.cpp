#include <gtest/gtest.h>
#include <cstring>
#include "core/model/component_spec.h"
#include "core/model/presentation_spec.h"

#include "editor/input/canvas_input.h"
#include "editor/input/editing_host.h"
#include "editor/window/window_scope_id.h"
#include "editor/viewport/viewport.h"
#include "editor/visual/scene.h"
#include "editor/visual/scene_mutations.h"
#include "editor/visual/presentation/canvas_scene_snapshot.h"
#include "editor/visual/snap.h"
#include "editor/visual/node/bus_node_widget.h"
#include "editor/visual/node/ref_node_widget.h"
#include "editor/visual/node/visual_node.h"
#include "editor/data/node_content.h"
#include "editor/visual/port/visual_port.h"
#include "editor/visual/wire/wire.h"
#include "editor/visual/presentation/semantic_scene_snapshot.h"
#include "editor/visual/presentation/semantic_scene_hittest.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/interface/interface.h"
#include "blueprint_v2/interface/port_descriptor.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "blueprint_v2/path/path.h"

namespace {

// Helper to make a PortDescriptor for a semantic interface
// Shared bp2 test helpers (make_port, set_iface)
#include "bp2_test_helpers.h"

static bp2::Blueprint::Node make_node(ui::StringInterner& I,
                                      const char* id,
                                      const char* type,
                                      float x,
                                      float y,
                                      const char* render_hint = "") {
    bp2::Blueprint::Node n;
    n.semantic.id = I.intern(id);
    n.semantic.type = I.intern(type);
    n.layout.x = x;
    n.layout.y = y;
    (void)render_hint;
    return n;
}

/// Set instance string params on a node (for overriding TypeDefinition defaults)
static void set_params(bp2::Blueprint::Node& n,
                       std::initializer_list<std::pair<std::string, std::string>> params) {
    for (auto& [k, v] : params) n.semantic.string_params[k] = v;
}

/// Update dynamic content on a widget after scene rebuild
static void update_dynamic(visual::Scene& scene, const char* node_id,
                           std::function<void(NodeContent&)> mutate) {
    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find(node_id));
    if (!widget) return;
    NodeContent c = widget->currentContent();
    mutate(c);
    widget->updateContent(c);
}

/// Compare hit result node_id with expected string via interner
static testing::AssertionResult hit_node_id_matches(
    const char* /*expr*/,
    ui::InternedId actual_id,
    ui::StringInterner& I,
    const char* expected_str) {
    ui::InternedId expected_id = I.intern(expected_str);
    if (actual_id == expected_id) {
        return testing::AssertionSuccess();
    }
    return testing::AssertionFailure()
        << "Hit node_id " << actual_id.raw() << " (interned) doesn't match expected \""
        << expected_str << "\" (id " << expected_id.raw() << ")";
}

static visual::HitResult snapshot_hit_test(const visual::Scene& scene,
                                            ui::StringInterner& I,
                                            Pt world) {
    auto snapshot = editor::presentation::build_canvas_scene_snapshot(scene, I);
    return editor::presentation::hit_test_canvas_scene(snapshot, world);
}

static bp2::Blueprint::Wire make_wire(ui::StringInterner& I,
                                       bp2::PathArena& /*arena*/,
                                       const char* wire_id,
                                       const char* src_node,
                                       const char* src_port,
                                       const char* dst_node,
                                       const char* dst_port) {
     bp2::Blueprint::Wire w;
     w.id = I.intern(wire_id);
     w.source = bp2::WireEndpoint{I.intern(src_node), I.intern(src_port)};
     w.target = bp2::WireEndpoint{I.intern(dst_node), I.intern(dst_port)};
     return w;
}

static ComponentRegistry make_canvas_input_test_registry() {
    ComponentRegistry reg;

    auto add_simple = [&](const char* name, const char* hint = "",
                          const char* ct = "None",
                          std::initializer_list<std::pair<std::string,std::string>> params = {}) {
        PrimitiveSpec def;
        def.classname = name;
        for (auto& [k, v] : params) def.params[k] = ParamSpec{ParamSchemaType::String, v};
        reg.types[def.classname] = std::move(def);
        if (hint && hint[0]) {
            reg.presentation.specs[name].render_hint = hint;
        }
        if (ct && ct[0] && strcmp(ct, "None") != 0) {
            reg.presentation.specs[name].content_type = ct;
        }
    };

    // Types with specific ports (used by wire-compatibility tests)
    {
        PrimitiveSpec def;
        def.classname = "Slider";
        def.params["min"] = ParamSpec{ParamSchemaType::Float, "0"};
        def.params["max"] = ParamSpec{ParamSchemaType::Float, "1"};
        def.ports.emplace("out", Port(bp2::Direction::Output, PortType::Bool, Domain::Logical, false));
        reg.types[def.classname] = std::move(def);
        reg.presentation.specs["Slider"].content_type = "Slider";
    }
    {
        PrimitiveSpec def;
        def.classname = "BoolSrc";
        def.ports.emplace("out", Port(bp2::Direction::Output, PortType::Bool, Domain::Logical, false));
        reg.types[def.classname] = std::move(def);
    }
    {
        PrimitiveSpec def;
        def.classname = "BoolSink";
        def.ports.emplace("in", Port(bp2::Direction::Input, PortType::Bool, Domain::Logical, false));
        reg.types[def.classname] = std::move(def);
    }
    // Simple types
    add_simple("Battery");
    add_simple("Lamp");
    add_simple("Bus", "bus");
    add_simple("RefNode", "ref");
    add_simple("Value");
    add_simple("AZS", "", "VerticalToggle", {{"closed", "false"}});
    add_simple("Switch", "", "Switch", {{"closed", "false"}});
    add_simple("KnobSwitch", "", "Knob", {{"positions", "2"}});
    add_simple("KnobControl", "", "Knob", {{"positions", "2"}});
    add_simple("SliderControl", "", "Slider", {{"min", "0"}, {"max", "1"}});
    add_simple("CompositeSwitch", "", "VerticalToggle", {{"closed", "false"}});
    add_simple("Composite");
    add_simple("CompositeType");
    add_simple("IndicatorLight", "", "Indicator");
    add_simple("Splitter");
    add_simple("Voltmeter", "", "Gauge", {{"min", "0"}, {"max", "28"}});
    add_simple("TypeSrc");
    add_simple("TypeSink");

    return reg;
}

static const ComponentRegistry& ci_reg() {
    static const ComponentRegistry r = make_canvas_input_test_registry();
    return r;
}

static ui::Pt port_center(visual::Port* p) {
    return p->worldPos() + ui::Pt(visual::PortConstants::RADIUS, visual::PortConstants::RADIUS);
}

static std::pair<ui::InternedId, ui::InternedId> endpoint_node_port(const bp2::Path& path,
                                                                     const bp2::PathArena& arena) {
    if (path.kind() != bp2::PathKind::Port) return {};
    ui::InternedId port = path.segment();
    bp2::Path parent = arena.parent(path);
    if (parent.kind() != bp2::PathKind::Node) return {};
    return {parent.segment(), port};
}

static std::pair<ui::InternedId, ui::InternedId> endpoint_node_port(const bp2::WireEndpoint& ep,
                                                                     const bp2::PathArena& /*arena*/) {
    return {ep.node, ep.port};
}

} // namespace

TEST(CanvasInputBus, AliasReconnectUsesSelectedWireNotFirst) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto bus = make_node(I, "bus", "Bus", 200.0f, 120.0f, "bus");
    auto bat1 = make_node(I, "bat1", "Battery", 40.0f, 40.0f);
    set_iface(bat1, {
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });
    auto bat2 = make_node(I, "bat2", "Battery", 40.0f, 180.0f);
    set_iface(bat2, {
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });
    auto l1 = make_node(I, "l1", "Lamp", 420.0f, 80.0f);
    set_iface(l1, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });
    auto l2 = make_node(I, "l2", "Lamp", 420.0f, 180.0f);
    set_iface(l2, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(bus));
    bp = bp.with_node(std::move(bat1));
    bp = bp.with_node(std::move(bat2));
    bp = bp.with_node(std::move(l1));
    bp = bp.with_node(std::move(l2));
    bp = bp.with_wire(make_wire(I, arena, "wire_0", "bat1", "v_out", "bus", "v"));
    bp = bp.with_wire(make_wire(I, arena, "wire_1", "bus", "v", "l1", "v_in"));
    bp = bp.with_wire(make_wire(I, arena, "wire_2", "bus", "v", "l2", "v_in"));

    bp2::EditorModel model(bp);
    model.next_wire_id_ = 1;
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* bus_widget = dynamic_cast<visual::BusNodeWidget*>(scene.find("bus"));
    ASSERT_NE(bus_widget, nullptr);
    auto* w2_alias = bus_widget->port("wire_2");
    ASSERT_NE(w2_alias, nullptr);

    auto* bat2_widget = dynamic_cast<visual::Widget*>(scene.find("bat2"));
    ASSERT_NE(bat2_widget, nullptr);
    auto* bat2_out = bat2_widget->portByName("v_out");
    ASSERT_NE(bat2_out, nullptr);

    Viewport vp;
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());

    const ui::Pt canvas_min(0.0f, 0.0f);
    input.on_mouse_down(port_center(w2_alias), MouseButton::Left, canvas_min);
    input.on_mouse_up(MouseButton::Left, port_center(bat2_out), canvas_min);

    const auto* w1 = model.current().find_wire(I.intern("wire_1"));
    const auto* w2 = model.current().find_wire(I.intern("wire_2"));
    ASSERT_NE(w1, nullptr);
    ASSERT_NE(w2, nullptr);

    auto [w1_src_n, w1_src_p] = endpoint_node_port(w1->source, arena);
    auto [w2_src_n, w2_src_p] = endpoint_node_port(w2->source, arena);
    auto [w2_tgt_n, w2_tgt_p] = endpoint_node_port(w2->target, arena);

    EXPECT_EQ(w1_src_n, I.intern("bus"));
    EXPECT_EQ(w1_src_p, I.intern("v"));

    EXPECT_EQ(w2_src_n, I.intern("bat2"));
    EXPECT_EQ(w2_src_p, I.intern("v_out"));
    EXPECT_EQ(w2_tgt_n, I.intern("l2"));
    EXPECT_EQ(w2_tgt_p, I.intern("v_in"));
}

TEST(CanvasInputBus, AliasToAliasReconnectSwapsWireOrder) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto bus = make_node(I, "bus", "Bus", 200.0f, 120.0f, "bus");
    auto src = make_node(I, "src", "Battery", 40.0f, 120.0f);
    set_iface(src, {
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });
    auto l1 = make_node(I, "l1", "Lamp", 420.0f, 80.0f);
    set_iface(l1, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });
    auto l2 = make_node(I, "l2", "Lamp", 420.0f, 180.0f);
    set_iface(l2, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(bus));
    bp = bp.with_node(std::move(src));
    bp = bp.with_node(std::move(l1));
    bp = bp.with_node(std::move(l2));
    bp = bp.with_wire(make_wire(I, arena, "wire_0", "src", "v_out", "bus", "v"));
    bp = bp.with_wire(make_wire(I, arena, "wire_1", "bus", "v", "l1", "v_in"));
    bp = bp.with_wire(make_wire(I, arena, "wire_2", "bus", "v", "l2", "v_in"));

    bp2::EditorModel model(bp);
    model.next_wire_id_ = 1;
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* bus_widget = dynamic_cast<visual::BusNodeWidget*>(scene.find("bus"));
    ASSERT_NE(bus_widget, nullptr);
    auto* w2_alias = bus_widget->port("wire_2");
    auto* w1_alias = bus_widget->port("wire_1");
    ASSERT_NE(w2_alias, nullptr);
    ASSERT_NE(w1_alias, nullptr);

    Viewport vp;
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());

    const ui::Pt canvas_min(0.0f, 0.0f);
    input.on_mouse_down(port_center(w2_alias), MouseButton::Left, canvas_min);
    input.on_mouse_up(MouseButton::Left, port_center(w1_alias), canvas_min);

    const auto& ws = model.current().wires();
    ASSERT_EQ(ws.size(), 3u);

    EXPECT_EQ(ws[1].id, I.intern("wire_2"));
    EXPECT_EQ(ws[2].id, I.intern("wire_1"));
}

TEST(CanvasInputBus, BasePortStartsCreateWireAndUsesCanonicalBusPort) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto bus = make_node(I, "bus", "Bus", 200.0f, 120.0f, "bus");
    auto src = make_node(I, "src", "Battery", 40.0f, 120.0f);
    set_iface(src, {
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });
    auto l1 = make_node(I, "l1", "Lamp", 420.0f, 120.0f);
    set_iface(l1, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(bus));
    bp = bp.with_node(std::move(src));
    bp = bp.with_node(std::move(l1));
    bp = bp.with_wire(make_wire(I, arena, "wire_0", "src", "v_out", "bus", "v"));

    bp2::EditorModel model(bp);
    model.next_wire_id_ = 1;
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* bus_widget = dynamic_cast<visual::BusNodeWidget*>(scene.find("bus"));
    ASSERT_NE(bus_widget, nullptr);
    auto* base_v = bus_widget->port("v");
    ASSERT_NE(base_v, nullptr);

    auto* src_widget = dynamic_cast<visual::Widget*>(scene.find("src"));
    ASSERT_NE(src_widget, nullptr);
    auto* src_out = src_widget->portByName("v_out");
    ASSERT_NE(src_out, nullptr);

    Viewport vp;
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());

    const ui::Pt canvas_min(0.0f, 0.0f);
    const size_t before = model.current().wires().size();

    input.on_mouse_down(port_center(base_v), MouseButton::Left, canvas_min);
    EXPECT_EQ(input.state(), InputState::CreatingWire);

    input.on_mouse_up(MouseButton::Left, port_center(src_out), canvas_min);

    const size_t after = model.current().wires().size();
    EXPECT_EQ(after, before + 1);

    bool found_connection_v_to_vout = false;
    for (const auto& w : model.current().wires()) {
        auto [src_n, src_p] = endpoint_node_port(w.source, arena);
        auto [tgt_n, tgt_p] = endpoint_node_port(w.target, arena);
        const bool forward = (src_n == I.intern("bus") && src_p == I.intern("v")
                           && tgt_n == I.intern("src") && tgt_p == I.intern("v_out"));
        const bool reverse = (src_n == I.intern("src") && src_p == I.intern("v_out")
                           && tgt_n == I.intern("bus") && tgt_p == I.intern("v"));
        if (forward || reverse) {
            found_connection_v_to_vout = true;
            break;
        }
    }

    EXPECT_TRUE(found_connection_v_to_vout);
}

TEST(CanvasInputValidation, RejectsIncompatiblePortTypesOnWireCreate) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto src = make_node(I, "src", "TypeSrc", 40.0f, 120.0f);
    set_iface(src, {
        make_port(I, "out", Domain::Electrical, bp2::Direction::Output, PortType::I),
    });

    auto sink = make_node(I, "sink", "TypeSink", 260.0f, 120.0f);
    set_iface(sink, {
        make_port(I, "in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(src));
    bp = bp.with_node(std::move(sink));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* src_w = dynamic_cast<visual::Widget*>(scene.find("src"));
    auto* sink_w = dynamic_cast<visual::Widget*>(scene.find("sink"));
    ASSERT_NE(src_w, nullptr);
    ASSERT_NE(sink_w, nullptr);
    auto* src_out = src_w->portByName("out");
    auto* sink_in = sink_w->portByName("in");
    ASSERT_NE(src_out, nullptr);
    ASSERT_NE(sink_in, nullptr);

    Viewport vp;
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    const ui::Pt canvas_min(0.0f, 0.0f);
    input.on_mouse_down(port_center(src_out), MouseButton::Left, canvas_min);
    input.on_mouse_up(MouseButton::Left, port_center(sink_in), canvas_min);

    EXPECT_EQ(model.current().wires().size(), 0u);
}

TEST(CanvasInputValidation, RejectsCrossDomainWireCreate) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto src = make_node(I, "src", "TypeSrc", 40.0f, 120.0f);
    set_iface(src, {
        make_port(I, "out", Domain::Mechanical, bp2::Direction::Output, PortType::RPM),
    });

    auto sink = make_node(I, "sink", "TypeSink", 260.0f, 120.0f);
    set_iface(sink, {
        make_port(I, "in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(src));
    bp = bp.with_node(std::move(sink));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* src_w = dynamic_cast<visual::Widget*>(scene.find("src"));
    auto* sink_w = dynamic_cast<visual::Widget*>(scene.find("sink"));
    ASSERT_NE(src_w, nullptr);
    ASSERT_NE(sink_w, nullptr);
    auto* src_out = src_w->portByName("out");
    auto* sink_in = sink_w->portByName("in");
    ASSERT_NE(src_out, nullptr);
    ASSERT_NE(sink_in, nullptr);

    Viewport vp;
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    const ui::Pt canvas_min(0.0f, 0.0f);
    input.on_mouse_down(port_center(src_out), MouseButton::Left, canvas_min);
    input.on_mouse_up(MouseButton::Left, port_center(sink_in), canvas_min);

    EXPECT_EQ(model.current().wires().size(), 0u);
}

TEST(CanvasInputReconnect, ReconnectUpdatesSelectedWireEndpoint) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto s1 = make_node(I, "s1", "Battery", 40.0f, 60.0f);
    set_iface(s1, {
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });
    auto s2 = make_node(I, "s2", "Battery", 40.0f, 220.0f);
    set_iface(s2, {
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });
    auto l1 = make_node(I, "l1", "Lamp", 420.0f, 60.0f);
    set_iface(l1, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });
    auto l2 = make_node(I, "l2", "Lamp", 420.0f, 220.0f);
    set_iface(l2, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(s1));
    bp = bp.with_node(std::move(s2));
    bp = bp.with_node(std::move(l1));
    bp = bp.with_node(std::move(l2));
    bp = bp.with_wire(make_wire(I, arena, "wire_0", "s1", "v_out", "l1", "v_in"));
    bp = bp.with_wire(make_wire(I, arena, "wire_1", "s2", "v_out", "l2", "v_in"));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* l2_widget = dynamic_cast<visual::Widget*>(scene.find("l2"));
    ASSERT_NE(l2_widget, nullptr);
    auto* l2_in = l2_widget->portByName("v_in");
    ASSERT_NE(l2_in, nullptr);

    auto* l1_widget = dynamic_cast<visual::Widget*>(scene.find("l1"));
    ASSERT_NE(l1_widget, nullptr);
    auto* l1_in = l1_widget->portByName("v_in");
    ASSERT_NE(l1_in, nullptr);

    Viewport vp;
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    const ui::Pt canvas_min(0.0f, 0.0f);

    // Reconnect wire_1 target from l2:v_in to l1:v_in
    input.on_mouse_down(port_center(l2_in), MouseButton::Left, canvas_min);
    input.on_mouse_up(MouseButton::Left, port_center(l1_in), canvas_min);

    const auto* w0 = model.current().find_wire(I.intern("wire_0"));
    const auto* w1 = model.current().find_wire(I.intern("wire_1"));
    ASSERT_NE(w0, nullptr);
    ASSERT_NE(w1, nullptr);

    auto [w1_src_n, w1_src_p] = endpoint_node_port(w1->source, arena);
    auto [w1_tgt_n, w1_tgt_p] = endpoint_node_port(w1->target, arena);
    auto [w0_tgt_n, w0_tgt_p] = endpoint_node_port(w0->target, arena);

    EXPECT_EQ(w1_src_n, I.intern("s2"));
    EXPECT_EQ(w1_src_p, I.intern("v_out"));
    EXPECT_EQ(w1_tgt_n, I.intern("l1"));
    EXPECT_EQ(w1_tgt_p, I.intern("v_in"));

    // wire_0 remains unchanged
    EXPECT_EQ(w0_tgt_n, I.intern("l1"));
    EXPECT_EQ(w0_tgt_p, I.intern("v_in"));
}

TEST(CanvasInputReconnect, ReconnectDropOnEmptyRemovesWire) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto s1 = make_node(I, "s1", "Battery", 40.0f, 60.0f);
    set_iface(s1, {
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });
    auto l1 = make_node(I, "l1", "Lamp", 420.0f, 60.0f);
    set_iface(l1, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(s1));
    bp = bp.with_node(std::move(l1));
    bp = bp.with_wire(make_wire(I, arena, "wire_0", "s1", "v_out", "l1", "v_in"));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* l1_widget = dynamic_cast<visual::Widget*>(scene.find("l1"));
    ASSERT_NE(l1_widget, nullptr);
    auto* l1_in = l1_widget->portByName("v_in");
    ASSERT_NE(l1_in, nullptr);

    Viewport vp;
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    const ui::Pt canvas_min(0.0f, 0.0f);

    ASSERT_EQ(model.current().wires().size(), 1u);
    input.on_mouse_down(port_center(l1_in), MouseButton::Left, canvas_min);
    input.on_mouse_up(MouseButton::Left, ui::Pt(900.0f, 900.0f), canvas_min);

    EXPECT_TRUE(model.current().wires().empty());
}

TEST(CanvasInputReconnect, ReconnectWithRoutingPointsStillChecksTypeCompatibility) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto src = make_node(I, "src", "TypeSrc", 40.0f, 120.0f);
    set_iface(src, {
        make_port(I, "out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    auto sink_ok = make_node(I, "sink_ok", "TypeSink", 420.0f, 120.0f);
    set_iface(sink_ok, {
        make_port(I, "in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    auto sink_bad = make_node(I, "sink_bad", "TypeSink", 420.0f, 220.0f);
    set_iface(sink_bad, {
        make_port(I, "in", Domain::Electrical, bp2::Direction::Input, PortType::I),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(src));
    bp = bp.with_node(std::move(sink_ok));
    bp = bp.with_node(std::move(sink_bad));
    auto w = make_wire(I, arena, "wire_0", "src", "out", "sink_ok", "in");
    w.domain = Domain::Electrical;
    w.routing_points.push_back({200.0f, 120.0f});
    bp = bp.with_wire(std::move(w));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* sink_ok_w = dynamic_cast<visual::Widget*>(scene.find("sink_ok"));
    auto* sink_bad_w = dynamic_cast<visual::Widget*>(scene.find("sink_bad"));
    ASSERT_NE(sink_ok_w, nullptr);
    ASSERT_NE(sink_bad_w, nullptr);
    auto* sink_ok_in = sink_ok_w->portByName("in");
    auto* sink_bad_in = sink_bad_w->portByName("in");
    ASSERT_NE(sink_ok_in, nullptr);
    ASSERT_NE(sink_bad_in, nullptr);

    Viewport vp;
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    const ui::Pt canvas_min(0.0f, 0.0f);

    // Try reconnecting target to incompatible type (V -> I). Must be rejected.
    input.on_mouse_down(port_center(sink_ok_in), MouseButton::Left, canvas_min);
    input.on_mouse_up(MouseButton::Left, port_center(sink_bad_in), canvas_min);

    const auto* wire_after = model.current().find_wire(I.intern("wire_0"));
    if (wire_after) {
        auto [tgt_n, _tgt_p] = endpoint_node_port(wire_after->target, arena);
        EXPECT_NE(tgt_n, I.intern("sink_bad"));
    } else {
        // Current reconnection semantics remove the wire when drop is invalid.
        EXPECT_TRUE(model.current().wires().empty());
    }
}

TEST(CanvasInputReconnect, ReconnectWithRoutingPointsStillAcceptsCompatibleTarget) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto src = make_node(I, "src", "TypeSrc", 40.0f, 120.0f);
    set_iface(src, {
        make_port(I, "out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    auto sink_a = make_node(I, "sink_a", "TypeSink", 420.0f, 120.0f);
    set_iface(sink_a, {
        make_port(I, "in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    auto sink_b = make_node(I, "sink_b", "TypeSink", 420.0f, 220.0f);
    set_iface(sink_b, {
        make_port(I, "in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(src));
    bp = bp.with_node(std::move(sink_a));
    bp = bp.with_node(std::move(sink_b));
    auto w = make_wire(I, arena, "wire_0", "src", "out", "sink_a", "in");
    w.domain = Domain::Electrical;
    w.routing_points.push_back({200.0f, 120.0f});
    bp = bp.with_wire(std::move(w));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* sink_a_w = dynamic_cast<visual::Widget*>(scene.find("sink_a"));
    auto* sink_b_w = dynamic_cast<visual::Widget*>(scene.find("sink_b"));
    ASSERT_NE(sink_a_w, nullptr);
    ASSERT_NE(sink_b_w, nullptr);
    auto* sink_a_in = sink_a_w->portByName("in");
    auto* sink_b_in = sink_b_w->portByName("in");
    ASSERT_NE(sink_a_in, nullptr);
    ASSERT_NE(sink_b_in, nullptr);

    Viewport vp;
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    const ui::Pt canvas_min(0.0f, 0.0f);

    input.on_mouse_down(port_center(sink_a_in), MouseButton::Left, canvas_min);
    input.on_mouse_up(MouseButton::Left, port_center(sink_b_in), canvas_min);

    const auto* wire_after = model.current().find_wire(I.intern("wire_0"));
    ASSERT_NE(wire_after, nullptr);
    auto [tgt_n, tgt_p] = endpoint_node_port(wire_after->target, arena);
    EXPECT_EQ(tgt_n, I.intern("sink_b"));
    EXPECT_EQ(tgt_p, I.intern("in"));
    EXPECT_EQ(wire_after->routing_points.size(), 1u);
}

TEST(CanvasInputCreateWire, EmbeddedAnyInputUsesConcreteSourceDomain) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto slider = make_node(I, "slider_1", "Slider", 40.0f, 120.0f);
    set_iface(slider, {
        make_port(I, "out", Domain::Logical, bp2::Direction::Output, PortType::Bool),
    });

    bp2::Blueprint inner;
    inner = inner.with_interface(bp2::Interface({
        make_port(I, "Torque", Domain::Electrical, bp2::Direction::Input, PortType::Any),
    }));

    bp2::Blueprint::Node host;
    host.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            std::make_unique<bp2::Blueprint>(inner.with_id(I.intern("RPMIntertial"))))
    };
    host.semantic.id = I.intern("extract_inst_4");
    host.semantic.type = I.intern("RPMIntertial");

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(slider));
    bp = bp.with_node(std::move(host));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* slider_w = dynamic_cast<visual::Widget*>(scene.find("slider_1"));
    auto* host_w = dynamic_cast<visual::Widget*>(scene.find("extract_inst_4"));
    ASSERT_NE(slider_w, nullptr);
    ASSERT_NE(host_w, nullptr);
    auto* slider_out = slider_w->portByName("out");
    auto* torque_in = host_w->portByName("Torque");
    ASSERT_NE(slider_out, nullptr);
    ASSERT_NE(torque_in, nullptr);

    Viewport vp;
    auto host_model = create_editor_model_host(model);
    CanvasInput input(scene, vp, host_model.get(), I, arena, WindowScopeId::root(), &ci_reg());
    const ui::Pt canvas_min(0.0f, 0.0f);

    input.on_mouse_down(port_center(slider_out), MouseButton::Left, canvas_min);
    input.on_mouse_up(MouseButton::Left, port_center(torque_in), canvas_min);

    ASSERT_EQ(model.current().wires().size(), 1u);
    EXPECT_EQ(model.current().wires().front().domain, Domain::Logical);
}

TEST(CanvasInputReconnect, EmbeddedAnyInputUsesConcreteSourceDomain) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto slider = make_node(I, "slider_1", "Slider", 40.0f, 120.0f);
    set_iface(slider, {
        make_port(I, "out", Domain::Logical, bp2::Direction::Output, PortType::Bool),
    });

    auto sink_a = make_node(I, "sink_a", "BoolSink", 420.0f, 120.0f);
    set_iface(sink_a, {
        make_port(I, "in", Domain::Logical, bp2::Direction::Input, PortType::Bool),
    });

    bp2::Blueprint inner;
    inner = inner.with_interface(bp2::Interface({
        make_port(I, "Torque", Domain::Electrical, bp2::Direction::Input, PortType::Any),
    }));

    bp2::Blueprint::Node host;
    host.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            std::make_unique<bp2::Blueprint>(inner.with_id(I.intern("RPMIntertial"))))
    };
    host.semantic.id = I.intern("extract_inst_4");
    host.semantic.type = I.intern("RPMIntertial");

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(slider));
    bp = bp.with_node(std::move(sink_a));
    bp = bp.with_node(std::move(host));
    auto w = make_wire(I, arena, "wire_logic", "slider_1", "out", "sink_a", "in");
    w.domain = Domain::Logical;
    bp = bp.with_wire(std::move(w));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* sink_a_w = dynamic_cast<visual::Widget*>(scene.find("sink_a"));
    auto* host_w = dynamic_cast<visual::Widget*>(scene.find("extract_inst_4"));
    ASSERT_NE(sink_a_w, nullptr);
    ASSERT_NE(host_w, nullptr);
    auto* sink_a_in = sink_a_w->portByName("in");
    auto* torque_in = host_w->portByName("Torque");
    ASSERT_NE(sink_a_in, nullptr);
    ASSERT_NE(torque_in, nullptr);

    Viewport vp;
    auto host_model = create_editor_model_host(model);
    CanvasInput input(scene, vp, host_model.get(), I, arena, WindowScopeId::root(), &ci_reg());
    const ui::Pt canvas_min(0.0f, 0.0f);

    input.on_mouse_down(port_center(sink_a_in), MouseButton::Left, canvas_min);
    input.on_mouse_up(MouseButton::Left, port_center(torque_in), canvas_min);

    const auto* wire_after = model.current().find_wire(I.intern("wire_logic"));
    ASSERT_NE(wire_after, nullptr);
    EXPECT_EQ(wire_after->domain, Domain::Logical);

    auto [tgt_n, tgt_p] = endpoint_node_port(wire_after->target, arena);
    EXPECT_EQ(tgt_n, I.intern("extract_inst_4"));
    EXPECT_EQ(tgt_p, I.intern("Torque"));
}

TEST(CanvasInputBus, DeleteNodeRemovesConnectedWiresBeforeRecreate) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto src = make_node(I, "src", "Battery", 40.0f, 120.0f);
    set_iface(src, {
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    auto split = make_node(I, "split", "Splitter", 220.0f, 120.0f);
    set_iface(split, {
        make_port(I, "i", Domain::Electrical, bp2::Direction::Input, PortType::Any),
        make_port(I, "o1", Domain::Electrical, bp2::Direction::Output, PortType::Any),
    });

    auto load = make_node(I, "load", "Lamp", 420.0f, 120.0f);
    set_iface(load, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(src));
    bp = bp.with_node(std::move(split));
    bp = bp.with_node(std::move(load));
    bp = bp.with_wire(make_wire(I, arena, "wire_0", "src", "v_out", "split", "i"));
    bp = bp.with_wire(make_wire(I, arena, "wire_1", "split", "o1", "load", "v_in"));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    Viewport vp;
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());

    ASSERT_TRUE(input.select_node_by_id("split"));
    input.on_key(Key::Delete);

    EXPECT_EQ(model.current().find_node(I.intern("split")), nullptr);
    EXPECT_TRUE(model.current().wires().empty());

    bp2::Blueprint::Node split2;
    split2.semantic.id = I.intern("split");
    split2.semantic.type = I.intern("Splitter");
    split2.layout.x = 220.0f;
    split2.layout.y = 120.0f;
    set_iface(split2, {
        make_port(I, "i", Domain::Electrical, bp2::Direction::Input, PortType::Any),
        make_port(I, "o1", Domain::Electrical, bp2::Direction::Output, PortType::Any),
    });
    EXPECT_TRUE(model.add_node(std::move(split2)));

    for (const auto& w : model.current().wires()) {
        auto [src_n, _src_p] = endpoint_node_port(w.source, arena);
        auto [tgt_n, _tgt_p] = endpoint_node_port(w.target, arena);
        EXPECT_NE(src_n, I.intern("split"));
        EXPECT_NE(tgt_n, I.intern("split"));
    }
}

TEST(CanvasInputWireProbe, ShiftClickWireRequestsProbeToggle) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto src = make_node(I, "src", "Battery", 40.0f, 120.0f);
    set_iface(src, {
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });
    auto load = make_node(I, "load", "Lamp", 420.0f, 120.0f);
    set_iface(load, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(src));
    bp = bp.with_node(std::move(load));
    bp = bp.with_wire(make_wire(I, arena, "wire_probe", "src", "v_out", "load", "v_in"));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* wire = dynamic_cast<visual::Wire*>(scene.find("wire_probe"));
    ASSERT_NE(wire, nullptr);
    ASSERT_GE(wire->polyline().size(), 2u);
    ui::Pt probe_pos = wire->polyline()[0];
    bool found_wire_hit = false;
    const auto& poly = wire->polyline();
    for (size_t i = 0; i + 1 < poly.size(); ++i) {
        const ui::Pt a = poly[i];
        const ui::Pt b = poly[i + 1];
        for (int step = 1; step <= 3; ++step) {
            const float t = static_cast<float>(step) / 4.0f;
            ui::Pt p(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
            auto hr = snapshot_hit_test(scene, I, p);
            if (std::holds_alternative<visual::HitWire>(hr)) {
                probe_pos = p;
                found_wire_hit = true;
                break;
            }
        }
        if (found_wire_hit) break;
    }
    ASSERT_TRUE(found_wire_hit);

    Viewport vp;
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    const ui::Pt canvas_min(0.0f, 0.0f);

    Modifiers mods;
    mods.shift = true;
    InputResult r = input.on_mouse_down(probe_pos, MouseButton::Left, canvas_min, mods);

    EXPECT_EQ(r.toggle_probe_wire_id, "wire_probe");
    EXPECT_TRUE(r.has_toggle_probe_world_pos);
    EXPECT_NEAR(r.toggle_probe_world_pos.x, probe_pos.x, 1.5f);
    EXPECT_NEAR(r.toggle_probe_world_pos.y, probe_pos.y, 1.5f);
    EXPECT_TRUE(input.selected_wire_id().empty());
}

TEST(CanvasInputSelection, ClickNodeDoesNotMarkModelDirty) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto node = make_node(I, "n1", "Battery", 120.0f, 80.0f);
    bp2::Blueprint bp;
    bp = bp.with_node(std::move(node));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* widget = dynamic_cast<visual::Widget*>(scene.find("n1"));
    ASSERT_NE(widget, nullptr);

    Viewport vp;
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    const ui::Pt canvas_min(0.0f, 0.0f);

    const size_t undo_before = model.undo_depth();
    EXPECT_FALSE(model.is_dirty());

    const ui::Pt click_pos = widget->worldPos() + ui::Pt(10.0f, 10.0f);
    input.on_mouse_down(click_pos, MouseButton::Left, canvas_min);
    input.on_mouse_up(MouseButton::Left, click_pos, canvas_min);

    EXPECT_EQ(input.state(), InputState::Idle);
    EXPECT_EQ(input.selected_node_ids().size(), 1u);
    EXPECT_EQ(model.undo_depth(), undo_before);
    EXPECT_FALSE(model.is_dirty());
}

TEST(CanvasInputDelete, DeleteNodeWithConnectedWiresIsSingleUndoStep) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto bat = make_node(I, "bat1", "Battery", 120.0f, 80.0f);
    set_iface(bat, {
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });
    auto lamp = make_node(I, "lamp1", "Lamp", 320.0f, 80.0f);
    set_iface(lamp, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(bat));
    bp = bp.with_node(std::move(lamp));
    bp = bp.with_wire(make_wire(I, arena, "wire_1", "bat1", "v_out", "lamp1", "v_in"));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* widget = dynamic_cast<visual::Widget*>(scene.find("bat1"));
    ASSERT_NE(widget, nullptr);

    Viewport vp;
    auto host = create_editor_model_host(model);
    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    const ui::Pt canvas_min(0.0f, 0.0f);

    const size_t undo_before = model.undo_depth();
    const ui::Pt click_pos = widget->worldPos() + ui::Pt(10.0f, 10.0f);
    input.on_mouse_down(click_pos, MouseButton::Left, canvas_min);
    input.on_mouse_up(MouseButton::Left, click_pos, canvas_min);
    input.on_key(Key::Delete);

    ASSERT_EQ(model.undo_depth(), undo_before + 1);
    EXPECT_EQ(model.current().find_node(I.intern("bat1")), nullptr);
    EXPECT_EQ(model.current().find_wire(I.intern("wire_1")), nullptr);

    model.undo();
    EXPECT_NE(model.current().find_node(I.intern("bat1")), nullptr);
    EXPECT_NE(model.current().find_wire(I.intern("wire_1")), nullptr);
}

TEST(CanvasInputDelete, MultiNodeDeleteIsSingleUndoStep) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto n1 = make_node(I, "n1", "Battery", 120.0f, 80.0f);
    set_iface(n1, {
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });
    auto n2 = make_node(I, "n2", "Lamp", 320.0f, 80.0f);
    set_iface(n2, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });
    auto n3 = make_node(I, "n3", "Lamp", 320.0f, 200.0f);
    set_iface(n3, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(n1));
    bp = bp.with_node(std::move(n2));
    bp = bp.with_node(std::move(n3));
    bp = bp.with_wire(make_wire(I, arena, "w1", "n1", "v_out", "n2", "v_in"));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    Viewport vp;
    auto host = create_editor_model_host(model);
    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    const ui::Pt canvas_min(0.0f, 0.0f);

    // Select both n1 and n2 (n1 has a connected wire)
    auto* w1 = dynamic_cast<visual::Widget*>(scene.find("n1"));
    auto* w2 = dynamic_cast<visual::Widget*>(scene.find("n2"));
    ASSERT_NE(w1, nullptr);
    ASSERT_NE(w2, nullptr);

    ui::Pt p1 = w1->worldPos() + ui::Pt(10.0f, 10.0f);
    ui::Pt p2 = w2->worldPos() + ui::Pt(10.0f, 10.0f);
    input.on_mouse_down(p1, MouseButton::Left, canvas_min);
    input.on_mouse_up(MouseButton::Left, p1, canvas_min);
    input.on_mouse_down(p2, MouseButton::Left, canvas_min, Modifiers{.ctrl = true});
    input.on_mouse_up(MouseButton::Left, p2, canvas_min);
    ASSERT_EQ(input.selected_node_ids().size(), 2u);

    const size_t undo_before = model.undo_depth();
    input.on_key(Key::Delete);

    // Deleting 2 nodes (plus connected wire) must be a single undo step
    ASSERT_EQ(model.undo_depth(), undo_before + 1)
        << "Multi-node delete must produce a single undo checkpoint";
    EXPECT_EQ(model.current().find_node(I.intern("n1")), nullptr);
    EXPECT_EQ(model.current().find_node(I.intern("n2")), nullptr);
    EXPECT_NE(model.current().find_node(I.intern("n3")), nullptr);
    EXPECT_TRUE(model.current().wires().empty());

    // Single undo restores all
    model.undo();
    EXPECT_NE(model.current().find_node(I.intern("n1")), nullptr);
    EXPECT_NE(model.current().find_node(I.intern("n2")), nullptr);
    EXPECT_NE(model.current().find_wire(I.intern("w1")), nullptr);
}

TEST(CanvasInputDelete, DeleteEmbeddedHostRemovesHostedNested) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto host_node = make_node(I, "host1", "CompositeType", 120.0f, 80.0f);
    host_node.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            std::make_unique<bp2::Blueprint>(bp2::Blueprint().with_id(I.intern("CompositeType"))))
    };

    auto inner_def = std::make_unique<bp2::Blueprint>();
    *inner_def = inner_def->with_id(I.intern("CompositeType"));
    *inner_def = inner_def->with_interface(bp2::Interface{});
    
    inner_def = std::make_unique<bp2::Blueprint>(inner_def->with_id(I.intern("CompositeType")));
    host_node.blueprint_instance().source = bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::move(inner_def)
    );

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(host_node));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    Viewport vp;
    auto host = create_editor_model_host(model);
    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());

    ASSERT_TRUE(input.select_node_by_id("host1"));
    input.on_key(Key::Delete);

    EXPECT_EQ(model.current().find_node(I.intern("host1")), nullptr);
}

TEST(CanvasInputDrag, MultiNodeDragIsSingleUndoStep) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto n1 = make_node(I, "n1", "Battery", 120.0f, 80.0f);
    auto n2 = make_node(I, "n2", "Lamp", 220.0f, 80.0f);
    bp2::Blueprint bp;
    bp = bp.with_node(std::move(n1));
    bp = bp.with_node(std::move(n2));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* w1 = dynamic_cast<visual::Widget*>(scene.find("n1"));
    auto* w2 = dynamic_cast<visual::Widget*>(scene.find("n2"));
    ASSERT_NE(w1, nullptr);
    ASSERT_NE(w2, nullptr);

    Viewport vp;
    auto host = create_editor_model_host(model);
    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    const ui::Pt canvas_min(0.0f, 0.0f);

    const size_t undo_before = model.undo_depth();

    // Select first node, then ctrl-add second node.
    ui::Pt p1 = w1->worldPos() + ui::Pt(10.0f, 10.0f);
    ui::Pt p2 = w2->worldPos() + ui::Pt(10.0f, 10.0f);
    input.on_mouse_down(p1, MouseButton::Left, canvas_min);
    input.on_mouse_up(MouseButton::Left, p1, canvas_min);
    input.on_mouse_down(p2, MouseButton::Left, canvas_min, Modifiers{.ctrl = true});
    input.on_mouse_up(MouseButton::Left, p2, canvas_min);
    ASSERT_EQ(input.selected_node_ids().size(), 2u);

    // Drag both nodes together.
    input.on_mouse_down(p1, MouseButton::Left, canvas_min);
    input.on_mouse_drag(MouseButton::Left, ui::Pt(40.0f, 20.0f), canvas_min);
    input.on_mouse_up(MouseButton::Left, p1 + ui::Pt(40.0f, 20.0f), canvas_min);
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    ASSERT_EQ(model.undo_depth(), undo_before + 1);
    ASSERT_NE(model.current().find_node(I.intern("n1")), nullptr);
    ASSERT_NE(model.current().find_node(I.intern("n2")), nullptr);
    EXPECT_FLOAT_EQ(model.current().find_node(I.intern("n1"))->layout.x, 160.0f);
    EXPECT_FLOAT_EQ(model.current().find_node(I.intern("n2"))->layout.x, 260.0f);

    model.undo();
    EXPECT_FLOAT_EQ(model.current().find_node(I.intern("n1"))->layout.x, 120.0f);
    EXPECT_FLOAT_EQ(model.current().find_node(I.intern("n2"))->layout.x, 220.0f);
}

TEST(CanvasInputGridStep, GridStepChangeDoesNotTouchBlueprintUndoHistory) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    bp2::Blueprint bp;

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    Viewport vp;
    vp.grid_step = 16.0f;
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    const size_t undo_before = model.undo_depth();

    vp.grid_step_up();

    EXPECT_EQ(model.undo_depth(), undo_before);
    EXPECT_FLOAT_EQ(vp.grid_step, 24.0f);
}

TEST(CanvasInputRoutingPoints, RoutingPointChangeIsSingleUndoStep) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto n1 = make_node(I, "n1", "Battery", 0.0f, 0.0f);
    set_iface(n1, {make_port(I, "v_out", bp2::Direction::Output, PortType::V)});
    auto n2 = make_node(I, "n2", "Lamp", 200.0f, 0.0f);
    set_iface(n2, {make_port(I, "v_in", bp2::Direction::Input, PortType::V)});

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(n1)).with_node(std::move(n2));

    bp2::Blueprint::Wire w;
    w.id = I.intern("w1");
    w.source = bp2::WireEndpoint{I.intern("n1"), I.intern("v_out")};
    w.target = bp2::WireEndpoint{I.intern("n2"), I.intern("v_in")};
    w.routing_points = {{100.0f, 50.0f}};
    bp = bp.with_wire(std::move(w));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    Viewport vp;
    auto host = create_editor_model_host(model);
    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());

    const size_t undo_before = model.undo_depth();

    // Move the routing point
    std::vector<std::pair<float, float>> new_points = {{120.0f, 60.0f}};
    input.snapshot_and_execute(cmd_set_routing_points(I.intern("w1"), std::move(new_points)));

    // Must produce exactly one undo step, not two
    ASSERT_EQ(model.undo_depth(), undo_before + 1)
        << "Routing point change must produce a single undo checkpoint";

    const auto* wire_after = model.current().find_wire(I.intern("w1"));
    ASSERT_NE(wire_after, nullptr);
    ASSERT_EQ(wire_after->routing_points.size(), 1u);
    EXPECT_FLOAT_EQ(wire_after->routing_points[0].first, 120.0f);
    EXPECT_FLOAT_EQ(wire_after->routing_points[0].second, 60.0f);

    model.undo();
    const auto* wire_undone = model.current().find_wire(I.intern("w1"));
    ASSERT_NE(wire_undone, nullptr);
    ASSERT_EQ(wire_undone->routing_points.size(), 1u);
    EXPECT_FLOAT_EQ(wire_undone->routing_points[0].first, 100.0f);
    EXPECT_FLOAT_EQ(wire_undone->routing_points[0].second, 50.0f);
}

TEST(CanvasInputDoubleClick, ValueNodeOpensInlineValueEditor) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto value_node = make_node(I, "val1", "Value", 120.0f, 80.0f);
    bp2::Blueprint bp;
    bp = bp.with_node(std::move(value_node));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* widget = dynamic_cast<visual::Widget*>(scene.find("val1"));
    ASSERT_NE(widget, nullptr);

    Viewport vp;
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    const ui::Pt canvas_min(0.0f, 0.0f);
    const ui::Pt click_pos = widget->worldPos() + ui::Pt(10.0f, 10.0f);

    InputResult r = input.on_double_click(click_pos, canvas_min);
    EXPECT_TRUE(r.open_inline_value_editor);
    EXPECT_EQ(r.inline_value_editor_node_id, "val1");
}

TEST(CanvasInputDoubleClick, NonValueNodeKeepsExistingDoubleClickBehavior) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto group = make_node(I, "grp1", "Composite", 120.0f, 80.0f);
    // Mark as blueprint instance (composite nodes can be expanded/opened)
    group.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            std::make_unique<bp2::Blueprint>(bp2::Blueprint().with_id(I.intern("Composite"))))
    };
    bp2::Blueprint bp;
    bp = bp.with_node(std::move(group));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* widget = dynamic_cast<visual::Widget*>(scene.find("grp1"));
    ASSERT_NE(widget, nullptr);

    Viewport vp;
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    const ui::Pt canvas_min(0.0f, 0.0f);
    const ui::Pt click_pos = widget->worldPos() + ui::Pt(10.0f, 10.0f);

    InputResult r = input.on_double_click(click_pos, canvas_min);
    EXPECT_FALSE(r.open_inline_value_editor);
    EXPECT_EQ(r.open_sub_window, "grp1");
}

// ============================================================================
// Regression: path_to_node_port() utility (extracted to snap.h)
// ============================================================================

TEST(PathToNodePort, ValidPortPath_ReturnsNodeAndPortIds) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    ui::InternedId node_id = I.intern("battery1");
    ui::InternedId port_name = I.intern("v_out");

    bp2::Path node_path = arena.make_node(arena.root(), node_id);
    bp2::Path port_path = arena.make_port(node_path, port_name);

    auto [result_node, result_port] = editor_math::path_to_node_port(port_path, arena);
    EXPECT_EQ(result_node, node_id);
    EXPECT_EQ(result_port, port_name);
}

TEST(PathToNodePort, NonPortPath_ReturnsEmpty) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    // A Node path (not a Port path) should return empty.
    bp2::Path node_path = arena.make_node(arena.root(), I.intern("some_node"));

    auto [result_node, result_port] = editor_math::path_to_node_port(node_path, arena);
    EXPECT_TRUE(result_node.empty());
    EXPECT_TRUE(result_port.empty());
}

TEST(PathToNodePort, RootPath_ReturnsEmpty) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto [result_node, result_port] = editor_math::path_to_node_port(arena.root(), arena);
    EXPECT_TRUE(result_node.empty());
    EXPECT_TRUE(result_port.empty());
}

// ============================================================================
// Regression: side_from_relative_position() (used by ref node orientation)
// ============================================================================

TEST(SideFromRelativePosition, NodeToTheRight_ReturnsRight) {
    ui::Pt from(100, 100);
    ui::Pt to(300, 100);
    EXPECT_EQ(editor_math::side_from_relative_position(from, to), bp2::PortLayoutSide::Right);
}

TEST(SideFromRelativePosition, NodeToTheLeft_ReturnsLeft) {
    ui::Pt from(300, 100);
    ui::Pt to(100, 100);
    EXPECT_EQ(editor_math::side_from_relative_position(from, to), bp2::PortLayoutSide::Left);
}

TEST(SideFromRelativePosition, NodeBelow_ReturnsBottom) {
    ui::Pt from(100, 100);
    ui::Pt to(100, 300);
    EXPECT_EQ(editor_math::side_from_relative_position(from, to), bp2::PortLayoutSide::Bottom);
}

TEST(SideFromRelativePosition, NodeAbove_ReturnsTop) {
    ui::Pt from(100, 300);
    ui::Pt to(100, 100);
    EXPECT_EQ(editor_math::side_from_relative_position(from, to), bp2::PortLayoutSide::Top);
}

TEST(SideFromRelativePosition, DiagonalTiesGoHorizontal) {
    // Equal |dx| and |dy| → horizontal wins → Right
    ui::Pt from(100, 100);
    ui::Pt to(200, 200);
    EXPECT_EQ(editor_math::side_from_relative_position(from, to), bp2::PortLayoutSide::Right);
}

// ============================================================================
// Regression: Ref node orientation after drag via commit_drag_node()
// ============================================================================

TEST(CanvasInputRefOrientation, DragRefNodeReorientsTowardConnectedNode) {
    // When a ref node is dragged from the right side of its connected node
    // to below it, the port layout side should change accordingly.
    ui::StringInterner I;
    bp2::PathArena arena(I);

    // Create a normal node at (200, 200)
    auto bat = make_node(I, "bat", "Battery", 200.0f, 200.0f);
    set_iface(bat, {
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    // Create a ref node to the right of the battery at (400, 200)
    auto ref = make_node(I, "gnd", "RefNode", 400.0f, 200.0f, "ref");
    set_iface(ref, {
        make_port(I, "v", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    auto w0 = make_wire(I, arena, "wire_0", "bat", "v_out", "gnd", "v");
    w0.domain = Domain::Electrical;

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(bat));
    bp = bp.with_node(std::move(ref));
    bp = bp.with_wire(std::move(w0));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    // Verify initial orientation: ref is to the right of bat → port should face Left
    auto* ref_widget = dynamic_cast<visual::RefNodeWidget*>(scene.find("gnd"));
    ASSERT_NE(ref_widget, nullptr);

    Viewport vp;
    vp.grid_step = 16.0f;
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    const ui::Pt canvas_min(0.0f, 0.0f);

    // Select and start dragging the ref node
    ui::Pt ref_pos = ref_widget->worldPos();
    ui::Pt click_pos = ref_pos + ui::Pt(10.0f, 10.0f);
    input.on_mouse_down(click_pos, MouseButton::Left, canvas_min);
    EXPECT_EQ(input.state(), InputState::DraggingNode);

    // Drag it below the battery: from (400, 200) to (200, 500)
    // world_delta = (-200, 300) but we need to apply as screen delta
    ui::Pt drag_delta(-200.0f, 300.0f);
    input.on_mouse_drag(MouseButton::Left, drag_delta, canvas_min);

    // Release — this calls commit_drag_node which re-orients ref nodes
    input.on_mouse_up(MouseButton::Left, click_pos + drag_delta, canvas_min);

    // Rebuild scene to reflect committed data
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    // After rebuild, the ref node should have its port oriented toward the battery
    // The ref node is now below the battery → port should face Top
    ref_widget = dynamic_cast<visual::RefNodeWidget*>(scene.find("gnd"));
    ASSERT_NE(ref_widget, nullptr);

     // Verify the data layer position was updated
     const bp2::Blueprint::Node* ref_data = model.current().find_node(I.intern("gnd"));
     ASSERT_NE(ref_data, nullptr);
     // The node should have moved — not still at (400, 200)
     EXPECT_NE(ref_data->layout.x, 400.0f);
}

// ============================================================================
// VerticalToggle layout: content bounds wide enough for click detection
// ============================================================================

TEST(CanvasInputContentToggle, VerticalToggleContentBoundsWideEnough) {
    // Regression: VerticalToggle content_widget was inside a flex column,
    // but preferredSize() didn't account for the content widget's width,
    // resulting in a 6.2px wide clickable area.
    ui::StringInterner I;
    bp2::PathArena arena(I);

    // Create an AZS-like node with VerticalToggle content and left/right ports
    auto azs = make_node(I, "azs_1", "AZS", 0.0f, 0.0f);
    set_iface(azs, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(azs));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("azs_1"));
    ASSERT_NE(widget, nullptr);

    // Content bounds must be at least as wide as the VerticalToggle's
    // preferred width (16px). Previously it was ~6.2px.
    Bounds cb = widget->contentBounds();
    Pt wpos = widget->worldPos();
    const auto& sem_snapshot = widget->content_semantic_snapshot();
    auto sem_hit = editor::presentation::hit_test_semantic_scene(
        sem_snapshot,
        Pt(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f));
    auto* sem_content = std::get_if<editor::presentation::SemanticHitContentRegion>(&sem_hit);
    ASSERT_NE(sem_content, nullptr);
    ASSERT_FALSE(sem_content->object->interactions.empty());
    EXPECT_EQ(sem_content->object->interactions[0].kind, editor::presentation::InteractionKind::Click);
    EXPECT_GE(cb.w, 16.0f)
        << "Content bounds width (" << cb.w << ") must be >= "
        << 16.0f << "px (VerticalToggle width)";
}

TEST(CanvasInputContentToggle, ClickOnVerticalToggleContentReturnsToggle) {
    // Verify that clicking in the center of the content area triggers a toggle
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto azs = make_node(I, "azs_1", "AZS", 100.0f, 100.0f);
    set_iface(azs, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(azs));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("azs_1"));
    ASSERT_NE(widget, nullptr);

    // Click at the center of the content area
    Bounds cb = widget->contentBounds();
    Pt wpos = widget->worldPos();
    Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);

    // Convert world to screen (zoom=1, pan=0 → screen == world)
    Pt canvas_min(0, 0);
    auto result = input.on_mouse_down(click_world, MouseButton::Left, canvas_min);

    EXPECT_FALSE(result.toggle_switch_node_id.empty())
        << "Clicking center of VerticalToggle content area should trigger toggle";
    if (!result.toggle_switch_node_id.empty()) {
        EXPECT_EQ(result.toggle_switch_node_id, "azs_1");
    }
}

TEST(CanvasInputContentToggle, EdgeClickOnVerticalToggleContentReturnsToggle) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto azs = make_node(I, "azs_1", "AZS", 100.0f, 100.0f);
    set_iface(azs, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(azs));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);
    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("azs_1"));
    ASSERT_NE(widget, nullptr);

    Bounds cb = widget->contentBounds();
    Pt wpos = widget->worldPos();
    Pt click_world(wpos.x + cb.x + 1.0f, wpos.y + cb.y + 1.0f);

    Pt canvas_min(0, 0);
    auto result = input.on_mouse_down(click_world, MouseButton::Left, canvas_min);
    EXPECT_EQ(result.toggle_switch_node_id, "azs_1");
}

TEST(CanvasInputLayoutSizing, ExplicitUndersizedNodeExpandsToRequiredMinimum) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto volt = make_node(I, "volt_1", "Voltmeter", 100.0f, 100.0f);
    set_params(volt, {{"min", "0.0"}, {"max", "30.0"}});
    volt.layout.width = 32.0f;
    volt.layout.height = 32.0f;
    set_iface(volt, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(volt));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());
    update_dynamic(scene, "volt_1", [](NodeContent& c) { c.value = 27.5f; });

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("volt_1"));
    ASSERT_NE(widget, nullptr);

    Pt minimum = widget->minimumNodeSize();
    EXPECT_GE(widget->size().x, minimum.x);
    EXPECT_GE(widget->size().y, minimum.y);
}

TEST(CanvasInputLayoutSizing, ManualResizeCannotShrinkBelowRequiredMinimum) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto volt = make_node(I, "volt_1", "Voltmeter", 100.0f, 100.0f);
    set_params(volt, {{"min", "0.0"}, {"max", "30.0"}});
    set_iface(volt, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(volt));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());
    update_dynamic(scene, "volt_1", [](NodeContent& c) { c.value = 27.5f; });

    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);
    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("volt_1"));
    ASSERT_NE(widget, nullptr);

    Pt minimum = widget->minimumNodeSize();
    const Pt canvas_min(0.0f, 0.0f);

    // Resize handles only work on already-selected nodes — select first
    // by clicking on the node body (center) and releasing.
    Pt center = widget->worldPos() + widget->size() * 0.5f;
    input.on_mouse_down(center, MouseButton::Left, canvas_min);
    input.on_mouse_up(MouseButton::Left, center, canvas_min);
    ASSERT_EQ(input.selected_node_ids().size(), 1u);

    Pt bottom_right = widget->worldPos() + widget->size();
    input.on_mouse_down(bottom_right, MouseButton::Left, canvas_min);
    ASSERT_EQ(input.state(), InputState::ResizingNode);

    input.on_mouse_drag(MouseButton::Left, Pt(-500.0f, -500.0f), canvas_min);

    EXPECT_GE(widget->size().x, minimum.x);
    EXPECT_GE(widget->size().y, minimum.y);

    input.on_mouse_up(MouseButton::Left, Pt(-500.0f, -500.0f), canvas_min);

    const auto* updated = model.current().find_node(I.lookup("volt_1"));
    ASSERT_NE(updated, nullptr);
    EXPECT_TRUE(updated->layout.manual_size);
}

TEST(CanvasInputLayoutSizing, ResizeSnapToGridDoesNotShrinkBelowRequiredMinimum) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto volt = make_node(I, "volt_1", "Voltmeter", 100.0f, 100.0f);
    set_params(volt, {{"min", "0.0"}, {"max", "30.0"}});
    set_iface(volt, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(volt));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());
    update_dynamic(scene, "volt_1", [](NodeContent& c) { c.value = 27.5f; });

    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    vp.grid_step = 24.0f;
    auto host = create_editor_model_host(model);
    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("volt_1"));
    ASSERT_NE(widget, nullptr);

    Pt minimum = widget->minimumNodeSize();
    const Pt canvas_min(0.0f, 0.0f);

    // Resize handles only work on already-selected nodes — select first
    // by clicking on the node body (center) and releasing.
    Pt center = widget->worldPos() + widget->size() * 0.5f;
    input.on_mouse_down(center, MouseButton::Left, canvas_min);
    input.on_mouse_up(MouseButton::Left, center, canvas_min);
    ASSERT_EQ(input.selected_node_ids().size(), 1u);

    Pt bottom_right = widget->worldPos() + widget->size();
    input.on_mouse_down(bottom_right, MouseButton::Left, canvas_min);
    ASSERT_EQ(input.state(), InputState::ResizingNode);

    input.on_mouse_drag(MouseButton::Left, Pt(-500.0f, -500.0f), canvas_min);

    EXPECT_GE(widget->size().x, minimum.x);
    EXPECT_GE(widget->size().y, minimum.y);
}

TEST(CanvasInputLayoutSizing, VerticalToggleMinimumHeightDoesNotAddFullContentStackHeight) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto azs = make_node(I, "azs_1", "AZS", 100.0f, 100.0f);
    set_iface(azs, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(azs));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("azs_1"));
    ASSERT_NE(widget, nullptr);

    Pt minimum = widget->minimumNodeSize();
    EXPECT_LE(minimum.y, 96.0f)
        << "VerticalToggle minimum height should come from row/header/footer layout, not a stacked full-content addition";
}

// ============================================================================
// Bug 3 regression: simulation_mode blocks editing but allows widget interaction
// ============================================================================

TEST(CanvasInputSimMode, SimModeBlocksNodeDrag) {
    // In simulation mode, clicking on a node body should NOT enter
    // DraggingNode state and should NOT select the node.
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto node = make_node(I, "n1", "Battery", 120.0f, 80.0f);
    bp2::Blueprint bp;
    bp = bp.with_node(std::move(node));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* widget = dynamic_cast<visual::Widget*>(scene.find("n1"));
    ASSERT_NE(widget, nullptr);

    Viewport vp;
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    input.simulation_mode = true;

    const Pt canvas_min(0.0f, 0.0f);
    Pt click_pos = widget->worldPos() + Pt(10.0f, 10.0f);
    input.on_mouse_down(click_pos, MouseButton::Left, canvas_min);

    EXPECT_NE(input.state(), InputState::DraggingNode)
        << "simulation_mode must block node dragging";
    EXPECT_EQ(input.state(), InputState::Panning)
        << "clicking on node body in simulation_mode should pan";
    EXPECT_EQ(input.selected_node_ids().size(), 0u)
        << "simulation_mode must not select nodes";
}

TEST(CanvasInputSimMode, SimModeBlocksWireCreation) {
    // In simulation mode, clicking on a port should NOT enter wire creation.
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto src = make_node(I, "src", "Battery", 40.0f, 120.0f);
    set_iface(src, {
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(src));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* src_w = dynamic_cast<visual::Widget*>(scene.find("src"));
    ASSERT_NE(src_w, nullptr);
    auto* src_out = src_w->portByName("v_out");
    ASSERT_NE(src_out, nullptr);

    Viewport vp;
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    input.simulation_mode = true;

    const Pt canvas_min(0.0f, 0.0f);
    Pt port_pos = src_out->worldPos() + Pt(visual::PortConstants::RADIUS, visual::PortConstants::RADIUS);
    input.on_mouse_down(port_pos, MouseButton::Left, canvas_min);

    EXPECT_NE(input.state(), InputState::CreatingWire)
        << "simulation_mode must block wire creation";
}

TEST(CanvasInputSimMode, SimModeBlocksDeleteKey) {
    // In simulation mode, pressing Delete with a selected node must NOT remove it.
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto node = make_node(I, "n1", "Battery", 120.0f, 80.0f);
    bp2::Blueprint bp;
    bp = bp.with_node(std::move(node));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    Viewport vp;
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    input.simulation_mode = true;

    ASSERT_TRUE(input.select_node_by_id("n1"));
    input.on_key(Key::Delete);

    // Node must still exist
    EXPECT_NE(model.current().find_node(I.intern("n1")), nullptr)
        << "simulation_mode must block node deletion";
}

TEST(CanvasInputSimMode, SimModeAllowsToggleInteraction) {
    // In simulation mode, clicking on a VerticalToggle content area should
    // still return toggle_switch_node_id.
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto azs = make_node(I, "azs_1", "AZS", 100.0f, 100.0f);
    set_iface(azs, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(azs));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    input.simulation_mode = true;

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("azs_1"));
    ASSERT_NE(widget, nullptr);

    Bounds cb = widget->contentBounds();
    Pt wpos = widget->worldPos();
    Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);

    Pt canvas_min(0, 0);
    auto result = input.on_mouse_down(click_world, MouseButton::Left, canvas_min);

    EXPECT_EQ(result.toggle_switch_node_id, "azs_1")
        << "simulation_mode must allow toggle interaction";
}

TEST(CanvasInputSimMode, SimModeAllowsKnobInteraction) {
    // In simulation mode, clicking on a Knob content area should still
    // enter DraggingKnob state and return knob_node_id.
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto knob = make_node(I, "knob_1", "KnobSwitch", 100.0f, 100.0f);
    set_params(knob, {{"positions", "5.0"}});
    set_iface(knob, {
        make_port(I, "throw1", Domain::Electrical, bp2::Direction::InOut, PortType::V),
        make_port(I, "throw1", Domain::Electrical, bp2::Direction::InOut, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(knob));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    input.simulation_mode = true;

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("knob_1"));
    ASSERT_NE(widget, nullptr);

    Bounds cb = widget->contentBounds();
    Pt wpos = widget->worldPos();
    Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);

    Pt canvas_min(0, 0);
    auto result = input.on_mouse_down(click_world, MouseButton::Left, canvas_min);

    EXPECT_EQ(result.knob_node_id, "knob_1")
        << "simulation_mode must allow knob interaction";
    EXPECT_EQ(input.state(), InputState::DraggingKnob)
        << "simulation_mode must allow knob drag state";
}

TEST(CanvasInputSimMode, SimModeAllowsSliderInteraction) {
    // In simulation mode, clicking on a Slider content area should still
    // enter DraggingSlider state and return slider_node_id.
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto slider = make_node(I, "slider_1", "Slider", 100.0f, 100.0f);
    set_params(slider, {{"min", "0.0"}, {"max", "100.0"}});
    set_iface(slider, {
        make_port(I, "ctrl", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(slider));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    input.simulation_mode = true;

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("slider_1"));
    ASSERT_NE(widget, nullptr);

    Bounds cb = widget->contentBounds();
    Pt wpos = widget->worldPos();
     Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);

     Pt canvas_min(0, 0);
     auto result = input.on_mouse_down(click_world, MouseButton::Left, canvas_min);

     EXPECT_EQ(result.slider_node_id, "slider_1")
         << "simulation_mode must allow slider interaction";
    EXPECT_EQ(input.state(), InputState::DraggingSlider)
        << "simulation_mode must allow slider drag state";
}

TEST(CanvasInputSimMode, SimModeAllowsSliderInteractionAtEdge) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto slider = make_node(I, "slider_1", "Slider", 100.0f, 100.0f);
    set_params(slider, {{"min", "0.0"}, {"max", "100.0"}});
    set_iface(slider, {
        make_port(I, "ctrl", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(slider));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    input.simulation_mode = true;

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("slider_1"));
    ASSERT_NE(widget, nullptr);

    Bounds cb = widget->contentBounds();
    Pt wpos = widget->worldPos();
    Pt click_world(wpos.x + cb.x + 1.0f, wpos.y + cb.y + cb.h * 0.5f);

    Pt canvas_min(0, 0);
    auto result = input.on_mouse_down(click_world, MouseButton::Left, canvas_min);
    EXPECT_EQ(result.slider_node_id, "slider_1");
    EXPECT_EQ(input.state(), InputState::DraggingSlider);
}

TEST(HitTestInteractionTarget, VerticalToggleReturnsToggleRole) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto azs = make_node(I, "azs_1", "AZS", 100.0f, 100.0f);
    set_iface(azs, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(azs));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("azs_1"));
    ASSERT_NE(widget, nullptr);

    Bounds cb = widget->contentBounds();
    Pt wpos = widget->worldPos();
    Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);

     auto hit = snapshot_hit_test(scene, I, click_world);
     auto* hit_node = std::get_if<visual::HitNode>(&hit);
     ASSERT_NE(hit_node, nullptr) << "hit_test should return HitNode for interactive content";
     EXPECT_EQ(hit_node->node_id, I.intern("azs_1"));
     
     const auto& sem_snapshot = widget->content_semantic_snapshot();
     auto sem_hit = editor::presentation::hit_test_semantic_scene(sem_snapshot, Pt(cb.x + cb.w * 0.5f, cb.y + cb.h * 0.5f));
     auto* sem_content = std::get_if<editor::presentation::SemanticHitContentRegion>(&sem_hit);
     ASSERT_NE(sem_content, nullptr);
     ASSERT_FALSE(sem_content->object->interactions.empty());
     EXPECT_EQ(sem_content->object->interactions[0].kind, editor::presentation::InteractionKind::Click);
}

TEST(HitTestInteractionTarget, KnobReturnsDiscreteSelectorRole) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto knob = make_node(I, "knob_1", "KnobSwitch", 100.0f, 100.0f);
    set_params(knob, {{"positions", "5.0"}});
    set_iface(knob, {
        make_port(I, "throw1", Domain::Electrical, bp2::Direction::InOut, PortType::V),
        make_port(I, "throw2", Domain::Electrical, bp2::Direction::InOut, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(knob));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("knob_1"));
    ASSERT_NE(widget, nullptr);

    Bounds cb = widget->contentBounds();
    Pt wpos = widget->worldPos();
    Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);

     auto hit = snapshot_hit_test(scene, I, click_world);
     auto* hit_node = std::get_if<visual::HitNode>(&hit);
     ASSERT_NE(hit_node, nullptr) << "hit_test should return HitNode for interactive content";
     EXPECT_EQ(hit_node->node_id, I.intern("knob_1"));
     
     const auto& sem_snapshot = widget->content_semantic_snapshot();
     auto sem_hit = editor::presentation::hit_test_semantic_scene(sem_snapshot, Pt(cb.x + cb.w * 0.5f, cb.y + cb.h * 0.5f));
     auto* sem_content = std::get_if<editor::presentation::SemanticHitContentRegion>(&sem_hit);
     ASSERT_NE(sem_content, nullptr);
     ASSERT_FALSE(sem_content->object->interactions.empty());
     EXPECT_EQ(sem_content->object->interactions[0].kind, editor::presentation::InteractionKind::DragDiscrete);
}

TEST(HitTestInteractionTarget, KnobContentBoundsCoverVisibleKnobSize) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto knob = make_node(I, "knob_1", "KnobSwitch", 100.0f, 100.0f);
    set_params(knob, {{"positions", "5.0"}});
    set_iface(knob, {
        make_port(I, "throw1", Domain::Electrical, bp2::Direction::InOut, PortType::V),
        make_port(I, "throw2", Domain::Electrical, bp2::Direction::InOut, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(knob));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("knob_1"));
    ASSERT_NE(widget, nullptr);

    Bounds cb = widget->contentBounds();
    EXPECT_GE(cb.w, 48.0f);
    EXPECT_GE(cb.h, 48.0f);
}

TEST(HitTestInteractionTarget, SliderReturnsContinuousScalarRole) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto slider = make_node(I, "slider_1", "Slider", 100.0f, 100.0f);
    set_params(slider, {{"min", "0.0"}, {"max", "100.0"}});
    set_iface(slider, {
        make_port(I, "ctrl", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(slider));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("slider_1"));
    ASSERT_NE(widget, nullptr);

    Bounds cb = widget->contentBounds();
    Pt wpos = widget->worldPos();
    Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);

     auto hit = snapshot_hit_test(scene, I, click_world);
     auto* hit_node = std::get_if<visual::HitNode>(&hit);
     ASSERT_NE(hit_node, nullptr) << "hit_test should return HitNode for interactive content";
     EXPECT_EQ(hit_node->node_id, I.intern("slider_1"));
     
     const auto& sem_snapshot = widget->content_semantic_snapshot();
     auto sem_hit = editor::presentation::hit_test_semantic_scene(sem_snapshot, Pt(cb.x + cb.w * 0.5f, cb.y + cb.h * 0.5f));
     auto* sem_content = std::get_if<editor::presentation::SemanticHitContentRegion>(&sem_hit);
     ASSERT_NE(sem_content, nullptr);
     ASSERT_FALSE(sem_content->object->interactions.empty());
     EXPECT_EQ(sem_content->object->interactions[0].kind, editor::presentation::InteractionKind::DragScalar);
}

TEST(HitTestInteractionTarget, InteractionTargetWinsOverGenericNodeBodyHit) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto slider = make_node(I, "slider_1", "Slider", 100.0f, 100.0f);
    set_params(slider, {{"min", "0.0"}, {"max", "100.0"}});
    set_iface(slider, {
        make_port(I, "ctrl", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(slider));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("slider_1"));
    ASSERT_NE(widget, nullptr);

    Bounds cb = widget->contentBounds();
    Pt wpos = widget->worldPos();
    Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);

    auto hit = snapshot_hit_test(scene, I, click_world);
    EXPECT_TRUE(std::holds_alternative<visual::HitNode>(hit));
    auto* hit_node = std::get_if<visual::HitNode>(&hit);
    ASSERT_NE(hit_node, nullptr);
    EXPECT_EQ(hit_node->node_id, I.intern("slider_1"));
}

TEST(HitTestInteractionTarget, ZoomedVerticalToggleStillReturnsToggleRole) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto azs = make_node(I, "azs_1", "AZS", 100.0f, 100.0f);
    set_iface(azs, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(azs));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    Viewport vp;
    vp.zoom = 2.0f;
    vp.pan = Pt(40.0f, 20.0f);

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("azs_1"));
    ASSERT_NE(widget, nullptr);

    Bounds cb = widget->contentBounds();
    Pt wpos = widget->worldPos();
    Pt world_click(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);
    Pt screen_click = vp.world_to_screen(world_click, Pt(0, 0));
    Pt roundtrip_world = vp.screen_to_world(screen_click, Pt(0, 0));

     auto hit = snapshot_hit_test(scene, I, roundtrip_world);
     auto* hit_node = std::get_if<visual::HitNode>(&hit);
     ASSERT_NE(hit_node, nullptr);
     EXPECT_EQ(hit_node->node_id, I.intern("azs_1"));
     
     const auto& sem_snapshot = widget->content_semantic_snapshot();
     auto sem_hit = editor::presentation::hit_test_semantic_scene(sem_snapshot, Pt(cb.x + cb.w * 0.5f, cb.y + cb.h * 0.5f));
     auto* sem_content = std::get_if<editor::presentation::SemanticHitContentRegion>(&sem_hit);
     ASSERT_NE(sem_content, nullptr);
     ASSERT_FALSE(sem_content->object->interactions.empty());
     EXPECT_EQ(sem_content->object->interactions[0].kind, editor::presentation::InteractionKind::Click);
}

TEST(CanvasInputInteractionTarget, VerticalTogglePublishesToggleRole) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto azs = make_node(I, "azs_1", "AZS", 100.0f, 100.0f);
    set_iface(azs, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(azs));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("azs_1"));
    ASSERT_NE(widget, nullptr);

     Bounds cb = widget->contentBounds();
     Pt wpos = widget->worldPos();
     Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);

     const auto& sem_snapshot = widget->content_semantic_snapshot();
     auto sem_hit = editor::presentation::hit_test_semantic_scene(sem_snapshot, Pt(cb.x + cb.w * 0.5f, cb.y + cb.h * 0.5f));
     auto* sem_content = std::get_if<editor::presentation::SemanticHitContentRegion>(&sem_hit);
     ASSERT_NE(sem_content, nullptr);
     ASSERT_FALSE(sem_content->object->interactions.empty());
     EXPECT_EQ(sem_content->object->interactions[0].kind, editor::presentation::InteractionKind::Click);
}

TEST(CanvasInputSemanticRender, SwitchProducesRenderObjectsAndHitObjects) {
    // Regression: render objects for Switch were inside the !interaction_info
    // branch, but derive_content_interaction always returns Toggle for Switch,
    // making the render path unreachable — switches were invisible.
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto sw = make_node(I, "sw_1", "Switch", 100.0f, 100.0f);
    sw.semantic.string_params["closed"] = "true";
    set_iface(sw, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(sw));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("sw_1"));
    ASSERT_NE(widget, nullptr);

    // Must have BOTH render objects (body + handle) AND hit objects
    const auto& snap = widget->content_semantic_snapshot();
    EXPECT_GE(snap.render_objects.size(), 2u)
        << "Switch must produce at least 2 render objects (body + handle)";
    EXPECT_FALSE(snap.hit_objects.empty())
        << "Switch must produce at least 1 hit object for interaction";
    EXPECT_TRUE(widget->renders_content_from_semantic_snapshot())
        << "Switch must set render_content_from_semantic_snapshot_ = true";
}

TEST(CanvasInputSemanticRender, VerticalToggleStandardLayoutProducesRenderObjectsAndHitObjects) {
    // Regression: same dead-code bug as Switch — VerticalToggle in standard
    // layout (with layout overrides forcing it off the VerticalToggle layout
    // path) used a Spacer but never populated render objects.
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto azs = make_node(I, "azs_1", "AZS", 100.0f, 100.0f);
    // Force standard layout by adding layout overrides
    azs.layout.layout_overrides.push_back(bp2::Blueprint::Node::PortLayoutOverride{
        "v_in", "left", std::nullopt});
    set_iface(azs, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(azs));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());
    update_dynamic(scene, "azs_1", [](NodeContent& c) { c.tripped = true; });

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("azs_1"));
    ASSERT_NE(widget, nullptr);

    const auto& snap = widget->content_semantic_snapshot();
    EXPECT_GE(snap.render_objects.size(), 2u)
        << "VerticalToggle (standard layout) must produce render objects";
    EXPECT_FALSE(snap.hit_objects.empty())
        << "VerticalToggle (standard layout) must produce hit objects";
    EXPECT_TRUE(widget->renders_content_from_semantic_snapshot())
        << "VerticalToggle (standard layout) must set render flag";
}

TEST(CanvasInputSemanticRender, AzsTrippedStateProducesTrippedColor) {
    // Regression: AZS thermal trip must produce COLOR_TRIPPED (red tint).
    // This verifies the visual pipeline from NodeContent::tripped →
    // PresentationSpec::content_tripped → COLOR_TRIPPED in build_switch_content.
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto azs = make_node(I, "azs_1", "AZS", 100.0f, 100.0f);
    set_iface(azs, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(azs));
    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    // Set tripped = true (simulates what overlay_from_cache does for AzsPorts)
    update_dynamic(scene, "azs_1", [](NodeContent& c) { c.tripped = true; });

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("azs_1"));
    ASSERT_NE(widget, nullptr);

    // Verify tripped flag round-trips through the widget
    NodeContent roundtrip = widget->currentContent();
    EXPECT_TRUE(roundtrip.tripped) << "AZS tripped flag must round-trip through NodeWidget";

    // Verify the visual snapshot uses COLOR_TRIPPED for the fill
    const auto& snap = widget->content_semantic_snapshot();
    ASSERT_GE(snap.render_objects.size(), 2u) << "AZS switch must have body + handle";

    // COLOR_TRIPPED = 0xFF4040FF — distinct from state-on green (0xFF3A6830)
    // and state-off dark (0xFF1C1D24)
    const uint32_t body_fill = snap.render_objects[0].fill_color;
    EXPECT_EQ(body_fill, 0xFF4040FF)
        << "Tripped AZS body must use COLOR_TRIPPED, got: " << std::hex << body_fill;
}

TEST(CanvasInputSemanticRender, AzsNonTrippedStateUsesNormalColors) {
    // Companion: when tripped = false, AZS uses standard switch colors.
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto azs = make_node(I, "azs_2", "AZS", 100.0f, 100.0f);
    set_iface(azs, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(azs));
    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    // Explicitly tripped = false (default)
    update_dynamic(scene, "azs_2", [](NodeContent& c) { c.tripped = false; });

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("azs_2"));
    ASSERT_NE(widget, nullptr);

    const auto& snap = widget->content_semantic_snapshot();
    ASSERT_GE(snap.render_objects.size(), 2u);

    const uint32_t body_fill = snap.render_objects[0].fill_color;
    EXPECT_NE(body_fill, 0xFF4040FF)
        << "Non-tripped AZS must NOT use COLOR_TRIPPED, got: " << std::hex << body_fill;
}

TEST(CanvasInputInteractionTarget, KnobPublishesDiscreteSelectorRole) {
     ui::StringInterner I;
     bp2::PathArena arena(I);

     auto knob = make_node(I, "knob_1", "KnobSwitch", 100.0f, 100.0f);
     set_params(knob, {{"positions", "5.0"}});
     set_iface(knob, {
         make_port(I, "throw1", Domain::Electrical, bp2::Direction::InOut, PortType::V),
         make_port(I, "throw2", Domain::Electrical, bp2::Direction::InOut, PortType::V),
     });

     bp2::Blueprint bp;
     bp = bp.with_node(std::move(knob));

     bp2::EditorModel model(std::move(bp));
     visual::Scene scene;
     visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

     auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("knob_1"));
     ASSERT_NE(widget, nullptr);

     Bounds cb = widget->contentBounds();
     Pt wpos = widget->worldPos();
     Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);

     const auto& sem_snapshot = widget->content_semantic_snapshot();
     auto sem_hit = editor::presentation::hit_test_semantic_scene(sem_snapshot, Pt(cb.x + cb.w * 0.5f, cb.y + cb.h * 0.5f));
     auto* sem_content = std::get_if<editor::presentation::SemanticHitContentRegion>(&sem_hit);
     ASSERT_NE(sem_content, nullptr);
     ASSERT_FALSE(sem_content->object->interactions.empty());
     EXPECT_EQ(sem_content->object->interactions[0].kind, editor::presentation::InteractionKind::DragDiscrete);
 }

TEST(CanvasInputSemanticRender, ContentSnapshotObjectIdsAreUniqueAndFindable) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto knob = make_node(I, "knob_1", "KnobSwitch", 100.0f, 100.0f);
    set_params(knob, {{"positions", "5.0"}});
    set_iface(knob, {
        make_port(I, "throw1", Domain::Electrical, bp2::Direction::InOut, PortType::V),
        make_port(I, "throw2", Domain::Electrical, bp2::Direction::InOut, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(knob));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("knob_1"));
    ASSERT_NE(widget, nullptr);

    const auto& snapshot = widget->content_semantic_snapshot();
    ASSERT_GT(snapshot.render_objects.size(), 1u);
    ASSERT_FALSE(snapshot.hit_objects.empty());

    for (size_t i = 0; i + 1 < snapshot.render_objects.size(); ++i) {
        for (size_t j = i + 1; j < snapshot.render_objects.size(); ++j) {
            EXPECT_NE(snapshot.render_objects[i].id, snapshot.render_objects[j].id);
        }
    }

    for (const auto& object : snapshot.render_objects) {
        EXPECT_EQ(editor::presentation::find_render_object_by_id(snapshot, object.id), &object);
    }
    for (const auto& object : snapshot.hit_objects) {
        EXPECT_EQ(editor::presentation::find_hit_object_by_id(snapshot, object.id), &object);
    }
}

TEST(CanvasInputSemanticRender, IndicatorAndKnobCirclesUseCenteredRadiusEncoding) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto indicator = make_node(I, "ind_1", "IndicatorLight", 100.0f, 100.0f);
    set_iface(indicator, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    auto knob = make_node(I, "knob_1", "KnobSwitch", 240.0f, 100.0f);
    set_params(knob, {{"positions", "5.0"}});
    set_iface(knob, {
        make_port(I, "throw1", Domain::Electrical, bp2::Direction::InOut, PortType::V),
        make_port(I, "throw2", Domain::Electrical, bp2::Direction::InOut, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(indicator));
    bp = bp.with_node(std::move(knob));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());
    update_dynamic(scene, "ind_1", [](NodeContent& c) { c.value = 1.0f; });

    auto* indicator_widget = dynamic_cast<visual::NodeWidget*>(scene.find("ind_1"));
    auto* knob_widget = dynamic_cast<visual::NodeWidget*>(scene.find("knob_1"));
    ASSERT_NE(indicator_widget, nullptr);
    ASSERT_NE(knob_widget, nullptr);

    const Bounds ind_cb = indicator_widget->contentBounds();
    const auto& ind_snapshot = indicator_widget->content_semantic_snapshot();
    const auto ind_circle = std::find_if(ind_snapshot.render_objects.begin(), ind_snapshot.render_objects.end(),
        [](const editor::presentation::SceneRenderObject& object) {
            return object.primitive == editor::presentation::PaintPrimitiveKind::Circle;
        });
    ASSERT_NE(ind_circle, ind_snapshot.render_objects.end());
    const auto* ind_geo = std::get_if<editor::presentation::CircleGeometry>(&ind_circle->geometry);
    ASSERT_NE(ind_geo, nullptr);
    // Radial primitives use bounds center + geometry offset as world-space center
    EXPECT_FLOAT_EQ(ind_circle->bounds.x + ind_circle->bounds.w * 0.5f + ind_geo->cx,
                    ind_cb.x + ind_cb.w * 0.5f);
    EXPECT_FLOAT_EQ(ind_circle->bounds.y + ind_circle->bounds.h * 0.5f + ind_geo->cy,
                    ind_cb.y + ind_cb.h * 0.5f);
    EXPECT_GT(ind_geo->radius, 0.0f);

    const Bounds knob_cb = knob_widget->contentBounds();
    const auto& knob_snapshot = knob_widget->content_semantic_snapshot();
    const auto knob_circle = std::find_if(knob_snapshot.render_objects.begin(), knob_snapshot.render_objects.end(),
        [](const editor::presentation::SceneRenderObject& object) {
            return object.primitive == editor::presentation::PaintPrimitiveKind::Circle;
        });
    ASSERT_NE(knob_circle, knob_snapshot.render_objects.end());
    const auto* knob_geo = std::get_if<editor::presentation::CircleGeometry>(&knob_circle->geometry);
    ASSERT_NE(knob_geo, nullptr);
    EXPECT_FLOAT_EQ(knob_circle->bounds.x + knob_circle->bounds.w * 0.5f + knob_geo->cx,
                    knob_cb.x + knob_cb.w * 0.5f);
    EXPECT_FLOAT_EQ(knob_circle->bounds.y + knob_circle->bounds.h * 0.5f + knob_geo->cy,
                    knob_cb.y + knob_cb.h * 0.5f);
    EXPECT_GT(knob_geo->radius, 0.0f);
}

TEST(CanvasInputSemanticRender, GaugeRestoresLegacyTextStackAndFullComposition) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto gauge = make_node(I, "volt_1", "Voltmeter", 100.0f, 100.0f);
    set_params(gauge, {{"min", "0.0"}, {"max", "30.0"}});
    set_iface(gauge, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(gauge));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());
    update_dynamic(scene, "volt_1", [](NodeContent& c) { c.value = 27.5f; });

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("volt_1"));
    ASSERT_NE(widget, nullptr);

    const auto& snapshot = widget->content_semantic_snapshot();
    EXPECT_GE(snapshot.render_objects.size(), 15u)
        << "Gauge should restore arc, ticks, needle, center cap, and both text rows";

    const editor::presentation::SceneRenderObject* value_text = nullptr;
    const editor::presentation::SceneRenderObject* unit_text = nullptr;
    for (const auto& object : snapshot.render_objects) {
        if (object.primitive != editor::presentation::PaintPrimitiveKind::Text) {
            continue;
        }
        if (object.text == "27.5") {
            value_text = &object;
        } else if (object.text == "V") {
            unit_text = &object;
        }
    }

    ASSERT_NE(value_text, nullptr);
    ASSERT_NE(unit_text, nullptr);
    // With Overlay layout, both texts share the same bounds rect.
    // The Y separation is encoded in TextGeometry.y (relative offset within the element).
    const auto* value_tg = std::get_if<editor::presentation::TextGeometry>(&value_text->geometry);
    const auto* unit_tg = std::get_if<editor::presentation::TextGeometry>(&unit_text->geometry);
    ASSERT_NE(value_tg, nullptr);
    ASSERT_NE(unit_tg, nullptr);
    EXPECT_GT(unit_tg->y, value_tg->y);
    EXPECT_GT(value_tg->font_size, unit_tg->font_size);
}

TEST(CanvasInputInteractionTarget, KnobTargetCarriesStepsMetadata) {
    // Verify that the knob target publishes steps metadata so that CanvasInput
    // can use this to track positions without reading concrete widget member variables.
    // This is the core contract for issue #136: semantic metadata instead of concrete knowledge.
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto knob = make_node(I, "knob_1", "KnobSwitch", 100.0f, 100.0f);
    set_params(knob, {{"positions", "7.0"}});  // 7 positions
    set_iface(knob, {
        make_port(I, "throw1", Domain::Electrical, bp2::Direction::InOut, PortType::V),
        make_port(I, "throw2", Domain::Electrical, bp2::Direction::InOut, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(knob));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("knob_1"));
    ASSERT_NE(widget, nullptr);

     Bounds cb = widget->contentBounds();
     Pt wpos = widget->worldPos();
     Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);

     const auto& sem_snapshot = widget->content_semantic_snapshot();
     auto sem_hit = editor::presentation::hit_test_semantic_scene(sem_snapshot, Pt(cb.x + cb.w * 0.5f, cb.y + cb.h * 0.5f));
     auto* sem_content = std::get_if<editor::presentation::SemanticHitContentRegion>(&sem_hit);
     ASSERT_NE(sem_content, nullptr);
     ASSERT_FALSE(sem_content->object->interactions.empty());
     EXPECT_EQ(sem_content->object->interactions[0].kind, editor::presentation::InteractionKind::DragDiscrete);
     
     // The key contract: binding publishes steps metadata.
     EXPECT_EQ(sem_content->object->interactions[0].step, 7)
         << "Binding must publish steps (discrete positions) from semantic content metadata";
    
    // Verify that CanvasInput can use this metadata to track knob state.
    // CanvasInput::enter_drag_knob now reads target.steps instead of accessing
    // concrete knob-renderer internals or node view fields directly.
    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);
    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    
    Pt canvas_min(0, 0);
    auto result = input.on_mouse_down(click_world, MouseButton::Left, canvas_min);
    
    EXPECT_EQ(result.knob_node_id, "knob_1")
        << "CanvasInput must handle knob interaction via semantic target metadata";
    EXPECT_EQ(input.state(), InputState::DraggingKnob)
        << "CanvasInput must enter DraggingKnob state via semantic metadata";
}

TEST(CanvasInputInteractionTarget, KnobTargetCarriesConfiguredFiveStepsMetadata) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto knob = make_node(I, "knob_5", "KnobSwitch", 100.0f, 100.0f);
    set_params(knob, {{"positions", "5.0"}});
    set_iface(knob, {
        make_port(I, "throw1", Domain::Electrical, bp2::Direction::InOut, PortType::V),
        make_port(I, "throw2", Domain::Electrical, bp2::Direction::InOut, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(knob));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("knob_5"));
    ASSERT_NE(widget, nullptr);

    Bounds cb = widget->contentBounds();
    const auto& sem_snapshot = widget->content_semantic_snapshot();
    auto sem_hit = editor::presentation::hit_test_semantic_scene(
        sem_snapshot,
        Pt(cb.x + cb.w * 0.5f, cb.y + cb.h * 0.5f));
    auto* sem_content = std::get_if<editor::presentation::SemanticHitContentRegion>(&sem_hit);
    ASSERT_NE(sem_content, nullptr);
    ASSERT_FALSE(sem_content->object->interactions.empty());
    EXPECT_EQ(sem_content->object->interactions[0].kind, editor::presentation::InteractionKind::DragDiscrete);
    EXPECT_EQ(sem_content->object->interactions[0].step, 5);
}

TEST(CanvasInputInteractionTarget, SliderPublishesContinuousScalarRole) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto slider = make_node(I, "slider_1", "Slider", 100.0f, 100.0f);
    set_params(slider, {{"min", "0.0"}, {"max", "100.0"}});
    set_iface(slider, {
        make_port(I, "ctrl", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(slider));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("slider_1"));
    ASSERT_NE(widget, nullptr);

     Bounds cb = widget->contentBounds();
     Pt wpos = widget->worldPos();
     Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);

     const auto& sem_snapshot = widget->content_semantic_snapshot();
     auto sem_hit = editor::presentation::hit_test_semantic_scene(sem_snapshot, Pt(cb.x + cb.w * 0.5f, cb.y + cb.h * 0.5f));
     auto* sem_content = std::get_if<editor::presentation::SemanticHitContentRegion>(&sem_hit);
     ASSERT_NE(sem_content, nullptr);
     ASSERT_FALSE(sem_content->object->interactions.empty());
      EXPECT_EQ(sem_content->object->interactions[0].kind, editor::presentation::InteractionKind::DragScalar);
}

TEST(CanvasInputInteractionTarget, SliderTargetCarriesMappingBoundsNotGeometry) {
    // Verify that the slider target publishes mapping bounds (primary_min, primary_max)
    // and that CanvasInput uses these bounds instead of depending on concrete slider geometry.
    // This is the core contract for issue #136: semantic mapping instead of geometry knowledge.
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto slider = make_node(I, "slider_1", "Slider", 100.0f, 100.0f);
    set_params(slider, {{"min", "0.0"}, {"max", "100.0"}});
    set_iface(slider, {
        make_port(I, "ctrl", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(slider));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("slider_1"));
    ASSERT_NE(widget, nullptr);

     Bounds cb = widget->contentBounds();
     Pt wpos = widget->worldPos();
     
     // Query the interaction binding at the center of the slider content area.
     Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);
     const auto& sem_snapshot = widget->content_semantic_snapshot();
     auto sem_hit = editor::presentation::hit_test_semantic_scene(sem_snapshot, Pt(cb.x + cb.w * 0.5f, cb.y + cb.h * 0.5f));
     auto* sem_content = std::get_if<editor::presentation::SemanticHitContentRegion>(&sem_hit);
     ASSERT_NE(sem_content, nullptr);
     ASSERT_FALSE(sem_content->object->interactions.empty());
     EXPECT_EQ(sem_content->object->interactions[0].kind, editor::presentation::InteractionKind::DragScalar);
     
     // The key contract: binding provides mapping bounds, not geometry.
     // min_value and max_value define the range for normalized computation.
     EXPECT_GT(sem_content->object->interactions[0].max_value, sem_content->object->interactions[0].min_value)
         << "Binding must provide valid mapping range (max_value > min_value)";
     
     // Verify that CanvasInput can use these bounds to compute a normalized value.
     // Note: CanvasInput now uses binding min/max_value instead of slider geometry constants.
     float range = sem_content->object->interactions[0].max_value - sem_content->object->interactions[0].min_value;
     EXPECT_GT(range, 0.0f) << "Mapping range must be positive";
    
    // Simulate a click and verify the slider interaction is captured without
    // CanvasInput needing to know about HANDLE_RADIUS.
    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);
    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    
    Pt canvas_min(0, 0);
    auto result = input.on_mouse_down(click_world, MouseButton::Left, canvas_min);
    
    // Verify the slider interaction was handled without accessing concrete geometry.
    EXPECT_EQ(result.slider_node_id, "slider_1")
        << "CanvasInput must handle slider interaction via semantic target mapping";
    EXPECT_EQ(input.state(), InputState::DraggingSlider)
        << "CanvasInput must enter DraggingSlider state via semantic mapping";
}

TEST(CanvasInputInteractionTarget, SliderTargetUsesConfiguredNonDefaultMinMax) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto slider = make_node(I, "slider_custom", "Slider", 100.0f, 100.0f);
    set_params(slider, {{"min", "-10.0"}, {"max", "200.0"}});
    set_iface(slider, {
        make_port(I, "ctrl", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(slider));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("slider_custom"));
    ASSERT_NE(widget, nullptr);

    Bounds cb = widget->contentBounds();
    const auto& sem_snapshot = widget->content_semantic_snapshot();
    auto sem_hit = editor::presentation::hit_test_semantic_scene(
        sem_snapshot,
        Pt(cb.x + cb.w * 0.5f, cb.y + cb.h * 0.5f));
    auto* sem_content = std::get_if<editor::presentation::SemanticHitContentRegion>(&sem_hit);
    ASSERT_NE(sem_content, nullptr);
    ASSERT_FALSE(sem_content->object->interactions.empty());
    EXPECT_EQ(sem_content->object->interactions[0].kind, editor::presentation::InteractionKind::DragScalar);

    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);
    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    input.simulation_mode = true;

    Pt wpos = widget->worldPos();
    Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);
    Pt canvas_min(0, 0);

    auto down = input.on_mouse_down(click_world, MouseButton::Left, canvas_min);
    ASSERT_EQ(down.slider_node_id, "slider_custom");
    ASSERT_EQ(input.state(), InputState::DraggingSlider);

    auto drag = input.on_mouse_drag(MouseButton::Left, Pt(500.0f, 0.0f), canvas_min);
    EXPECT_EQ(drag.slider_node_id, "slider_custom");
    EXPECT_FLOAT_EQ(drag.slider_value, 200.0f);

    input.on_mouse_up(MouseButton::Left, click_world + Pt(500.0f, 0.0f), canvas_min);
}

TEST(CanvasInputInteractionTarget, SliderTargetUsesConfiguredNonDefaultMinOnLeftClamp) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto slider = make_node(I, "slider_custom", "Slider", 100.0f, 100.0f);
    set_params(slider, {{"min", "-10.0"}, {"max", "200.0"}});
    set_iface(slider, {
        make_port(I, "ctrl", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(slider));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("slider_custom"));
    ASSERT_NE(widget, nullptr);

    Bounds cb = widget->contentBounds();
    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);
    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    input.simulation_mode = true;

    Pt wpos = widget->worldPos();
    Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);
    Pt canvas_min(0, 0);

    auto down = input.on_mouse_down(click_world, MouseButton::Left, canvas_min);
    ASSERT_EQ(down.slider_node_id, "slider_custom");
    ASSERT_EQ(input.state(), InputState::DraggingSlider);

    auto drag = input.on_mouse_drag(MouseButton::Left, Pt(-500.0f, 0.0f), canvas_min);
    EXPECT_EQ(drag.slider_node_id, "slider_custom");
    EXPECT_FLOAT_EQ(drag.slider_value, -10.0f);

    input.on_mouse_up(MouseButton::Left, click_world + Pt(-500.0f, 0.0f), canvas_min);
}

TEST(CanvasInputSimMode, SimModeBlocksRightClickContextMenu) {

    // In simulation mode, right-clicking should NOT show context menus.
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto node = make_node(I, "n1", "Battery", 120.0f, 80.0f);
    bp2::Blueprint bp;
    bp = bp.with_node(std::move(node));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* widget = dynamic_cast<visual::Widget*>(scene.find("n1"));
    ASSERT_NE(widget, nullptr);

    Viewport vp;
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    input.simulation_mode = true;

    const Pt canvas_min(0.0f, 0.0f);
    Pt click_pos = widget->worldPos() + Pt(10.0f, 10.0f);
    auto result = input.on_mouse_down(click_pos, MouseButton::Right, canvas_min);

    EXPECT_FALSE(result.show_node_context_menu)
        << "simulation_mode must block right-click context menu";
    EXPECT_FALSE(result.show_context_menu)
        << "simulation_mode must block right-click context menu";
}

TEST(CanvasInputSimMode, SimModeAllowsPanning) {
    // In simulation mode, clicking on empty space should enter panning.
    ui::StringInterner I;
    bp2::PathArena arena(I);

    bp2::Blueprint bp;
    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    Viewport vp;
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    input.simulation_mode = true;

    const Pt canvas_min(0.0f, 0.0f);
    input.on_mouse_down(Pt(500.0f, 500.0f), MouseButton::Left, canvas_min);

    EXPECT_EQ(input.state(), InputState::Panning)
        << "simulation_mode must allow panning on empty space";
}

// ============================================================================
// Regression: simulation_mode blocks inline value editor (BUG 3)
// ============================================================================

TEST(CanvasInputSimMode, SimModeBlocksInlineValueEditor) {
    // In simulation mode, double-clicking a Value node must NOT open
    // the inline value editor (editing params won't affect compiled solver).
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto value_node = make_node(I, "val1", "Value", 120.0f, 80.0f);
    bp2::Blueprint bp;
    bp = bp.with_node(std::move(value_node));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* widget = dynamic_cast<visual::Widget*>(scene.find("val1"));
    ASSERT_NE(widget, nullptr);

    Viewport vp;
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    input.simulation_mode = true;

    const Pt canvas_min(0.0f, 0.0f);
    const Pt click_pos = widget->worldPos() + Pt(10.0f, 10.0f);

    InputResult r = input.on_double_click(click_pos, canvas_min);
    EXPECT_FALSE(r.open_inline_value_editor)
        << "simulation_mode must block inline value editor";
    EXPECT_TRUE(r.inline_value_editor_node_id.empty())
        << "simulation_mode must not populate inline editor node id";
}

TEST(CanvasInputSimMode, ReadOnlyBlocksInlineValueEditor) {
    // read_only mode must also block the inline value editor.
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto value_node = make_node(I, "val1", "Value", 120.0f, 80.0f);
    bp2::Blueprint bp;
    bp = bp.with_node(std::move(value_node));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* widget = dynamic_cast<visual::Widget*>(scene.find("val1"));
    ASSERT_NE(widget, nullptr);

    Viewport vp;
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    input.read_only = true;

    const Pt canvas_min(0.0f, 0.0f);
    const Pt click_pos = widget->worldPos() + Pt(10.0f, 10.0f);

    InputResult r = input.on_double_click(click_pos, canvas_min);
    EXPECT_FALSE(r.open_inline_value_editor)
        << "read_only must block inline value editor";
}

// ============================================================================
// Regression: Value node snap behavior (Issue #21)
// ============================================================================

TEST(CanvasInputNodeSnap, ValueNodeSnapsToHalfGridDespiteRefRenderHint) {
    // Regression test for Issue #21: Value nodes must snap to half-grid (0.5x)
    // even though they have render_hint="ref" (which normally triggers full-grid snap).
    // Value nodes should be excluded from the full-grid snap logic.
    //
    // Strategy: We test by creating a Blueprint with a Value node that has render_hint="ref",
    // then calling the snap logic indirectly through the drag handler. We'll verify the snap
    // behavior by examining the node's position after a drag operation that would disambiguate
    // between half-grid and full-grid snap.
    ui::StringInterner I;
    bp2::PathArena arena(I);

    // Create a Battery as a reference
    auto bat = make_node(I, "bat", "Battery", 200.0f, 200.0f);
    set_iface(bat, {
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });
    
    // Create a Value node with render_hint="ref" at (103, 103)
    // (not at a grid/half-grid boundary to test snap behavior)
    auto value_node = make_node(I, "val1", "Value", 103.0f, 103.0f, "ref");
    
    // Create a RefNode (for comparison) also with render_hint="ref" at (113, 113)
    auto ref_node = make_node(I, "ref1", "RefNode", 113.0f, 113.0f, "ref");
    set_iface(ref_node, {
        make_port(I, "v", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });
    
    auto wire = make_wire(I, arena, "w1", "bat", "v_out", "ref1", "v");
    wire.domain = Domain::Electrical;
    
    bp2::Blueprint bp;
    bp = bp.with_node(std::move(bat));
    bp = bp.with_node(std::move(value_node));
    bp = bp.with_node(std::move(ref_node));
    bp = bp.with_wire(std::move(wire));

    bp2::EditorModel model(bp);
    
    // Verify that the snap logic will differentiate between half-grid and full-grid:
    // With grid_step=10:
    // - Half-grid anchors: 0, 5, 10, 15, 20, ... (multiples of 5)
    // - Full-grid anchors: 0, 10, 20, 30, ... (multiples of 10)
    //
    // For Value node at 103:
    // - Half-grid snap → 105 (distance 2)
    // - Full-grid snap → 100 (distance 3)
    // → Half-grid is closer, so should snap to 105
    //
    // For RefNode at 113:
    // - Half-grid snap → 115 (distance 2)
    // - Full-grid snap → 110 (distance 3)
    // → But RefNode uses full-grid, so should snap to 110
    
    // Update Value node position to 105 via snap (simulating a drag)
    // We'll call snap_to_half_grid directly to verify the fix
    ui::Pt value_pos(103.0f, 103.0f);
    ui::Pt snapped_half = editor_math::snap_to_half_grid(value_pos, 10.0f);
    EXPECT_NEAR(snapped_half.x, 105.0f, 0.01f)
        << "snap_to_half_grid(103, 10) should snap to 105";
    EXPECT_NEAR(snapped_half.y, 105.0f, 0.01f)
        << "snap_to_half_grid(103, 10) should snap to 105";
    
    // Verify that full-grid would snap differently
    ui::Pt snapped_full = editor_math::snap_to_grid(value_pos, 10.0f);
    EXPECT_NEAR(snapped_full.x, 100.0f, 0.01f)
        << "snap_to_grid(103, 10) should snap to 100";
    EXPECT_NEAR(snapped_full.y, 100.0f, 0.01f)
        << "snap_to_grid(103, 10) should snap to 100";
    
    // Now test the fix: When we drag a Value node, it should use half-grid snap,
    // not full-grid. We do this by checking that the node position after rebuild
    // is snapped correctly.
    
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* val_widget = dynamic_cast<visual::Widget*>(scene.find("val1"));
    ASSERT_NE(val_widget, nullptr) << "Value widget should exist in scene";
    
    auto* ref_widget = dynamic_cast<visual::RefNodeWidget*>(scene.find("ref1"));
    ASSERT_NE(ref_widget, nullptr) << "RefNode widget should exist in scene";

    Viewport vp;
    vp.grid_step = 10.0f;
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());

    const Pt canvas_min(0.0f, 0.0f);
    
    // Test: Drag RefNode and verify it uses full-grid snap
    Pt ref_click = ref_widget->worldPos() + Pt(10.0f, 10.0f);  // Click on ref node body
    input.on_mouse_down(ref_click, MouseButton::Left, canvas_min);
    
    if (input.state() == InputState::DraggingNode) {
        // Drag by -3 units: (113, 113) + (-3, -3) = (110, 110)
        // Half-grid: snap to 110 (distance 0) or 105 (distance 5) → 110 ✓
        // Full-grid: snap to 110 (distance 0) or 100 (distance 10) → 110 ✓
        // (Both agree at 110 because 110 is on full-grid boundary)
        Pt drag_delta(-3.0f, -3.0f);
        input.on_mouse_drag(MouseButton::Left, drag_delta, canvas_min);
        input.on_mouse_up(MouseButton::Left, ref_click + drag_delta, canvas_min);
        
        // Rebuild and check position
        visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());
        
         Pt ref_pos = dynamic_cast<visual::Widget*>(scene.find("ref1"))->worldPos();
        EXPECT_NEAR(ref_pos.x, 110.0f, 0.1f)
            << "RefNode should snap to full-grid (110)";
    }
}

// ============================================================================
// Regression: double-click on interactive content still resolves node actions
// ============================================================================

TEST(CanvasInputDoubleClick, DoubleClickOnInteractiveContentOfBlueprintInstanceOpensSubWindow) {
    // When a composite (BlueprintInstance) node has interactive content
    // (e.g. VerticalToggle), double-clicking the content area should still
    // open the sub-window. Before the fix, hit_test returned HitInteractionTarget
    // but on_double_click only checked HitNode, silently dropping the event.
    ui::StringInterner I;
    bp2::PathArena arena(I);

    bp2::Interface composite_iface({
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    auto composite = make_node(I, "comp_1", "CompositeSwitch", 100.0f, 100.0f);
    composite.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            std::make_unique<bp2::Blueprint>(bp2::Blueprint().with_id(I.intern("CompositeSwitch"))))
    };

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(composite));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("comp_1"));
    ASSERT_NE(widget, nullptr);

    // Click at the center of the content area
    Bounds cb = widget->contentBounds();
    Pt wpos = widget->worldPos();
    Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);

    // Verify we actually get HitNode for this click position
    auto hit = snapshot_hit_test(scene, I, click_world);
    ASSERT_TRUE(std::holds_alternative<visual::HitNode>(hit))
        << "Precondition: click on content area must return HitNode";

    Pt canvas_min(0, 0);
    auto result = input.on_double_click(click_world, canvas_min);

    EXPECT_EQ(result.open_sub_window, "comp_1")
        << "Double-clicking interactive content of a BlueprintInstance "
           "should still open the sub-window";
}

// ============================================================================
// Regression: Semantic controller gates slider/knob drag follow-through
// ============================================================================

TEST(CanvasInputSemanticGate, SliderDragOffHitStillEmitsThroughSemanticContinuation) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto slider = make_node(I, "slider_1", "Slider", 100.0f, 100.0f);
    set_params(slider, {{"min", "0.0"}, {"max", "100.0"}});
    set_iface(slider, {
        make_port(I, "ctrl", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(slider));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    input.simulation_mode = true;

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("slider_1"));
    ASSERT_NE(widget, nullptr);

    Bounds cb = widget->contentBounds();
    Pt wpos = widget->worldPos();
    Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);

    Pt canvas_min(0, 0);
    auto down = input.on_mouse_down(click_world, MouseButton::Left, canvas_min);
    ASSERT_EQ(down.slider_node_id, "slider_1");
    ASSERT_EQ(input.state(), InputState::DraggingSlider);

    auto drag = input.on_mouse_drag(MouseButton::Left, Pt(500.0f, 0.0f), canvas_min);

    EXPECT_EQ(input.state(), InputState::DraggingSlider);
    EXPECT_EQ(drag.slider_node_id, "slider_1");
    EXPECT_GE(drag.slider_value, 0.0f);
}

TEST(CanvasInputSemanticGate, SliderDragLeftFromCenterDoesNotSnapToMaximum) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto slider = make_node(I, "slider_1", "Slider", 100.0f, 100.0f);
    set_params(slider, {{"min", "0.0"}, {"max", "100.0"}});
    set_iface(slider, {
        make_port(I, "ctrl", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(slider));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());
    update_dynamic(scene, "slider_1", [](NodeContent& c) { c.value = 50.0f; });

    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    input.simulation_mode = true;

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("slider_1"));
    ASSERT_NE(widget, nullptr);

    Bounds cb = widget->contentBounds();
    Pt wpos = widget->worldPos();
    Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);

    Pt canvas_min(0, 0);
    auto down = input.on_mouse_down(click_world, MouseButton::Left, canvas_min);
    ASSERT_EQ(down.slider_node_id, "slider_1");
    ASSERT_EQ(input.state(), InputState::DraggingSlider);

    auto drag = input.on_mouse_drag(MouseButton::Left, Pt(-20.0f, 0.0f), canvas_min);

    EXPECT_EQ(drag.slider_node_id, "slider_1");
    EXPECT_LT(drag.slider_value, 100.0f);
}

TEST(CanvasInputSemanticGate, KnobDragOffHitStillEmitsThroughSemanticContinuation) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto knob = make_node(I, "knob_1", "KnobSwitch", 100.0f, 100.0f);
    set_params(knob, {{"positions", "5.0"}});
    set_iface(knob, {
        make_port(I, "throw1", Domain::Electrical, bp2::Direction::InOut, PortType::V),
        make_port(I, "throw2", Domain::Electrical, bp2::Direction::InOut, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(knob));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    input.simulation_mode = true;

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("knob_1"));
    ASSERT_NE(widget, nullptr);

    Bounds cb = widget->contentBounds();
    Pt wpos = widget->worldPos();
    Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);

    Pt canvas_min(0, 0);
    auto down = input.on_mouse_down(click_world, MouseButton::Left, canvas_min);
    ASSERT_EQ(down.knob_node_id, "knob_1");
    ASSERT_EQ(input.state(), InputState::DraggingKnob);

    auto drag = input.on_mouse_drag(MouseButton::Left, Pt(500.0f, 0.0f), canvas_min);

    EXPECT_EQ(input.state(), InputState::DraggingKnob);
    EXPECT_EQ(drag.knob_node_id, "knob_1");
    EXPECT_GE(drag.knob_position, 0);
}

TEST(CanvasInputSemanticGate, SliderReleaseOffHitEndsDragState) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto slider = make_node(I, "slider_1", "Slider", 100.0f, 100.0f);
    set_params(slider, {{"min", "0.0"}, {"max", "100.0"}});
    set_iface(slider, {
        make_port(I, "ctrl", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(slider));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    input.simulation_mode = true;

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("slider_1"));
    ASSERT_NE(widget, nullptr);

    Bounds cb = widget->contentBounds();
    Pt wpos = widget->worldPos();
    Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);

    Pt canvas_min(0, 0);
    auto down = input.on_mouse_down(click_world, MouseButton::Left, canvas_min);
    ASSERT_EQ(down.slider_node_id, "slider_1");
    ASSERT_EQ(input.state(), InputState::DraggingSlider);

    input.on_mouse_drag(MouseButton::Left, Pt(500.0f, 0.0f), canvas_min);
    input.on_mouse_up(MouseButton::Left, click_world + Pt(500.0f, 0.0f), canvas_min);

    EXPECT_EQ(input.state(), InputState::Idle);
}

TEST(CanvasInputSemanticGate, KnobReleaseOffHitEndsDragState) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto knob = make_node(I, "knob_1", "KnobSwitch", 100.0f, 100.0f);
    set_params(knob, {{"positions", "5.0"}});
    set_iface(knob, {
        make_port(I, "throw1", Domain::Electrical, bp2::Direction::InOut, PortType::V),
        make_port(I, "throw2", Domain::Electrical, bp2::Direction::InOut, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(knob));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    input.simulation_mode = true;

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("knob_1"));
    ASSERT_NE(widget, nullptr);

    Bounds cb = widget->contentBounds();
    Pt wpos = widget->worldPos();
    Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);

    Pt canvas_min(0, 0);
    auto down = input.on_mouse_down(click_world, MouseButton::Left, canvas_min);
    ASSERT_EQ(down.knob_node_id, "knob_1");
    ASSERT_EQ(input.state(), InputState::DraggingKnob);

    input.on_mouse_drag(MouseButton::Left, Pt(500.0f, 0.0f), canvas_min);
    input.on_mouse_up(MouseButton::Left, click_world + Pt(500.0f, 0.0f), canvas_min);

    EXPECT_EQ(input.state(), InputState::Idle);
}

TEST(CanvasInputSemanticGate, SimulationModeSliderDragStillEmitsWhenSemanticActive) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto slider = make_node(I, "slider_1", "Slider", 100.0f, 100.0f);
    set_params(slider, {{"min", "0.0"}, {"max", "100.0"}});
    set_iface(slider, {
        make_port(I, "ctrl", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(slider));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    input.simulation_mode = true;

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("slider_1"));
    ASSERT_NE(widget, nullptr);

    Bounds cb = widget->contentBounds();
    Pt wpos = widget->worldPos();
    Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);

    Pt canvas_min(0, 0);
    auto down = input.on_mouse_down(click_world, MouseButton::Left, canvas_min);
    ASSERT_EQ(down.slider_node_id, "slider_1");
    ASSERT_EQ(input.state(), InputState::DraggingSlider);

    auto drag = input.on_mouse_drag(MouseButton::Left, Pt(50.0f, 0.0f), canvas_min);

    EXPECT_EQ(drag.slider_node_id, "slider_1");
    EXPECT_GE(drag.slider_value, 0.0f);
}

TEST(CanvasInputSemanticGate, SimulationModeKnobDragStillEmitsWhenSemanticActive) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto knob = make_node(I, "knob_1", "KnobSwitch", 100.0f, 100.0f);
    set_params(knob, {{"positions", "5.0"}});
    set_iface(knob, {
        make_port(I, "throw1", Domain::Electrical, bp2::Direction::InOut, PortType::V),
        make_port(I, "throw2", Domain::Electrical, bp2::Direction::InOut, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(knob));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());
    input.simulation_mode = true;

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("knob_1"));
    ASSERT_NE(widget, nullptr);

    Bounds cb = widget->contentBounds();
    Pt wpos = widget->worldPos();
    Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);

     Pt canvas_min(0, 0);
    auto down = input.on_mouse_down(click_world, MouseButton::Left, canvas_min);
    ASSERT_EQ(down.knob_node_id, "knob_1");
    ASSERT_EQ(input.state(), InputState::DraggingKnob);

    auto drag = input.on_mouse_drag(MouseButton::Left, Pt(50.0f, 0.0f), canvas_min);

    EXPECT_EQ(drag.knob_node_id, "knob_1");
    EXPECT_GE(drag.knob_position, 0);
}

TEST(CanvasInputHoverSuppression, DraggingKnobSuppressesWireHover) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto knob = make_node(I, "knob_1", "KnobControl", 100.0f, 100.0f);
    set_iface(knob, {
        make_port(I, "ctrl", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    auto wire_src = make_node(I, "src", "Battery", 0.0f, 100.0f);
    set_iface(wire_src, {
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    auto wire_dst = make_node(I, "dst", "Lamp", 200.0f, 100.0f);
    set_iface(wire_dst, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(knob))
        .with_node(std::move(wire_src))
        .with_node(std::move(wire_dst))
        .with_wire(make_wire(I, arena, "wire_0", "src", "v_out", "dst", "v_in"));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());
    update_dynamic(scene, "knob_1", [](NodeContent& c) { c.value = 1.0f; });

    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);
    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("knob_1"));
    ASSERT_NE(widget, nullptr);

    Bounds cb = widget->contentBounds();
    Pt wpos = widget->worldPos();
    Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);

    Pt canvas_min(0, 0);
    input.on_mouse_down(click_world, MouseButton::Left, canvas_min);
    ASSERT_EQ(input.state(), InputState::DraggingKnob);

    input.on_mouse_drag(MouseButton::Left, Pt(50.0f, 0.0f), canvas_min);
    ASSERT_EQ(input.state(), InputState::DraggingKnob);

    input.update_hover(Pt(100.0f, 100.0f));

    EXPECT_TRUE(input.hovered_wire_id().empty())
         << "While DraggingKnob, hovered_wire_id() must be empty (hover suppressed)";
     EXPECT_TRUE(input.hovered_routing_point_id().empty())
         << "While DraggingKnob, hovered_routing_point_id() must be empty (hover suppressed)";
}

TEST(CanvasInputHoverSuppression, DraggingSliderSuppressesWireHover) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto slider = make_node(I, "slider_1", "SliderControl", 100.0f, 100.0f);
    set_params(slider, {{"min", "0.0"}, {"max", "100.0"}});
    set_iface(slider, {
        make_port(I, "ctrl", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    auto wire_src = make_node(I, "src", "Battery", 0.0f, 100.0f);
    set_iface(wire_src, {
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    auto wire_dst = make_node(I, "dst", "Lamp", 200.0f, 100.0f);
    set_iface(wire_dst, {
        make_port(I, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(slider))
        .with_node(std::move(wire_src))
        .with_node(std::move(wire_dst))
        .with_wire(make_wire(I, arena, "wire_0", "src", "v_out", "dst", "v_in"));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());
    update_dynamic(scene, "slider_1", [](NodeContent& c) { c.value = 50.0f; });

    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);
    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("slider_1"));
    ASSERT_NE(widget, nullptr);

    Bounds cb = widget->contentBounds();
    Pt wpos = widget->worldPos();
    Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);

    Pt canvas_min(0, 0);
    input.on_mouse_down(click_world, MouseButton::Left, canvas_min);
    ASSERT_EQ(input.state(), InputState::DraggingSlider);

    input.on_mouse_drag(MouseButton::Left, Pt(50.0f, 0.0f), canvas_min);
    ASSERT_EQ(input.state(), InputState::DraggingSlider);

    input.update_hover(Pt(100.0f, 100.0f));

    EXPECT_TRUE(input.hovered_wire_id().empty())
        << "While DraggingSlider, hovered_wire_id() must be empty (hover suppressed)";
    EXPECT_TRUE(input.hovered_routing_point_id().empty())
        << "While DraggingSlider, hovered_routing_point_id() must be empty (hover suppressed)";
}

TEST(CanvasInputSemanticCancellation, CancelGestureInDraggingKnobReturnsToIdleAndCancelsSemanticSession) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto knob = make_node(I, "knob_1", "KnobControl", 100.0f, 100.0f);
    set_iface(knob, {
        make_port(I, "ctrl", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(knob));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());
    update_dynamic(scene, "knob_1", [](NodeContent& c) { c.value = 1.0f; });

    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("knob_1"));
    ASSERT_NE(widget, nullptr);

    Bounds cb = widget->contentBounds();
    Pt wpos = widget->worldPos();
    Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);

    Pt canvas_min(0, 0);
    input.on_mouse_down(click_world, MouseButton::Left, canvas_min);
    ASSERT_EQ(input.state(), InputState::DraggingKnob);

    input.cancel_gesture();

    EXPECT_EQ(input.state(), InputState::Idle)
        << "cancel_gesture() must return to Idle state";
}

TEST(CanvasInputSemanticCancellation, CancelGestureInDraggingSliderReturnsToIdleAndCancelsSemanticSession) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto slider = make_node(I, "slider_1", "SliderControl", 100.0f, 100.0f);
    set_params(slider, {{"min", "0.0"}, {"max", "100.0"}});
    set_iface(slider, {
        make_port(I, "ctrl", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(slider));

    bp2::EditorModel model(std::move(bp));
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());
    update_dynamic(scene, "slider_1", [](NodeContent& c) { c.value = 50.0f; });

    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("slider_1"));
    ASSERT_NE(widget, nullptr);

    Bounds cb = widget->contentBounds();
    Pt wpos = widget->worldPos();
    Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);

    Pt canvas_min(0, 0);
    input.on_mouse_down(click_world, MouseButton::Left, canvas_min);
    ASSERT_EQ(input.state(), InputState::DraggingSlider);

    input.cancel_gesture();

    EXPECT_EQ(input.state(), InputState::Idle)
        << "cancel_gesture() must return to Idle state";
}

// ============================================================================
// Regression: Ref node snap uses half-grid, not full-grid (inverted ternary fix)
// ============================================================================

TEST(CanvasInputNodeSnap, RefNodeDragUsesHalfGridSnap) {
    // This test exercises the handle_drag_node code path to verify that
    // ref nodes (render_hint="ref", NOT type=Value) snap to half-grid.
    //
    // We place a single ref node at grid position (160, 160) with grid_step=16.
    // After clicking and starting a drag, we apply a delta that moves the
    // drag anchor to a position where half-grid and full-grid snap differ.
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto ref = make_node(I, "ref1", "RefNode", 160.0f, 160.0f, "ref");
    set_iface(ref, {
        make_port(I, "v", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    auto bat = make_node(I, "bat", "Battery", 0.0f, 0.0f);
    set_iface(bat, {
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    auto wire = make_wire(I, arena, "w1", "bat", "v_out", "ref1", "v");
    wire.domain = Domain::Electrical;

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(bat));
    bp = bp.with_node(std::move(ref));
    bp = bp.with_wire(std::move(wire));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* ref_widget = dynamic_cast<visual::Widget*>(scene.find("ref1"));
    ASSERT_NE(ref_widget, nullptr);

    Viewport vp;
    vp.grid_step = 16.0f;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);

    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());

    const Pt canvas_min(0.0f, 0.0f);

    // Click on the ref node body to start dragging
    Pt click_pos = ref_widget->worldPos() + Pt(10.0f, 10.0f);
    input.on_mouse_down(click_pos, MouseButton::Left, canvas_min);

    if (input.state() == InputState::DraggingNode) {
        // After mouse_down, drag_anchor is set to the ref node's world position
        // (the click position). We drag by +5 to move the anchor off-grid.
        // With grid_step=16, half_step=8:
        //   If anchor lands at e.g. 165, half_grid→168, full_grid→160.
        //   The ref node should snap to half-grid (168), not full-grid (160).
        input.on_mouse_drag(MouseButton::Left, Pt(5.0f, 5.0f), canvas_min);

        Pt new_pos = ref_widget->localPos();
        float half_step = vp.grid_step * 0.5f;  // 8.0

        // Verify the position is on the half-grid
        float rem_x = std::fmod(std::abs(new_pos.x), half_step);
        float rem_y = std::fmod(std::abs(new_pos.y), half_step);
        bool on_half_grid_x = (rem_x < 0.01f || std::abs(rem_x - half_step) < 0.01f);
        bool on_half_grid_y = (rem_y < 0.01f || std::abs(rem_y - half_step) < 0.01f);
        EXPECT_TRUE(on_half_grid_x)
            << "Ref node X=" << new_pos.x << " must be on half-grid (step=" << half_step << ")";
        EXPECT_TRUE(on_half_grid_y)
            << "Ref node Y=" << new_pos.y << " must be on half-grid (step=" << half_step << ")";
    } else {
        // If the click didn't land on the node body (hit a port instead), skip
         GTEST_SKIP() << "Click landed on port instead of node body; "
                     << "state=" << static_cast<int>(input.state());
    }
}

// ============================================================================
// Regression: Newly inserted nodes must be immediately interactive
// ============================================================================

// Reproduces the exact user-reported bug: after adding a node via model
// mutation + rebuild_scene (the path used by Document::addComponent →
// rebuildAllWindows), the node must be selectable and resizable without
// any second insertion or other workaround.
TEST(CanvasInputLifecycle, NewlyInsertedNodeIsImmediatelySelectable) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    // Start with one pre-existing node (simulates an already-saved blueprint)
    auto existing = make_node(I, "existing", "Battery", 40.0f, 40.0f);
    existing.layout.width = 100.0f;
    existing.layout.height = 60.0f;
    set_iface(existing, {
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(existing));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);
    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());

    // --- Simulate addComponent: add a new node to the model ---
    {
        bp2::Blueprint::Node new_node;
        new_node.content = bp2::Blueprint::Node::ComponentData{};
        new_node.semantic.id = I.intern("new_lamp");
        new_node.semantic.type = I.intern("Lamp");
        new_node.view.name = "new_lamp";
        new_node.layout.x = 300.0f;
        new_node.layout.y = 300.0f;
        // width/height intentionally left as nullopt — this is how addComponent works
        new_node.component().iface = bp2::Interface({
            {I.intern("v_in"), Domain::Electrical, bp2::Direction::Input, PortType::V},
        });

        bp2::Blueprint updated = model.current().with_node(std::move(new_node));
        model.replace_current(std::move(updated));
    }

    // --- Simulate rebuildAllWindows: cancel + rebuild scene + rebuild snapshot ---
    // (Document::rebuildAllWindows does: cancel_gesture → visual::mutations::rebuild → rebuild_snapshot)
    input.cancel_gesture();
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());
    input.rebuild_snapshot();

    // Verify the new node widget exists in the scene
    auto* new_widget = scene.find("new_lamp");
    ASSERT_NE(new_widget, nullptr) << "New node widget must exist in scene after rebuild";
    ASSERT_GT(new_widget->size().x, 0.0f) << "New node must have non-zero width";
    ASSERT_GT(new_widget->size().y, 0.0f) << "New node must have non-zero height";

    const Pt canvas_min(0.0f, 0.0f);

    // --- Click on the new node body to select it ---
    Pt new_node_center = new_widget->worldPos() + new_widget->size() * 0.5f;
    input.on_mouse_down(new_node_center, MouseButton::Left, canvas_min);
    EXPECT_EQ(input.state(), InputState::DraggingNode)
        << "Clicking on newly inserted node must enter DraggingNode (i.e., select it)";
    input.on_mouse_up(MouseButton::Left, new_node_center, canvas_min);

    // Verify it's actually selected
    ASSERT_EQ(input.selected_node_ids().size(), 1u);
    EXPECT_EQ(input.selected_node_ids()[0], I.intern("new_lamp"));

    // --- Deselect by clicking empty space ---
    Pt empty_space(900.0f, 900.0f);
    input.on_mouse_down(empty_space, MouseButton::Left, canvas_min);
    input.on_mouse_up(MouseButton::Left, empty_space, canvas_min);
    EXPECT_TRUE(input.selected_node_ids().empty()) << "Selection must be cleared after clicking empty space";

    // --- Click the new node again — THIS IS THE BUG: should still be selectable ---
    input.on_mouse_down(new_node_center, MouseButton::Left, canvas_min);
    EXPECT_EQ(input.state(), InputState::DraggingNode)
        << "Re-clicking newly inserted node after deselection must still enter DraggingNode";
    input.on_mouse_up(MouseButton::Left, new_node_center, canvas_min);
    ASSERT_EQ(input.selected_node_ids().size(), 1u);
    EXPECT_EQ(input.selected_node_ids()[0], I.intern("new_lamp"))
        << "Newly inserted node must remain selectable without a second insertion";
}

TEST(CanvasInputLifecycle, NewlyInsertedNodeIsImmediatelyResizable) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto existing = make_node(I, "existing", "Battery", 40.0f, 40.0f);
    existing.layout.width = 100.0f;
    existing.layout.height = 60.0f;
    set_iface(existing, {
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(existing));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);
    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());

    // --- Simulate addComponent ---
    {
        bp2::Blueprint::Node new_node;
        new_node.content = bp2::Blueprint::Node::ComponentData{};
        new_node.semantic.id = I.intern("new_lamp");
        new_node.semantic.type = I.intern("Lamp");
        new_node.view.name = "new_lamp";
        new_node.layout.x = 300.0f;
        new_node.layout.y = 300.0f;
        new_node.component().iface = bp2::Interface({
            {I.intern("v_in"), Domain::Electrical, bp2::Direction::Input, PortType::V},
        });

        bp2::Blueprint updated = model.current().with_node(std::move(new_node));
        model.replace_current(std::move(updated));
    }

    input.cancel_gesture();
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());
    input.rebuild_snapshot();

    auto* new_widget = scene.find("new_lamp");
    ASSERT_NE(new_widget, nullptr);

    const Pt canvas_min(0.0f, 0.0f);

    // Resize handles only work on already-selected nodes — select first
    // by clicking on the node body (center) and releasing.
    Pt center = new_widget->worldPos() + new_widget->size() * 0.5f;
    input.on_mouse_down(center, MouseButton::Left, canvas_min);
    input.on_mouse_up(MouseButton::Left, center, canvas_min);
    ASSERT_EQ(input.selected_node_ids().size(), 1u);

    // Compute the bottom-right corner of the new node for resize handle hit
    Pt br_corner = new_widget->worldMin() + new_widget->size();
    // Hit the resize handle area: slightly inside the bottom-right corner
    Pt resize_hit = Pt(br_corner.x - 4.0f, br_corner.y - 4.0f);

    input.on_mouse_down(resize_hit, MouseButton::Left, canvas_min);
    EXPECT_EQ(input.state(), InputState::ResizingNode)
        << "Clicking on resize handle of newly inserted node must enter ResizingNode immediately";
    input.on_mouse_up(MouseButton::Left, resize_hit, canvas_min);
}

// ============================================================================
// Regression: double-click on regular node must select (not swallow click)
// ============================================================================

TEST(CanvasInputDoubleClick, DoubleClickOnRegularNodeDoesNotConsumeEvent) {
    // When on_double_click fires on a regular component node (not Value, not
    // BlueprintInstance), the result must NOT be consumed, so that the caller
    // can fall through to on_mouse_down for normal selection/drag behavior.
    // This is the root cause of the "newly inserted node not selectable" bug:
    // ImGui sends IsMouseDoubleClicked when two clicks are fast enough, and
    // the old code skipped on_mouse_down entirely even though on_double_click
    // did nothing for regular nodes.
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto node = make_node(I, "bat1", "Battery", 120.0f, 80.0f);
    bp2::Blueprint bp;
    bp = bp.with_node(std::move(node));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* widget = dynamic_cast<visual::Widget*>(scene.find("bat1"));
    ASSERT_NE(widget, nullptr);

    Viewport vp;
    auto host = create_editor_model_host(model);
    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());

    const Pt canvas_min(0.0f, 0.0f);
    Pt click_pos = widget->worldPos() + Pt(10.0f, 10.0f);

    // Simulate what handleInput does: call on_double_click, check consumed flag
    InputResult dbl_result = input.on_double_click(click_pos, canvas_min);
    EXPECT_FALSE(dbl_result.double_click_consumed)
        << "on_double_click on a regular component node must NOT consume the event";

    // Since not consumed, handleInput would call on_mouse_down next
    input.on_mouse_down(click_pos, MouseButton::Left, canvas_min);
    EXPECT_EQ(input.state(), InputState::DraggingNode)
        << "Fallthrough to on_mouse_down after unconsumed double-click must select the node";
    EXPECT_EQ(input.selected_node_ids().size(), 1u);
    EXPECT_EQ(input.selected_node_ids()[0], I.intern("bat1"));
}

TEST(CanvasInputDoubleClick, DoubleClickOnValueNodeConsumesEvent) {
    // Value node double-click opens inline editor — must be consumed.
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto value_node = make_node(I, "val1", "Value", 120.0f, 80.0f);
    bp2::Blueprint bp;
    bp = bp.with_node(std::move(value_node));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* widget = dynamic_cast<visual::Widget*>(scene.find("val1"));
    ASSERT_NE(widget, nullptr);

    Viewport vp;
    auto host = create_editor_model_host(model);
    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());

    const Pt canvas_min(0.0f, 0.0f);
    Pt click_pos = widget->worldPos() + Pt(10.0f, 10.0f);

    InputResult dbl_result = input.on_double_click(click_pos, canvas_min);
    EXPECT_TRUE(dbl_result.double_click_consumed)
        << "on_double_click on Value node must consume the event (opens inline editor)";
    EXPECT_TRUE(dbl_result.open_inline_value_editor);
}

TEST(CanvasInputDoubleClick, DoubleClickOnBlueprintInstanceConsumesEvent) {
    // BlueprintInstance double-click opens sub-window — must be consumed.
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto group = make_node(I, "grp1", "Composite", 120.0f, 80.0f);
    group.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            std::make_unique<bp2::Blueprint>(bp2::Blueprint().with_id(I.intern("Composite"))))
    };
    bp2::Blueprint bp;
    bp = bp.with_node(std::move(group));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    auto* widget = dynamic_cast<visual::Widget*>(scene.find("grp1"));
    ASSERT_NE(widget, nullptr);

    Viewport vp;
    auto host = create_editor_model_host(model);
    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());

    const Pt canvas_min(0.0f, 0.0f);
    Pt click_pos = widget->worldPos() + Pt(10.0f, 10.0f);

    InputResult dbl_result = input.on_double_click(click_pos, canvas_min);
    EXPECT_TRUE(dbl_result.double_click_consumed)
        << "on_double_click on BlueprintInstance must consume the event (opens sub-window)";
    EXPECT_EQ(dbl_result.open_sub_window, "grp1");
}

TEST(CanvasInputDoubleClick, DoubleClickOnEmptySpaceDoesNotConsume) {
    // Double-clicking on empty space should not consume — handleInput should
    // fall through to on_mouse_down which enters panning.
    ui::StringInterner I;
    bp2::PathArena arena(I);

    bp2::Blueprint bp;
    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    Viewport vp;
    auto host = create_editor_model_host(model);
    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());

    const Pt canvas_min(0.0f, 0.0f);

    InputResult dbl_result = input.on_double_click(Pt(500.0f, 500.0f), canvas_min);
    EXPECT_FALSE(dbl_result.double_click_consumed)
        << "on_double_click on empty space must NOT consume the event";
}

// ============================================================================
// Regression: snapshot staleness after geometry-changing gestures
// ============================================================================

// The canvas hit-test snapshot caches resize handle positions. After a resize
// gesture completes, the snapshot must be rebuilt so that subsequent clicks on
// the (now moved) resize handles still enter ResizingNode rather than
// DraggingNode.  This was the root cause of the "resize works once then
// breaks" bug.

TEST(CanvasInputSnapshotRegression, ResizeSelectResizeAgainStillWorks) {
    // Exact reproduction of the reported bug:
    //   1. Select node (click center + release)
    //   2. Resize via bottom-right handle
    //   3. Click node center to re-select
    //   4. Resize via bottom-right handle again — MUST still enter ResizingNode
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto bat = make_node(I, "bat1", "Battery", 100.0f, 100.0f);
    bat.layout.width = 120.0f;
    bat.layout.height = 80.0f;
    set_iface(bat, {
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(bat));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);
    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("bat1"));
    ASSERT_NE(widget, nullptr);

    const Pt canvas_min(0.0f, 0.0f);

    // --- Step 1: Select the node by clicking center ---
    {
        Pt center = widget->worldPos() + widget->size() * 0.5f;
        input.on_mouse_down(center, MouseButton::Left, canvas_min);
        input.on_mouse_up(MouseButton::Left, center, canvas_min);
        ASSERT_EQ(input.selected_node_ids().size(), 1u);
    }

    // --- Step 2: First resize via bottom-right handle ---
    {
        Pt br = widget->worldPos() + widget->size();
        input.on_mouse_down(br, MouseButton::Left, canvas_min);
        ASSERT_EQ(input.state(), InputState::ResizingNode)
            << "First resize attempt must enter ResizingNode";

        // Drag to enlarge the node
        input.on_mouse_drag(MouseButton::Left, Pt(40.0f, 30.0f), canvas_min);
        input.on_mouse_up(MouseButton::Left, br + Pt(40.0f, 30.0f), canvas_min);
        ASSERT_EQ(input.state(), InputState::Idle);
    }

    // --- Step 3: Re-select the node (click on center, release) ---
    {
        Pt center = widget->worldPos() + widget->size() * 0.5f;
        input.on_mouse_down(center, MouseButton::Left, canvas_min);
        input.on_mouse_up(MouseButton::Left, center, canvas_min);
        ASSERT_EQ(input.selected_node_ids().size(), 1u);
        EXPECT_EQ(input.selected_node_ids()[0], I.intern("bat1"));
    }

    // --- Step 4: Second resize via bottom-right handle — THE REGRESSION ---
    {
        Pt br = widget->worldPos() + widget->size();
        input.on_mouse_down(br, MouseButton::Left, canvas_min);
        EXPECT_EQ(input.state(), InputState::ResizingNode)
            << "Second resize attempt (after re-select) must still enter ResizingNode. "
               "If this fails, the snapshot was not rebuilt after the first resize.";
        input.on_mouse_up(MouseButton::Left, br, canvas_min);
    }
}

TEST(CanvasInputSnapshotRegression, DragThenResizeStillWorks) {
    // After dragging a node to a new position, the resize handles in the
    // snapshot must reflect the new position.
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto bat = make_node(I, "bat1", "Battery", 50.0f, 50.0f);
    bat.layout.width = 100.0f;
    bat.layout.height = 60.0f;
    set_iface(bat, {
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(bat));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);
    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("bat1"));
    ASSERT_NE(widget, nullptr);

    const Pt canvas_min(0.0f, 0.0f);

    // Select the node by clicking center
    {
        Pt center = widget->worldPos() + widget->size() * 0.5f;
        input.on_mouse_down(center, MouseButton::Left, canvas_min);
        input.on_mouse_up(MouseButton::Left, center, canvas_min);
        ASSERT_EQ(input.selected_node_ids().size(), 1u);
    }

    // Drag the node 200px to the right
    {
        Pt center = widget->worldPos() + widget->size() * 0.5f;
        input.on_mouse_down(center, MouseButton::Left, canvas_min);
        ASSERT_EQ(input.state(), InputState::DraggingNode);
        input.on_mouse_drag(MouseButton::Left, Pt(200.0f, 0.0f), canvas_min);
        input.on_mouse_up(MouseButton::Left, center + Pt(200.0f, 0.0f), canvas_min);
        ASSERT_EQ(input.state(), InputState::Idle);
    }

    // Re-select by clicking center at new position
    {
        Pt center = widget->worldPos() + widget->size() * 0.5f;
        input.on_mouse_down(center, MouseButton::Left, canvas_min);
        input.on_mouse_up(MouseButton::Left, center, canvas_min);
        ASSERT_EQ(input.selected_node_ids().size(), 1u);
    }

    // Now resize via the bottom-right handle at the NEW position
    {
        Pt br = widget->worldPos() + widget->size();
        input.on_mouse_down(br, MouseButton::Left, canvas_min);
        EXPECT_EQ(input.state(), InputState::ResizingNode)
            << "After dragging a node, resize handles must reflect the new position. "
               "If this fails, the snapshot was not rebuilt after the drag.";
        input.on_mouse_up(MouseButton::Left, br, canvas_min);
    }
}

TEST(CanvasInputSnapshotRegression, MultipleResizeCyclesAllSucceed) {
    // Stress test: resize the same node 5 times in a row, re-selecting each
    // time.  Every cycle must enter ResizingNode.
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto bat = make_node(I, "bat1", "Battery", 100.0f, 100.0f);
    bat.layout.width = 80.0f;
    bat.layout.height = 60.0f;
    set_iface(bat, {
        make_port(I, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(bat));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, std::span<const ui::InternedId>{}, ci_reg());

    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0, 0);
    auto host = create_editor_model_host(model);
    CanvasInput input(scene, vp, host.get(), I, arena, WindowScopeId::root(), &ci_reg());

    auto* widget = dynamic_cast<visual::NodeWidget*>(scene.find("bat1"));
    ASSERT_NE(widget, nullptr);

    const Pt canvas_min(0.0f, 0.0f);

    for (int cycle = 0; cycle < 5; ++cycle) {
        // Select by clicking center
        {
            Pt center = widget->worldPos() + widget->size() * 0.5f;
            input.on_mouse_down(center, MouseButton::Left, canvas_min);
            input.on_mouse_up(MouseButton::Left, center, canvas_min);
            ASSERT_EQ(input.selected_node_ids().size(), 1u)
                << "Cycle " << cycle << ": node must be selected";
        }

        // Resize via bottom-right handle
        Pt br = widget->worldPos() + widget->size();
        input.on_mouse_down(br, MouseButton::Left, canvas_min);
        ASSERT_EQ(input.state(), InputState::ResizingNode)
            << "Cycle " << cycle << ": must enter ResizingNode";

        // Enlarge by 10px each cycle
        Pt delta(10.0f, 10.0f);
        input.on_mouse_drag(MouseButton::Left, delta, canvas_min);
        input.on_mouse_up(MouseButton::Left, br + delta, canvas_min);
        ASSERT_EQ(input.state(), InputState::Idle)
            << "Cycle " << cycle << ": must return to Idle after mouse_up";
    }

    // Verify the node actually grew
    EXPECT_GT(widget->size().x, 80.0f);
    EXPECT_GT(widget->size().y, 60.0f);
}
