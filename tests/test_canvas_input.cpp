#include <gtest/gtest.h>

#include "editor/input/canvas_input.h"
#include "editor/viewport/viewport.h"
#include "editor/visual/scene.h"
#include "editor/visual/scene_mutations.h"
#include "editor/visual/scene_hittest.h"
#include "editor/visual/node/bus_node_widget.h"
#include "editor/visual/node/visual_node.h"
#include "editor/visual/port/visual_port.h"
#include "editor/visual/wire/wire.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "blueprint_v2/path/path.h"

namespace {

static bp2::Blueprint::Node make_node(ui::StringInterner& I,
                                      const char* id,
                                      const char* type,
                                      float x,
                                      float y,
                                      const char* render_hint = "") {
    bp2::Blueprint::Node n;
    n.id = I.intern(id);
    n.type = I.intern(type);
    n.x = x;
    n.y = y;
    n.render_hint = render_hint;
    return n;
}

static bp2::Blueprint::Wire make_wire(ui::StringInterner& I,
                                      bp2::PathArena& arena,
                                      const char* wire_id,
                                      const char* src_node,
                                      const char* src_port,
                                      const char* dst_node,
                                      const char* dst_port) {
    bp2::Blueprint::Wire w;
    w.id = I.intern(wire_id);
    w.source = arena.make_port(arena.make_node(arena.root(), I.intern(src_node)), I.intern(src_port));
    w.target = arena.make_port(arena.make_node(arena.root(), I.intern(dst_node)), I.intern(dst_port));
    return w;
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

} // namespace

TEST(CanvasInputBus, AliasReconnectUsesSelectedWireNotFirst) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto bus = make_node(I, "bus", "Bus", 200.0f, 120.0f, "bus");
    auto bat1 = make_node(I, "bat1", "Battery", 40.0f, 40.0f);
    bat1.outputs.push_back(EditorPort(I.intern("v_out"), PortSide::Output, PortType::V));
    auto bat2 = make_node(I, "bat2", "Battery", 40.0f, 180.0f);
    bat2.outputs.push_back(EditorPort(I.intern("v_out"), PortSide::Output, PortType::V));
    auto l1 = make_node(I, "l1", "Lamp", 420.0f, 80.0f);
    l1.inputs.push_back(EditorPort(I.intern("v_in"), PortSide::Input, PortType::V));
    auto l2 = make_node(I, "l2", "Lamp", 420.0f, 180.0f);
    l2.inputs.push_back(EditorPort(I.intern("v_in"), PortSide::Input, PortType::V));

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
    visual::mutations::rebuild(scene, model.current(), I, arena, "");

    auto* bus_widget = dynamic_cast<visual::BusNodeWidget*>(scene.find("bus"));
    ASSERT_NE(bus_widget, nullptr);
    auto* w2_alias = bus_widget->port("wire_2");
    ASSERT_NE(w2_alias, nullptr);

    auto* bat2_widget = dynamic_cast<visual::Widget*>(scene.find("bat2"));
    ASSERT_NE(bat2_widget, nullptr);
    auto* bat2_out = bat2_widget->portByName("v_out");
    ASSERT_NE(bat2_out, nullptr);

    Viewport vp;
    CanvasInput input(scene, vp, model, I, arena, "");

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
    src.outputs.push_back(EditorPort(I.intern("v_out"), PortSide::Output, PortType::V));
    auto l1 = make_node(I, "l1", "Lamp", 420.0f, 80.0f);
    l1.inputs.push_back(EditorPort(I.intern("v_in"), PortSide::Input, PortType::V));
    auto l2 = make_node(I, "l2", "Lamp", 420.0f, 180.0f);
    l2.inputs.push_back(EditorPort(I.intern("v_in"), PortSide::Input, PortType::V));

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
    visual::mutations::rebuild(scene, model.current(), I, arena, "");

    auto* bus_widget = dynamic_cast<visual::BusNodeWidget*>(scene.find("bus"));
    ASSERT_NE(bus_widget, nullptr);
    auto* w2_alias = bus_widget->port("wire_2");
    auto* w1_alias = bus_widget->port("wire_1");
    ASSERT_NE(w2_alias, nullptr);
    ASSERT_NE(w1_alias, nullptr);

    Viewport vp;
    CanvasInput input(scene, vp, model, I, arena, "");

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
    src.outputs.push_back(EditorPort(I.intern("v_out"), PortSide::Output, PortType::V));
    auto l1 = make_node(I, "l1", "Lamp", 420.0f, 120.0f);
    l1.inputs.push_back(EditorPort(I.intern("v_in"), PortSide::Input, PortType::V));

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(bus));
    bp = bp.with_node(std::move(src));
    bp = bp.with_node(std::move(l1));
    bp = bp.with_wire(make_wire(I, arena, "wire_0", "src", "v_out", "bus", "v"));

    bp2::EditorModel model(bp);
    model.next_wire_id_ = 1;
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, "");

    auto* bus_widget = dynamic_cast<visual::BusNodeWidget*>(scene.find("bus"));
    ASSERT_NE(bus_widget, nullptr);
    auto* base_v = bus_widget->port("v");
    ASSERT_NE(base_v, nullptr);

    auto* src_widget = dynamic_cast<visual::Widget*>(scene.find("src"));
    ASSERT_NE(src_widget, nullptr);
    auto* src_out = src_widget->portByName("v_out");
    ASSERT_NE(src_out, nullptr);

    Viewport vp;
    CanvasInput input(scene, vp, model, I, arena, "");

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

TEST(CanvasInputReconnect, ReconnectUpdatesSelectedWireEndpoint) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto s1 = make_node(I, "s1", "Battery", 40.0f, 60.0f);
    s1.outputs.push_back(EditorPort(I.intern("v_out"), PortSide::Output, PortType::V));
    auto s2 = make_node(I, "s2", "Battery", 40.0f, 220.0f);
    s2.outputs.push_back(EditorPort(I.intern("v_out"), PortSide::Output, PortType::V));
    auto l1 = make_node(I, "l1", "Lamp", 420.0f, 60.0f);
    l1.inputs.push_back(EditorPort(I.intern("v_in"), PortSide::Input, PortType::V));
    auto l2 = make_node(I, "l2", "Lamp", 420.0f, 220.0f);
    l2.inputs.push_back(EditorPort(I.intern("v_in"), PortSide::Input, PortType::V));

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(s1));
    bp = bp.with_node(std::move(s2));
    bp = bp.with_node(std::move(l1));
    bp = bp.with_node(std::move(l2));
    bp = bp.with_wire(make_wire(I, arena, "wire_0", "s1", "v_out", "l1", "v_in"));
    bp = bp.with_wire(make_wire(I, arena, "wire_1", "s2", "v_out", "l2", "v_in"));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, "");

    auto* l2_widget = dynamic_cast<visual::Widget*>(scene.find("l2"));
    ASSERT_NE(l2_widget, nullptr);
    auto* l2_in = l2_widget->portByName("v_in");
    ASSERT_NE(l2_in, nullptr);

    auto* l1_widget = dynamic_cast<visual::Widget*>(scene.find("l1"));
    ASSERT_NE(l1_widget, nullptr);
    auto* l1_in = l1_widget->portByName("v_in");
    ASSERT_NE(l1_in, nullptr);

    Viewport vp;
    CanvasInput input(scene, vp, model, I, arena, "");
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
    s1.outputs.push_back(EditorPort(I.intern("v_out"), PortSide::Output, PortType::V));
    auto l1 = make_node(I, "l1", "Lamp", 420.0f, 60.0f);
    l1.inputs.push_back(EditorPort(I.intern("v_in"), PortSide::Input, PortType::V));

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(s1));
    bp = bp.with_node(std::move(l1));
    bp = bp.with_wire(make_wire(I, arena, "wire_0", "s1", "v_out", "l1", "v_in"));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, "");

    auto* l1_widget = dynamic_cast<visual::Widget*>(scene.find("l1"));
    ASSERT_NE(l1_widget, nullptr);
    auto* l1_in = l1_widget->portByName("v_in");
    ASSERT_NE(l1_in, nullptr);

    Viewport vp;
    CanvasInput input(scene, vp, model, I, arena, "");
    const ui::Pt canvas_min(0.0f, 0.0f);

    ASSERT_EQ(model.current().wires().size(), 1u);
    input.on_mouse_down(port_center(l1_in), MouseButton::Left, canvas_min);
    input.on_mouse_up(MouseButton::Left, ui::Pt(900.0f, 900.0f), canvas_min);

    EXPECT_TRUE(model.current().wires().empty());
}

TEST(CanvasInputBus, DeleteNodeRemovesConnectedWiresBeforeRecreate) {
    ui::StringInterner I;
    bp2::PathArena arena(I);

    auto src = make_node(I, "src", "Battery", 40.0f, 120.0f);
    src.outputs.push_back(EditorPort(I.intern("v_out"), PortSide::Output, PortType::V));

    auto split = make_node(I, "split", "Splitter", 220.0f, 120.0f);
    split.inputs.push_back(EditorPort(I.intern("i"), PortSide::Input, PortType::Any));
    split.outputs.push_back(EditorPort(I.intern("o1"), PortSide::Output, PortType::Any));

    auto load = make_node(I, "load", "Lamp", 420.0f, 120.0f);
    load.inputs.push_back(EditorPort(I.intern("v_in"), PortSide::Input, PortType::V));

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(src));
    bp = bp.with_node(std::move(split));
    bp = bp.with_node(std::move(load));
    bp = bp.with_wire(make_wire(I, arena, "wire_0", "src", "v_out", "split", "i"));
    bp = bp.with_wire(make_wire(I, arena, "wire_1", "split", "o1", "load", "v_in"));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, "");

    Viewport vp;
    CanvasInput input(scene, vp, model, I, arena, "");

    ASSERT_TRUE(input.select_node_by_id("split"));
    input.on_key(Key::Delete);

    EXPECT_EQ(model.current().find_node(I.intern("split")), nullptr);
    EXPECT_TRUE(model.current().wires().empty());

    bp2::Blueprint::Node split2;
    split2.id = I.intern("split");
    split2.type = I.intern("Splitter");
    split2.x = 220.0f;
    split2.y = 120.0f;
    split2.inputs.push_back(EditorPort(I.intern("i"), PortSide::Input, PortType::Any));
    split2.outputs.push_back(EditorPort(I.intern("o1"), PortSide::Output, PortType::Any));
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
    src.outputs.push_back(EditorPort(I.intern("v_out"), PortSide::Output, PortType::V));
    auto load = make_node(I, "load", "Lamp", 420.0f, 120.0f);
    load.inputs.push_back(EditorPort(I.intern("v_in"), PortSide::Input, PortType::V));

    bp2::Blueprint bp;
    bp = bp.with_node(std::move(src));
    bp = bp.with_node(std::move(load));
    bp = bp.with_wire(make_wire(I, arena, "wire_probe", "src", "v_out", "load", "v_in"));

    bp2::EditorModel model(bp);
    visual::Scene scene;
    visual::mutations::rebuild(scene, model.current(), I, arena, "");

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
            auto hr = visual::hit_test(scene, p);
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
    CanvasInput input(scene, vp, model, I, arena, "");
    const ui::Pt canvas_min(0.0f, 0.0f);

    Modifiers mods;
    mods.shift = true;
    InputResult r = input.on_mouse_down(probe_pos, MouseButton::Left, canvas_min, mods);

    EXPECT_EQ(r.toggle_probe_wire_id, "wire_probe");
    EXPECT_TRUE(r.has_toggle_probe_world_pos);
    EXPECT_NEAR(r.toggle_probe_world_pos.x, probe_pos.x, 1.5f);
    EXPECT_NEAR(r.toggle_probe_world_pos.y, probe_pos.y, 1.5f);
    EXPECT_EQ(input.selected_wire(), nullptr);
}
