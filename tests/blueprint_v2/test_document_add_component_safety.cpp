#include <gtest/gtest.h>

#include "editor/document.h"
#include "editor/document_simulation_internal.h"
#include "editor/commands/blueprint_checksum.h"
#include "editor/commands/commands.h"
#include "editor/commands/extract_blueprint.h"
#include "editor/input/canvas_input.h"
#include "editor/input/editing_host.h"
#include "editor/input/input_types.h"
#include "editor/window_system.h"
#include "editor/window/properties_window.h"
#include "editor/visual/scene_mutations.h"
#include "editor/data/node_content.h"
#include "editor/data/node_state.h"
#include "editor/visual/node/visual_node.h"
#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/interface/type_definition_interface.h"
#include "blueprint_v2/library/library_index.h"
#include "io/json/component_registry_json_loader.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "../bp2_test_helpers.h"

namespace {

std::filesystem::path make_temp_dir(const char* name) {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void write_file(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream out(path);
    ASSERT_TRUE(out.is_open()) << path;
    out << contents;
}

const bp2::Blueprint::Node* require_node(const bp2::Blueprint& bp, ui::StringInterner& interner, const char* id) {
    auto* node = bp.find_node(interner.lookup(id));
    EXPECT_NE(node, nullptr) << id;
    return node;
}

bp2::Blueprint::Wire make_wire(ui::StringInterner& I,
                               const char* wire_id,
                               const char* src_node,
                               const char* src_port,
                               const char* dst_node,
                               const char* dst_port) {
    bp2::Blueprint::Wire w;
    w.id = I.intern(wire_id);
    w.source = bp2::WireEndpoint{I.intern(src_node), I.intern(src_port)};
    w.target = bp2::WireEndpoint{I.intern(dst_node), I.intern(dst_port)};
    w.domain = Domain::Electrical;
    return w;
}

/// Build a Component node whose interface matches the ComponentRegistry exactly.
bp2::Blueprint::Node make_typed_node(ui::StringInterner& I,
                                     const ComponentRegistry& registry,
                                     const char* id,
                                     const char* type,
                                     float x,
                                     float y) {
    bp2::Blueprint::Node n;
    n.semantic.id = I.intern(id);
    n.semantic.type = I.intern(type);
    n.layout.x = x;
    n.layout.y = y;
    const auto* def = registry.get(type);
    if (def) {
        n.component().iface = bp2::interface_from_type_definition(*def, I);
    }
    return n;
}

NodeContent resolve_node_content(const bp2::Blueprint::Node& node,
                                 const ComponentRegistry& registry,
                                 ui::StringInterner& interner) {
    const std::string type_name(interner.resolve(node.semantic.type));
    const auto* def = registry.get(type_name);
    EXPECT_NE(def, nullptr);
    return create_node_content(*def, registry.presentation.get(type_name),
                               node.semantic.params, node.semantic.string_params, interner);
}

bp2::Blueprint::Node make_bridge_node(ui::StringInterner& I,
                                      const char* id,
                                      bool input_bridge,
                                      PortType t = PortType::V) {
    bp2::Blueprint::Node n;
    n.semantic.id = I.intern(id);
    n.semantic.type = I.intern("BridgePort");
    n.view.name = id;
    n.layout.x = 0.0f;
    n.layout.y = 0.0f;
    n.content = bp2::Blueprint::Node::BridgePortData{
        I.intern(id),
        input_bridge ? bp2::BridgeDirection::Input
                    : bp2::BridgeDirection::Output,
        t,
    };
    return n;
}

/// Build an extract-roundtrip fixture using real registered types.
///
/// Topology (all Electrical domain):
///   ext_in (bridge input)  --port-->  a (ElectricalSource) .v_in
///                                       a.v_out  -->  b (Resistor) .v_in
///   ext_out (bridge output) <--port--  b.v_out
///
/// Wire naming:
///   w0: ext_in.port  → a.v_in
///   w1: a.v_out      → b.v_in       (internal to selection {a,b})
///   w2: b.v_out      → ext_out.port
bp2::Blueprint make_extract_roundtrip_fixture(ui::StringInterner& I,
                                              const ComponentRegistry& registry) {
    bp2::Blueprint bp;
    bp = bp.with_id(I.intern("bp_extract_doc"));
    bp = bp.with_name("ExtractDoc");
    bp = bp.with_interface(bp2::Interface({
        make_port(I, "ext_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "ext_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    }));

    auto ext_in = make_bridge_node(I, "ext_in", true);
    ext_in.layout.x = 0.0f;
    ext_in.layout.y = 0.0f;
    bp = bp.with_node(std::move(ext_in));
    bp = bp.with_node(make_typed_node(I, registry, "a",       "ElectricalSource", 20.0f, 0.0f));
    bp = bp.with_node(make_typed_node(I, registry, "b",       "Resistor",        40.0f, 0.0f));
    auto ext_out = make_bridge_node(I, "ext_out", false);
    ext_out.layout.x = 60.0f;
    ext_out.layout.y = 0.0f;
    bp = bp.with_node(std::move(ext_out));

    // bridge input port → ElectricalSource.v_in
    bp = bp.with_wire(make_wire(I, "w0", "ext_in", "port", "a",       "v_in"));
    // ElectricalSource.v_out → Resistor.v_in  (internal wire)
    bp = bp.with_wire(make_wire(I, "w1", "a",      "v_out", "b",      "v_in"));
    // Resistor.v_out → bridge output port
    bp = bp.with_wire(make_wire(I, "w2", "b",      "v_out", "ext_out", "port"));
    return bp;
}

} // namespace

TEST(DocumentSafety, AddComponentUnknownTypeDoesNotCrashOrMutate) {
    Document doc;
    ComponentRegistry registry = load_component_registry("library/");

    const size_t before_nodes = doc.model().current().nodes().size();
    const size_t before_wires = doc.model().current().wires().size();

    EXPECT_NO_THROW(doc.addComponent("DefinitelyUnknownComponent", Pt{64.0f, 64.0f}, WindowScopeId::root(), registry));

    EXPECT_EQ(doc.model().current().nodes().size(), before_nodes);
    EXPECT_EQ(doc.model().current().wires().size(), before_wires);
}

TEST(DocumentSafety, LoadHydratesRootNodeViewFromComponentRegistry) {
    namespace fs = std::filesystem;

    Document doc;
    ComponentRegistry registry = load_component_registry("library/");
    doc.setComponentRegistry(&registry);

    const fs::path dir = make_temp_dir("an24_doc_load_hydrate_root");
    const fs::path bp_path = dir / "root.blueprint";

    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("root_hydrate"));
    bp = bp.with_name("Root Hydrate");

    bp2::Blueprint::Node slider;
    slider.semantic.id = interner.intern("slider1");
    slider.semantic.type = interner.intern("Slider");
    slider.layout.x = 10.0f;
    slider.layout.y = 20.0f;

    bp2::Blueprint::Node value;
    value.semantic.id = interner.intern("value1");
    value.semantic.type = interner.intern("Value");
    value.layout.x = 40.0f;
    value.layout.y = 60.0f;

    bp = bp.with_node(std::move(slider));
    bp = bp.with_node(std::move(value));

    write_file(bp_path, bp2::BlueprintCodec::encode(bp, interner, arena, &registry));

    ASSERT_TRUE(doc.load(bp_path.string()));

    const auto* loaded_slider = require_node(doc.model().current(), doc.interner(), "slider1");
    ASSERT_NE(loaded_slider, nullptr);
    const NodeContent slider_content = resolve_node_content(*loaded_slider, registry, doc.interner());
    EXPECT_EQ(slider_content.type, bp2::NodeContentType::Slider);
    EXPECT_FLOAT_EQ(slider_content.min, 0.0f);
    EXPECT_FLOAT_EQ(slider_content.max, 1.0f);

    const auto* loaded_value = require_node(doc.model().current(), doc.interner(), "value1");
    ASSERT_NE(loaded_value, nullptr);
    EXPECT_EQ(editor::presentation::resolve_frame_kind(
                  registry.get("Value"), registry.presentation.get("Value")),
              editor::presentation::NodeFrameKind::Reference);

    fs::remove_all(dir);
}

TEST(DocumentSafety, LoadHydratesEmbeddedInlineBlueprintNodeViewFromComponentRegistry) {
    namespace fs = std::filesystem;

    Document doc;
    ComponentRegistry registry = load_component_registry("library/");
    doc.setComponentRegistry(&registry);

    const fs::path dir = make_temp_dir("an24_doc_load_hydrate_embedded");
    const fs::path bp_path = dir / "embedded.blueprint";

    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    bp2::Blueprint inline_bp;
    inline_bp = inline_bp.with_id(interner.intern("inline_bp"));
    inline_bp = inline_bp.with_name("Inline BP");

    bp2::Blueprint::Node slider;
    slider.semantic.id = interner.intern("inner_slider");
    slider.semantic.type = interner.intern("Slider");
    slider.layout.x = 4.0f;
    slider.layout.y = 8.0f;
    inline_bp = inline_bp.with_node(std::move(slider));

    bp2::Blueprint::Node host;
    host.semantic.id = interner.intern("host1");
    host.semantic.type = interner.intern("Group");
    host.layout.collapsed = true;
    host.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(inline_bp.with_id(interner.intern("Group"))))
    };

    bp2::Blueprint root;
    root = root.with_id(interner.intern("root_embedded_hydrate"));
    root = root.with_name("Embedded Hydrate");
    root = root.with_node(std::move(host));

    write_file(bp_path, bp2::BlueprintCodec::encode(root, interner, arena, &registry));

    ASSERT_TRUE(doc.load(bp_path.string()));

    const auto* loaded_host = require_node(doc.model().current(), doc.interner(), "host1");
    ASSERT_NE(loaded_host, nullptr);
    ASSERT_TRUE(loaded_host->is_blueprint_instance());
    ASSERT_TRUE(loaded_host->blueprint_instance().source.is_embedded());
    ASSERT_NE(loaded_host->blueprint_instance().source.inline_def(), nullptr);

    const auto* loaded_slider = require_node(*loaded_host->blueprint_instance().source.inline_def(), doc.interner(), "inner_slider");
    ASSERT_NE(loaded_slider, nullptr);
    const NodeContent slider_content = resolve_node_content(*loaded_slider, registry, doc.interner());
    EXPECT_EQ(slider_content.type, bp2::NodeContentType::Slider);
    EXPECT_FLOAT_EQ(slider_content.min, 0.0f);
    EXPECT_FLOAT_EQ(slider_content.max, 1.0f);

    fs::remove_all(dir);
}

TEST(DocumentSafety, SetSliderValuePreservesCanonicalStaticContent) {
    Document doc;
    ComponentRegistry registry = load_component_registry("library/");
    doc.setComponentRegistry(&registry);

    ui::StringInterner& I = doc.interner();
    bp2::Blueprint bp;
    bp = bp.with_id(I.intern("slider_doc"));
    bp = bp.with_name("Slider Doc");

    auto slider = make_typed_node(I, registry, "slider1", "Slider", 40.0f, 20.0f);
    slider.semantic.params[I.intern("min")] = -10.0f;
    slider.semantic.params[I.intern("max")] = 200.0f;
    bp = bp.with_node(std::move(slider));

    doc.model().replace_current(std::move(bp));
    doc.rebuildAllWindows();

    auto* win = doc.windowManager().find(WindowScopeId::root());
    ASSERT_NE(win, nullptr);
    auto* widget = dynamic_cast<visual::NodeWidget*>(win->scene.find("slider1"));
    ASSERT_NE(widget, nullptr);

    doc.setSliderValue(editor::NodeId::from_string("slider1"), 42.0f);

    NodeContent content = widget->currentContent();
    EXPECT_EQ(content.type, bp2::NodeContentType::Slider);
    EXPECT_FLOAT_EQ(content.min, -10.0f);
    EXPECT_FLOAT_EQ(content.max, 200.0f);
    EXPECT_FLOAT_EQ(content.value, 42.0f);
}

TEST(DocumentSafety, SetKnobPositionPreservesCanonicalStaticContent) {
    Document doc;
    ComponentRegistry registry = load_component_registry("library/");
    doc.setComponentRegistry(&registry);

    ui::StringInterner& I = doc.interner();
    bp2::Blueprint bp;
    bp = bp.with_id(I.intern("knob_doc"));
    bp = bp.with_name("Knob Doc");

    auto knob = make_typed_node(I, registry, "knob1", "KnobSwitch", 40.0f, 20.0f);
    knob.semantic.params[I.intern("positions")] = 7.0f;
    bp = bp.with_node(std::move(knob));

    doc.model().replace_current(std::move(bp));
    doc.rebuildAllWindows();

    auto* win = doc.windowManager().find(WindowScopeId::root());
    ASSERT_NE(win, nullptr);
    auto* widget = dynamic_cast<visual::NodeWidget*>(win->scene.find("knob1"));
    ASSERT_NE(widget, nullptr);

    doc.setKnobPosition(editor::NodeId::from_string("knob1"), 3);

    NodeContent content = widget->currentContent();
    EXPECT_EQ(content.type, bp2::NodeContentType::Knob);
    EXPECT_FLOAT_EQ(content.min, 0.0f);
    EXPECT_FLOAT_EQ(content.max, 7.0f);
    EXPECT_FLOAT_EQ(content.value, 3.0f);
}

TEST(DocumentSafety, LoadHydratesNonDefaultKnobPositionsFromInstanceParams) {
    namespace fs = std::filesystem;

    Document doc;
    ComponentRegistry registry = load_component_registry("library/");
    doc.setComponentRegistry(&registry);

    const fs::path dir = make_temp_dir("an24_doc_knob_positions_load");
    const fs::path bp_path = dir / "knob_positions.blueprint";

    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("knob_positions_root"));
    bp = bp.with_name("Knob Positions Root");

    auto knob = make_typed_node(interner, registry, "knob5", "KnobSwitch", 80.0f, 40.0f);
    knob.semantic.params[interner.intern("positions")] = 5.0f;
    bp = bp.with_node(std::move(knob));

    write_file(bp_path, bp2::BlueprintCodec::encode(bp, interner, arena, &registry));
    ASSERT_TRUE(doc.load(bp_path.string()));

    const auto* loaded = require_node(doc.model().current(), doc.interner(), "knob5");
    ASSERT_NE(loaded, nullptr);
    const NodeContent loaded_content = resolve_node_content(*loaded, registry, doc.interner());
    EXPECT_EQ(loaded_content.type, bp2::NodeContentType::Knob);
    EXPECT_FLOAT_EQ(loaded_content.max, 5.0f);

    auto* win = doc.windowManager().find(WindowScopeId::root());
    ASSERT_NE(win, nullptr);
    auto* widget = dynamic_cast<visual::NodeWidget*>(win->scene.find("knob5"));
    ASSERT_NE(widget, nullptr);
    EXPECT_FLOAT_EQ(widget->currentContent().max, 5.0f);

    fs::remove_all(dir);
}

TEST(DocumentSafety, LoadHydratesNonDefaultGaugeRangeAndUnitFromInstanceParams) {
    namespace fs = std::filesystem;

    Document doc;
    ComponentRegistry registry = load_component_registry("library/");
    doc.setComponentRegistry(&registry);

    const fs::path dir = make_temp_dir("an24_doc_gauge_params_load");
    const fs::path bp_path = dir / "gauge_params.blueprint";

    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("gauge_params_root"));
    bp = bp.with_name("Gauge Params Root");

    auto gauge = make_typed_node(interner, registry, "gauge1", "Voltmeter", 80.0f, 40.0f);
    gauge.semantic.params[interner.intern("min")] = -20.0f;
    gauge.semantic.params[interner.intern("max")] = 60.0f;
    bp = bp.with_node(std::move(gauge));

    write_file(bp_path, bp2::BlueprintCodec::encode(bp, interner, arena, &registry));
    ASSERT_TRUE(doc.load(bp_path.string()));

    const auto* loaded = require_node(doc.model().current(), doc.interner(), "gauge1");
    ASSERT_NE(loaded, nullptr);
    const NodeContent loaded_content = resolve_node_content(*loaded, registry, doc.interner());
    EXPECT_EQ(loaded_content.type, bp2::NodeContentType::Gauge);
    EXPECT_FLOAT_EQ(loaded_content.min, -20.0f);
    EXPECT_FLOAT_EQ(loaded_content.max, 60.0f);
    EXPECT_EQ(loaded_content.unit, "V");

    auto* win = doc.windowManager().find(WindowScopeId::root());
    ASSERT_NE(win, nullptr);
    auto* widget = dynamic_cast<visual::NodeWidget*>(win->scene.find("gauge1"));
    ASSERT_NE(widget, nullptr);
    NodeContent content = widget->currentContent();
    EXPECT_FLOAT_EQ(content.min, -20.0f);
    EXPECT_FLOAT_EQ(content.max, 60.0f);
    EXPECT_EQ(content.unit, "V");

    fs::remove_all(dir);
}

TEST(DocumentSafety, LoadHydratesHoldButtonAsSwitchLikeContent) {
    namespace fs = std::filesystem;

    Document doc;
    ComponentRegistry registry = load_component_registry("library/");
    doc.setComponentRegistry(&registry);

    const fs::path dir = make_temp_dir("an24_doc_hold_button_load");
    const fs::path bp_path = dir / "hold_button.blueprint";

    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("hold_button_root"));
    bp = bp.with_name("Hold Button Root");

    auto btn = make_typed_node(interner, registry, "btn1", "HoldButton", 20.0f, 20.0f);
    btn.semantic.params[interner.intern("idle")] = 2.5f;
    bp = bp.with_node(std::move(btn));

    write_file(bp_path, bp2::BlueprintCodec::encode(bp, interner, arena, &registry));
    ASSERT_TRUE(doc.load(bp_path.string()));

    const auto* loaded = require_node(doc.model().current(), doc.interner(), "btn1");
    ASSERT_NE(loaded, nullptr);
    const NodeContent loaded_content = resolve_node_content(*loaded, registry, doc.interner());
    EXPECT_EQ(loaded_content.type, bp2::NodeContentType::Switch);
    EXPECT_FALSE(loaded_content.state);

    auto* win = doc.windowManager().find(WindowScopeId::root());
    ASSERT_NE(win, nullptr);
    auto* widget = dynamic_cast<visual::NodeWidget*>(win->scene.find("btn1"));
    ASSERT_NE(widget, nullptr);

    NodeContent content = widget->currentContent();
    EXPECT_EQ(content.type, bp2::NodeContentType::Switch);
    EXPECT_FALSE(content.state);

    fs::remove_all(dir);
}

TEST(DocumentSafety, PropertiesApplyRebuildsSliderWidgetAndInteractionFromEditedParams) {
    Document doc;
    ComponentRegistry registry = load_component_registry("library/");
    doc.setComponentRegistry(&registry);

    ui::StringInterner& I = doc.interner();
    bp2::Blueprint bp;
    bp = bp.with_id(I.intern("slider_apply_doc"));
    bp = bp.with_name("Slider Apply Doc");

    auto slider = make_typed_node(I, registry, "slider_apply", "Slider", 40.0f, 20.0f);
    slider.semantic.params[I.intern("min")] = 0.0f;
    slider.semantic.params[I.intern("max")] = 1.0f;
    bp = bp.with_node(std::move(slider));

    doc.model().replace_current(std::move(bp));
    doc.rebuildAllWindows();

    const auto* original = require_node(doc.model().current(), I, "slider_apply");
    ASSERT_NE(original, nullptr);

    PropertiesWindow props;
    props.open(*original, "slider_apply", create_editor_model_host(doc.model()), doc.interner(), &registry,
               [&doc](const std::string&) { doc.rebuildAllWindows(); });
    props.set_pending_param("min", -10.0f);
    props.set_pending_param("max", 200.0f);
    props.apply();

    const auto* updated = require_node(doc.model().current(), I, "slider_apply");
    ASSERT_NE(updated, nullptr);
    EXPECT_FLOAT_EQ(updated->semantic.params.at(I.intern("min")), -10.0f);
    EXPECT_FLOAT_EQ(updated->semantic.params.at(I.intern("max")), 200.0f);
    {
        const NodeContent updated_content = resolve_node_content(*updated, registry, I);
        EXPECT_FLOAT_EQ(updated_content.min, -10.0f);
        EXPECT_FLOAT_EQ(updated_content.max, 200.0f);
    }

    auto* win = doc.windowManager().find(WindowScopeId::root());
    ASSERT_NE(win, nullptr);
    auto* widget = dynamic_cast<visual::NodeWidget*>(win->scene.find("slider_apply"));
    ASSERT_NE(widget, nullptr);

    NodeContent content = widget->currentContent();
    EXPECT_FLOAT_EQ(content.min, -10.0f);
    EXPECT_FLOAT_EQ(content.max, 200.0f);

    auto& input = doc.input();
    input.simulation_mode = true;
    Bounds cb = widget->contentBounds();
    Pt wpos = widget->worldPos();
    Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);
    Pt canvas_min(0.0f, 0.0f);

    auto down = input.on_mouse_down(click_world, MouseButton::Left, canvas_min);
    ASSERT_EQ(down.slider_node_id, "slider_apply");
    ASSERT_EQ(input.state(), InputState::DraggingSlider);

    auto drag = input.on_mouse_drag(MouseButton::Left, Pt(500.0f, 0.0f), canvas_min);
    EXPECT_EQ(drag.slider_node_id, "slider_apply");
    EXPECT_FLOAT_EQ(drag.slider_value, 200.0f)
        << "Slider interaction must immediately use inspector-edited max after rebuild";
}

TEST(DocumentSafety, PropertiesApplyRebuildsKnobWidgetAndInteractionFromEditedParams) {
    Document doc;
    ComponentRegistry registry = load_component_registry("library/");
    doc.setComponentRegistry(&registry);

    ui::StringInterner& I = doc.interner();
    bp2::Blueprint bp;
    bp = bp.with_id(I.intern("knob_apply_doc"));
    bp = bp.with_name("Knob Apply Doc");

    auto knob = make_typed_node(I, registry, "knob_apply", "KnobSwitch", 40.0f, 20.0f);
    knob.semantic.params[I.intern("positions")] = 2.0f;
    knob.semantic.params[I.intern("initial_position")] = 0.0f;
    bp = bp.with_node(std::move(knob));

    doc.model().replace_current(std::move(bp));
    doc.rebuildAllWindows();

    const auto* original = require_node(doc.model().current(), I, "knob_apply");
    ASSERT_NE(original, nullptr);

    PropertiesWindow props;
    props.open(*original, "knob_apply", create_editor_model_host(doc.model()), doc.interner(), &registry,
               [&doc](const std::string&) { doc.rebuildAllWindows(); });
    props.set_pending_param("positions", 5.0f);
    props.apply();

    const auto* updated = require_node(doc.model().current(), I, "knob_apply");
    ASSERT_NE(updated, nullptr);
    EXPECT_FLOAT_EQ(updated->semantic.params.at(I.intern("positions")), 5.0f);
    EXPECT_FLOAT_EQ(resolve_node_content(*updated, registry, I).max, 5.0f);

    auto* win = doc.windowManager().find(WindowScopeId::root());
    ASSERT_NE(win, nullptr);
    auto* widget = dynamic_cast<visual::NodeWidget*>(win->scene.find("knob_apply"));
    ASSERT_NE(widget, nullptr);

    NodeContent content = widget->currentContent();
    EXPECT_FLOAT_EQ(content.max, 5.0f);

    auto& input = doc.input();
    input.simulation_mode = true;
    Bounds cb = widget->contentBounds();
    Pt wpos = widget->worldPos();
    Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);
    Pt canvas_min(0.0f, 0.0f);

    auto down = input.on_mouse_down(click_world, MouseButton::Left, canvas_min);
    ASSERT_EQ(down.knob_node_id, "knob_apply");
    ASSERT_EQ(input.state(), InputState::DraggingKnob);

    auto drag = input.on_mouse_drag(MouseButton::Left, Pt(120.0f, 0.0f), canvas_min);
    EXPECT_EQ(drag.knob_node_id, "knob_apply");
    EXPECT_EQ(drag.knob_position, 4)
        << "Knob interaction must immediately use inspector-edited positions after rebuild";
}

TEST(DocumentSafety, PropertiesApplyRebuildsGaugeWidgetFromEditedParams) {
    Document doc;
    ComponentRegistry registry = load_component_registry("library/");
    doc.setComponentRegistry(&registry);

    ui::StringInterner& I = doc.interner();
    bp2::Blueprint bp;
    bp = bp.with_id(I.intern("gauge_apply_doc"));
    bp = bp.with_name("Gauge Apply Doc");

    auto gauge = make_typed_node(I, registry, "gauge_apply", "Voltmeter", 40.0f, 20.0f);
    gauge.semantic.params[I.intern("min")] = 0.0f;
    gauge.semantic.params[I.intern("max")] = 28.0f;
    bp = bp.with_node(std::move(gauge));

    doc.model().replace_current(std::move(bp));
    doc.rebuildAllWindows();

    const auto* original = require_node(doc.model().current(), I, "gauge_apply");
    ASSERT_NE(original, nullptr);

    PropertiesWindow props;
    props.open(*original, "gauge_apply", create_editor_model_host(doc.model()), doc.interner(), &registry,
               [&doc](const std::string&) { doc.rebuildAllWindows(); });
    props.set_pending_param("min", -20.0f);
    props.set_pending_param("max", 60.0f);
    props.apply();

    const auto* updated = require_node(doc.model().current(), I, "gauge_apply");
    ASSERT_NE(updated, nullptr);
    EXPECT_FLOAT_EQ(updated->semantic.params.at(I.intern("min")), -20.0f);
    EXPECT_FLOAT_EQ(updated->semantic.params.at(I.intern("max")), 60.0f);
    {
        const NodeContent updated_content = resolve_node_content(*updated, registry, I);
        EXPECT_FLOAT_EQ(updated_content.min, -20.0f);
        EXPECT_FLOAT_EQ(updated_content.max, 60.0f);
    }

    auto* win = doc.windowManager().find(WindowScopeId::root());
    ASSERT_NE(win, nullptr);
    auto* widget = dynamic_cast<visual::NodeWidget*>(win->scene.find("gauge_apply"));
    ASSERT_NE(widget, nullptr);

    NodeContent content = widget->currentContent();
    EXPECT_EQ(content.type, bp2::NodeContentType::Gauge);
    EXPECT_FLOAT_EQ(content.min, -20.0f);
    EXPECT_FLOAT_EQ(content.max, 60.0f);
    EXPECT_EQ(content.unit, "V");
}

TEST(DocumentSafety, PropertiesApplyRebuildsSwitchWidgetFromEditedClosedState) {
    Document doc;
    ComponentRegistry registry = load_component_registry("library/");
    doc.setComponentRegistry(&registry);

    ui::StringInterner& I = doc.interner();
    bp2::Blueprint bp;
    bp = bp.with_id(I.intern("switch_apply_doc"));
    bp = bp.with_name("Switch Apply Doc");

    auto sw = make_typed_node(I, registry, "switch_apply", "Switch", 40.0f, 20.0f);
    sw.semantic.params[I.intern("closed")] = 0.0f;
    bp = bp.with_node(std::move(sw));

    doc.model().replace_current(std::move(bp));
    doc.rebuildAllWindows();

    const auto* original = require_node(doc.model().current(), I, "switch_apply");
    ASSERT_NE(original, nullptr);

    PropertiesWindow props;
    props.open(*original, "switch_apply", create_editor_model_host(doc.model()), doc.interner(), &registry,
               [&doc](const std::string&) { doc.rebuildAllWindows(); });
    props.set_pending_param("closed", 1.0f);
    props.apply();

    const auto* updated = require_node(doc.model().current(), I, "switch_apply");
    ASSERT_NE(updated, nullptr);
    ASSERT_TRUE(updated->semantic.string_params.count("closed") > 0);
    EXPECT_EQ(updated->semantic.string_params.at("closed"), "true");

    auto* win = doc.windowManager().find(WindowScopeId::root());
    ASSERT_NE(win, nullptr);
    auto* widget = dynamic_cast<visual::NodeWidget*>(win->scene.find("switch_apply"));
    ASSERT_NE(widget, nullptr);

    NodeContent content = widget->currentContent();
    EXPECT_EQ(content.type, bp2::NodeContentType::Switch);
    EXPECT_TRUE(content.state);
}

TEST(DocumentSafety, PropertiesApplyRebuildsAzsVerticalToggleFromEditedClosedState) {
    Document doc;
    ComponentRegistry registry = load_component_registry("library/");
    doc.setComponentRegistry(&registry);

    ui::StringInterner& I = doc.interner();
    bp2::Blueprint bp;
    bp = bp.with_id(I.intern("azs_apply_doc"));
    bp = bp.with_name("AZS Apply Doc");

    auto azs = make_typed_node(I, registry, "azs_apply", "AZS", 40.0f, 20.0f);
    azs.semantic.params[I.intern("closed")] = 0.0f;
    bp = bp.with_node(std::move(azs));

    doc.model().replace_current(std::move(bp));
    doc.rebuildAllWindows();

    const auto* original = require_node(doc.model().current(), I, "azs_apply");
    ASSERT_NE(original, nullptr);

    PropertiesWindow props;
    props.open(*original, "azs_apply", create_editor_model_host(doc.model()), doc.interner(), &registry,
               [&doc](const std::string&) { doc.rebuildAllWindows(); });
    props.set_pending_param("closed", 1.0f);
    props.apply();

    const auto* updated = require_node(doc.model().current(), I, "azs_apply");
    ASSERT_NE(updated, nullptr);
    ASSERT_TRUE(updated->semantic.string_params.count("closed") > 0);
    EXPECT_EQ(updated->semantic.string_params.at("closed"), "true");

    auto* win = doc.windowManager().find(WindowScopeId::root());
    ASSERT_NE(win, nullptr);
    auto* widget = dynamic_cast<visual::NodeWidget*>(win->scene.find("azs_apply"));
    ASSERT_NE(widget, nullptr);

    NodeContent content = widget->currentContent();
    EXPECT_EQ(content.type, bp2::NodeContentType::VerticalToggle);
    EXPECT_TRUE(content.state);
}

TEST(DocumentSafety, PropertiesApplyRebuildsRelaySwitchFromEditedClosedState) {
    Document doc;
    ComponentRegistry registry = load_component_registry("library/");
    doc.setComponentRegistry(&registry);

    ui::StringInterner& I = doc.interner();
    bp2::Blueprint bp;
    bp = bp.with_id(I.intern("relay_apply_doc"));
    bp = bp.with_name("Relay Apply Doc");

    auto relay = make_typed_node(I, registry, "relay_apply", "Relay", 40.0f, 20.0f);
    relay.semantic.params[I.intern("closed")] = 0.0f;
    bp = bp.with_node(std::move(relay));

    doc.model().replace_current(std::move(bp));
    doc.rebuildAllWindows();

    const auto* original = require_node(doc.model().current(), I, "relay_apply");
    ASSERT_NE(original, nullptr);

    PropertiesWindow props;
    props.open(*original, "relay_apply", create_editor_model_host(doc.model()), doc.interner(), &registry,
               [&doc](const std::string&) { doc.rebuildAllWindows(); });
    props.set_pending_param("closed", 1.0f);
    props.apply();

    const auto* updated = require_node(doc.model().current(), I, "relay_apply");
    ASSERT_NE(updated, nullptr);
    ASSERT_TRUE(updated->semantic.string_params.count("closed") > 0);
    EXPECT_EQ(updated->semantic.string_params.at("closed"), "true");

    auto* win = doc.windowManager().find(WindowScopeId::root());
    ASSERT_NE(win, nullptr);
    auto* widget = dynamic_cast<visual::NodeWidget*>(win->scene.find("relay_apply"));
    ASSERT_NE(widget, nullptr);

    NodeContent content = widget->currentContent();
    EXPECT_EQ(content.type, bp2::NodeContentType::Switch);
    EXPECT_TRUE(content.state);
}

TEST(DocumentSafety, SliderRuntimeReadbackUsesOutPortWhenPresent) {
    ui::StringInterner I;
    ComponentRegistry registry = load_component_registry("library/");

    auto slider = make_typed_node(I, registry, "slider1", "Slider", 40.0f, 20.0f);
    set_iface(slider, {
        make_port(I, "out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    auto port = editor::select_slider_readback_port(slider, I);
    ASSERT_TRUE(port.has_value());
    EXPECT_EQ(*port, "out");
}

TEST(DocumentSafety, SliderRuntimeReadbackUsesControlPortWhenOutMissing) {
    ui::StringInterner I;
    ComponentRegistry registry = load_component_registry("library/");

    auto slider = make_typed_node(I, registry, "slider1", "Slider", 40.0f, 20.0f);
    set_iface(slider, {
        make_port(I, "control", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });

    auto port = editor::select_slider_readback_port(slider, I);
    ASSERT_TRUE(port.has_value());
    EXPECT_EQ(*port, "control");
}

TEST(DocumentSafety, SliderRuntimeReadbackPrefersOutOverControlWhenBothExist) {
    ui::StringInterner I;
    ComponentRegistry registry = load_component_registry("library/");

    auto slider = make_typed_node(I, registry, "slider1", "Slider", 40.0f, 20.0f);
    set_iface(slider, {
        make_port(I, "control", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });

    auto port = editor::select_slider_readback_port(slider, I);
    ASSERT_TRUE(port.has_value());
    EXPECT_EQ(*port, "out");
}

TEST(DocumentSafety, SliderRuntimeReadbackReturnsNulloptWhenNoRelevantPorts) {
    ui::StringInterner I;

    bp2::Blueprint::Node slider;
    slider.semantic.id = I.intern("slider1");
    slider.semantic.type = I.intern("Slider");
    // Empty interface — no out, no control
    set_iface(slider, {});

    auto port = editor::select_slider_readback_port(slider, I);
    EXPECT_FALSE(port.has_value());
}

TEST(DocumentSafety, LoadNormalizesLegacyAutosizeWithoutDirtyingOrCreatingUndoHistory) {
    namespace fs = std::filesystem;

    Document doc;
    ComponentRegistry registry = load_component_registry("library/");
    doc.setComponentRegistry(&registry);

    const fs::path dir = make_temp_dir("an24_doc_load_normalize_sizes");
    const fs::path bp_path = dir / "normalize.blueprint";

    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("normalize_root"));
    bp = bp.with_name("Normalize Root");

    auto legacy = make_typed_node(interner, registry, "legacy_vc", "VariableConductance", 0.0f, 0.0f);
    legacy.layout.width = 160.0f;
    legacy.layout.height = 112.0f;
    legacy.layout.manual_size = false;
    legacy.view.name = "legacy_vc";

    auto manual = make_typed_node(interner, registry, "manual_vc", "VariableConductance", 200.0f, 0.0f);
    manual.layout.width = 160.0f;
    manual.layout.height = 112.0f;
    manual.layout.manual_size = true;
    manual.view.name = "manual_vc";

    bp = bp.with_node(std::move(legacy));
    bp = bp.with_node(std::move(manual));

    write_file(bp_path, bp2::BlueprintCodec::encode(bp, interner, arena, &registry));

    ASSERT_TRUE(doc.load(bp_path.string()));

    const auto* legacy_node = require_node(doc.model().current(), doc.interner(), "legacy_vc");
    const auto* manual_node = require_node(doc.model().current(), doc.interner(), "manual_vc");
    ASSERT_NE(legacy_node, nullptr);
    ASSERT_NE(manual_node, nullptr);

    ASSERT_TRUE(legacy_node->layout.width.has_value());
    ASSERT_TRUE(legacy_node->layout.height.has_value());
    EXPECT_GE(*legacy_node->layout.width, 160.0f);
    EXPECT_LE(*legacy_node->layout.height, 112.0f);
    EXPECT_FALSE(legacy_node->layout.manual_size);

    ASSERT_TRUE(manual_node->layout.width.has_value());
    ASSERT_TRUE(manual_node->layout.height.has_value());
    EXPECT_FLOAT_EQ(*manual_node->layout.width, 160.0f);
    EXPECT_FLOAT_EQ(*manual_node->layout.height, 112.0f);
    EXPECT_TRUE(manual_node->layout.manual_size);

    EXPECT_FALSE(doc.model().is_dirty());
    EXPECT_FALSE(doc.canUndo());

    fs::remove_all(dir);
}

TEST(DocumentSafety, OpenExternalRefWindowHydratesNodeViewFromComponentRegistry) {
    namespace fs = std::filesystem;

    Document doc;
    ComponentRegistry registry = load_component_registry("library/");
    doc.setComponentRegistry(&registry);

    bp2::LibraryIndex index;
    const fs::path dir = make_temp_dir("an24_doc_load_hydrate_external");
    const fs::path ext_path = dir / "external.blueprint";
    index.entries["external_test"] = ext_path.string();
    doc.setLibraryIndex(&index);

    ui::StringInterner ext_interner;
    bp2::PathArena ext_arena(ext_interner);
    bp2::Blueprint ext_bp;
    ext_bp = ext_bp.with_id(ext_interner.intern("external_test"));
    ext_bp = ext_bp.with_name("External Test");

    bp2::Blueprint::Node slider;
    slider.semantic.id = ext_interner.intern("external_slider");
    slider.semantic.type = ext_interner.intern("Slider");

    bp2::Blueprint::Node value;
    value.semantic.id = ext_interner.intern("external_value");
    value.semantic.type = ext_interner.intern("Value");

    ext_bp = ext_bp.with_node(std::move(slider));
    ext_bp = ext_bp.with_node(std::move(value));

    write_file(ext_path, bp2::BlueprintCodec::encode(ext_bp, ext_interner, ext_arena, &registry));

    bp2::Blueprint::Node ref_host;
    ref_host.semantic.id = doc.interner().intern("external_node");
    ref_host.semantic.type = doc.interner().intern("external_test");
    ref_host.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_reference(
        doc.interner().intern("external_test"))
    };
    doc.model().replace_current(doc.model().current().with_node(std::move(ref_host)));

    doc.openSubWindow(WindowScopeId::external({doc.interner().intern("external_node")}));

    auto* win = doc.windowManager().find(WindowScopeId::external({doc.interner().intern("external_node")}));
    ASSERT_NE(win, nullptr);
    ASSERT_TRUE(win->external_blueprint.has_value());

    const auto* loaded_slider = require_node(*win->external_blueprint, *win->external_interner, "external_slider");
    ASSERT_NE(loaded_slider, nullptr);
    EXPECT_EQ(resolve_node_content(*loaded_slider, registry, *win->external_interner).type,
              bp2::NodeContentType::Slider);

    const auto* loaded_value = require_node(*win->external_blueprint, *win->external_interner, "external_value");
    ASSERT_NE(loaded_value, nullptr);
    EXPECT_EQ(editor::presentation::resolve_frame_kind(
                  registry.get("Value"), registry.presentation.get("Value")),
              editor::presentation::NodeFrameKind::Reference);

    fs::remove_all(dir);
}

TEST(DocumentSafety, NestedExternalRefWindowUsesNestedHostNodeTitle) {
    namespace fs = std::filesystem;

    Document doc;
    ComponentRegistry registry = load_component_registry("library/");
    doc.setComponentRegistry(&registry);

    bp2::LibraryIndex index;
    const fs::path dir = make_temp_dir("an24_doc_nested_external_title");
    const fs::path ext_path = dir / "external.blueprint";
    index.entries["external_test"] = ext_path.string();
    doc.setLibraryIndex(&index);

    ui::StringInterner ext_interner;
    bp2::PathArena ext_arena(ext_interner);
    bp2::Blueprint ext_bp;
    ext_bp = ext_bp.with_id(ext_interner.intern("external_test"));
    ext_bp = ext_bp.with_name("External Test");
    write_file(ext_path, bp2::BlueprintCodec::encode(ext_bp, ext_interner, ext_arena, &registry));

    bp2::Blueprint inner;
    inner = inner.with_id(doc.interner().intern("inner_bp"));
    inner = inner.with_name("Inner");

    bp2::Blueprint::Node nested_ref;
    nested_ref.semantic.id = doc.interner().intern("nested_ref");
    nested_ref.semantic.type = doc.interner().intern("external_test");
    nested_ref.view.name = "Nested Ref";
    nested_ref.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_reference(
            doc.interner().intern("external_test"))
    };
    inner = inner.with_node(std::move(nested_ref));

    bp2::Blueprint::Node host;
    host.semantic.id = doc.interner().intern("group_1");
    host.semantic.type = doc.interner().intern("Group");
    host.view.name = "group_1";
    host.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            std::make_unique<bp2::Blueprint>(inner.with_id(doc.interner().intern("Group"))))
    };

    bp2::Blueprint root;
    root = root.with_id(doc.interner().intern("root_bp"));
    root = root.with_name("Root");
    root = root.with_node(std::move(host));
    doc.model().replace_current(std::move(root));

    doc.openSubWindow(WindowScopeId::external({doc.interner().intern("group_1"), doc.interner().intern("nested_ref")}));

    auto* win = doc.windowManager().find(WindowScopeId::external({doc.interner().intern("group_1"), doc.interner().intern("nested_ref")}));
    ASSERT_NE(win, nullptr);
    EXPECT_EQ(win->title, "Nested Ref [group_1:nested_ref]");

    fs::remove_all(dir);
}

TEST(DocumentSafety, ClosingDocumentClosesPropertiesWindowOwnedByThatDocument) {
    WindowSystem ws;

    Document& first = *ws.activeDocument();
    ui::StringInterner& interner = first.interner();

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("root_bp"));
    bp = bp.with_node(make_typed_node(interner, ws.typeRegistry(), "node_a", "Battery", 0.0f, 0.0f));
    first.model().replace_current(std::move(bp));

    ws.openPropertiesForNode(editor::NodeId::from_string("node_a"), WindowScopeId::root(), first);
    ASSERT_TRUE(ws.propertiesWindow().is_open());

    Document& second = ws.createDocument();
    (void)second;

    ASSERT_TRUE(ws.closeDocument(first));
    EXPECT_FALSE(ws.propertiesWindow().is_open());
}

TEST(DocumentSafety, ExplicitNormalizeNodeSizesCreatesUndoableShrinkAndClearsManualIntent) {
    Document doc;
    ComponentRegistry registry = load_component_registry("library/");
    doc.setComponentRegistry(&registry);

    bp2::Blueprint bp;
    bp = bp.with_id(doc.interner().intern("normalize_manual_doc"));
    bp = bp.with_name("Normalize Manual Doc");

    auto legacy = make_typed_node(doc.interner(), registry, "legacy_vc", "VariableConductance", 0.0f, 0.0f);
    legacy.layout.width = 160.0f;
    legacy.layout.height = 112.0f;
    legacy.layout.manual_size = false;
    legacy.view.name = "legacy_vc";

    auto manual = make_typed_node(doc.interner(), registry, "manual_vc", "VariableConductance", 200.0f, 0.0f);
    manual.layout.width = 160.0f;
    manual.layout.height = 112.0f;
    manual.layout.manual_size = true;
    manual.view.name = "manual_vc";

    bp = bp.with_node(std::move(legacy));
    bp = bp.with_node(std::move(manual));
    doc.model().replace_current(std::move(bp));
    visual::mutations::rebuild(doc.scene(), doc.blueprint(), doc.interner(), doc.arena(), {}, ComponentRegistry{});

    ASSERT_TRUE(doc.normalizeNodeSizesToFit(false));
    EXPECT_TRUE(doc.canUndo());
    EXPECT_TRUE(doc.model().is_dirty());

    const auto* legacy_node = require_node(doc.model().current(), doc.interner(), "legacy_vc");
    const auto* manual_node = require_node(doc.model().current(), doc.interner(), "manual_vc");
    ASSERT_NE(legacy_node, nullptr);
    ASSERT_NE(manual_node, nullptr);

    ASSERT_TRUE(legacy_node->layout.width.has_value());
    ASSERT_TRUE(legacy_node->layout.height.has_value());
    ASSERT_TRUE(manual_node->layout.width.has_value());
    ASSERT_TRUE(manual_node->layout.height.has_value());
    EXPECT_LT(*legacy_node->layout.width, 160.0f);
    EXPECT_LE(*legacy_node->layout.height, 112.0f);
    EXPECT_LT(*manual_node->layout.width, 160.0f);
    EXPECT_LE(*manual_node->layout.height, 112.0f);
    EXPECT_FALSE(legacy_node->layout.manual_size);
    EXPECT_FALSE(manual_node->layout.manual_size);

    ASSERT_TRUE(doc.performUndo());
    const auto* undone_manual = require_node(doc.model().current(), doc.interner(), "manual_vc");
    ASSERT_NE(undone_manual, nullptr);
    ASSERT_TRUE(undone_manual->layout.width.has_value());
    ASSERT_TRUE(undone_manual->layout.height.has_value());
    EXPECT_FLOAT_EQ(*undone_manual->layout.width, 160.0f);
    EXPECT_FLOAT_EQ(*undone_manual->layout.height, 112.0f);
    EXPECT_TRUE(undone_manual->layout.manual_size);
}

TEST(DocumentSafety, SaveLoadRoundTripPreservesCanonicalNodeColor) {
    namespace fs = std::filesystem;

    Document doc;
    ComponentRegistry registry = load_component_registry("library/");
    doc.setComponentRegistry(&registry);

    const fs::path dir = make_temp_dir("an24_doc_save_load_color_session_only");
    const fs::path bp_path = dir / "color.blueprint";

    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Blueprint seed;
    seed = seed.with_id(interner.intern("color_seed"));
    seed = seed.with_name("Color Seed");

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("slider1");
    node.semantic.type = interner.intern("Slider");
    seed = seed.with_node(std::move(node));

    write_file(bp_path, bp2::BlueprintCodec::encode(seed, interner, arena, &registry));
    ASSERT_TRUE(doc.load(bp_path.string()));

    doc.set_node_color_for_scope(WindowScopeId::root(), doc.interner().intern("slider1"), editor::NodeColor{0.8f, 0.2f, 0.1f, 1.0f});
    EXPECT_NE(doc.title().find('*'), std::string::npos);

    ASSERT_TRUE(doc.save(bp_path.string()));

    Document loaded;
    loaded.setComponentRegistry(&registry);
    ASSERT_TRUE(loaded.load(bp_path.string()));

    const auto* loaded_node = require_node(loaded.model().current(), loaded.interner(), "slider1");
    ASSERT_NE(loaded_node, nullptr);
    ASSERT_TRUE(loaded_node->view.color.has_value());
    EXPECT_FLOAT_EQ(loaded_node->view.color->r, 0.8f);
    EXPECT_FLOAT_EQ(loaded_node->view.color->g, 0.2f);
    EXPECT_FLOAT_EQ(loaded_node->view.color->b, 0.1f);
    EXPECT_FLOAT_EQ(loaded_node->view.color->a, 1.0f);
    auto loaded_color = loaded.node_color_for_scope(WindowScopeId::root(), loaded.interner().intern("slider1"));
    ASSERT_TRUE(loaded_color.has_value());
    EXPECT_FLOAT_EQ(loaded_color->r, 0.8f);

    fs::remove_all(dir);
}

TEST(DocumentSafety, ExtractSaveLoadRoundTripPreservesEmbeddedBlueprintStructure) {
    namespace fs = std::filesystem;

    Document doc;
    ComponentRegistry registry = load_component_registry("library/");
    doc.setComponentRegistry(&registry);

    doc.model().replace_current(make_extract_roundtrip_fixture(doc.interner(), registry));

    std::string err;
    ASSERT_TRUE(doc.extractToBlueprint(
        {doc.interner().intern("a"), doc.interner().intern("b")},
        "extracted_blueprint_1",
        WindowScopeId::root(),
        &err,
        false)) << err;

    const fs::path dir = make_temp_dir("an24_doc_extract_roundtrip");
    const fs::path bp_path = dir / "extracted.blueprint";
    ASSERT_TRUE(doc.save(bp_path.string()));

    Document loaded;
    loaded.setComponentRegistry(&registry);
    ASSERT_TRUE(loaded.load(bp_path.string()));

    const auto* collapsed = require_node(loaded.model().current(), loaded.interner(), "extract_inst_1");
    ASSERT_NE(collapsed, nullptr);
    ASSERT_TRUE(collapsed->has_embedded_blueprint());
    ASSERT_NE(collapsed->blueprint_instance().source.inline_def(), nullptr);

    const auto& inner = *collapsed->blueprint_instance().source.inline_def();
    EXPECT_NE(inner.find_node(loaded.interner().lookup("a")), nullptr);
    EXPECT_NE(inner.find_node(loaded.interner().lookup("b")), nullptr);
    // Internal wire a.v_out → b.v_in plus two bridge-to-internal wires
    EXPECT_EQ(inner.wires().size(), 3u);
    // Bridge interface port names derive from the internal ports of boundary wires
    EXPECT_TRUE(inner.iface().has(loaded.interner().intern("v_in")));
    EXPECT_TRUE(inner.iface().has(loaded.interner().intern("v_out")));

    fs::remove_all(dir);
}

TEST(DocumentSafety, DeleteSaveLoadRoundTripRemovesNodeAndConnectedWires) {
    namespace fs = std::filesystem;

    Document doc;
    ComponentRegistry registry = load_component_registry("library/");
    doc.setComponentRegistry(&registry);

    bp2::Blueprint bp;
    bp = bp.with_id(doc.interner().intern("delete_roundtrip"));
    bp = bp.with_name("DeleteRoundtrip");
    bp = bp.with_interface(bp2::Interface({
        make_port(doc.interner(), "sink", Domain::Electrical, bp2::Direction::Output, PortType::V),
    }));
    bp = bp.with_node(make_typed_node(doc.interner(), registry, "bat", "ElectricalSource", 0.0f, 0.0f));
    bp = bp.with_node(make_typed_node(doc.interner(), registry, "res", "Resistor", 20.0f, 0.0f));
    auto sink = make_bridge_node(doc.interner(), "sink", false);
    sink.layout.x = 40.0f;
    sink.layout.y = 0.0f;
    bp = bp.with_node(std::move(sink));
    bp = bp.with_wire(make_wire(doc.interner(), "w0", "bat", "v_out", "res", "v_in"));
    bp = bp.with_wire(make_wire(doc.interner(), "w1", "res", "v_out", "sink", "port"));
    doc.model().replace_current(std::move(bp));

    doc.model().push_checkpoint();
    execute(doc.model(), doc.interner(), cmd_remove_node(doc.interner().intern("res"), {
        doc.interner().intern("w0"),
        doc.interner().intern("w1"),
    }));

    const fs::path dir = make_temp_dir("an24_doc_delete_roundtrip");
    const fs::path bp_path = dir / "deleted.blueprint";
    ASSERT_TRUE(doc.save(bp_path.string()));

    Document loaded;
    loaded.setComponentRegistry(&registry);
    ASSERT_TRUE(loaded.load(bp_path.string()));

    EXPECT_EQ(loaded.model().current().find_node(loaded.interner().lookup("res")), nullptr);
    EXPECT_EQ(loaded.model().current().wires().size(), 0u);

    fs::remove_all(dir);
}

TEST(DocumentSafety, InspectorEditedParamsRoundTripPreservesRebuiltWidgetAuthority) {
    namespace fs = std::filesystem;

    Document doc;
    ComponentRegistry registry = load_component_registry("library/");
    doc.setComponentRegistry(&registry);

    ui::StringInterner& I = doc.interner();
    bp2::Blueprint bp;
    bp = bp.with_id(I.intern("inspector_roundtrip"));
    bp = bp.with_name("Inspector Roundtrip");

    auto slider = make_typed_node(I, registry, "slider_rt", "Slider", 40.0f, 20.0f);
    slider.semantic.params[I.intern("min")] = 0.0f;
    slider.semantic.params[I.intern("max")] = 1.0f;
    bp = bp.with_node(std::move(slider));

    auto knob = make_typed_node(I, registry, "knob_rt", "KnobSwitch", 260.0f, 20.0f);
    knob.semantic.params[I.intern("positions")] = 2.0f;
    knob.semantic.params[I.intern("initial_position")] = 0.0f;
    bp = bp.with_node(std::move(knob));

    auto gauge = make_typed_node(I, registry, "gauge_rt", "Voltmeter", 480.0f, 20.0f);
    gauge.semantic.params[I.intern("min")] = 0.0f;
    gauge.semantic.params[I.intern("max")] = 28.0f;
    bp = bp.with_node(std::move(gauge));

    doc.model().replace_current(std::move(bp));
    doc.rebuildAllWindows();

    {
        const auto* slider_node = require_node(doc.model().current(), I, "slider_rt");
        ASSERT_NE(slider_node, nullptr);
        PropertiesWindow props;
        props.open(*slider_node, "slider_rt", create_editor_model_host(doc.model()), doc.interner(), &registry,
                   [&doc](const std::string&) { doc.rebuildAllWindows(); });
        props.set_pending_param("min", -10.0f);
        props.set_pending_param("max", 200.0f);
        props.apply();
    }

    {
        const auto* knob_node = require_node(doc.model().current(), I, "knob_rt");
        ASSERT_NE(knob_node, nullptr);
        PropertiesWindow props;
        props.open(*knob_node, "knob_rt", create_editor_model_host(doc.model()), doc.interner(), &registry,
                   [&doc](const std::string&) { doc.rebuildAllWindows(); });
        props.set_pending_param("positions", 5.0f);
        props.apply();
    }

    {
        const auto* gauge_node = require_node(doc.model().current(), I, "gauge_rt");
        ASSERT_NE(gauge_node, nullptr);
        PropertiesWindow props;
        props.open(*gauge_node, "gauge_rt", create_editor_model_host(doc.model()), doc.interner(), &registry,
                   [&doc](const std::string&) { doc.rebuildAllWindows(); });
        props.set_pending_param("min", -20.0f);
        props.set_pending_param("max", 60.0f);
        props.apply();
    }

    const fs::path dir = make_temp_dir("an24_doc_inspector_param_roundtrip");
    const fs::path bp_path = dir / "inspector_roundtrip.blueprint";
    ASSERT_TRUE(doc.save(bp_path.string()));

    Document loaded;
    loaded.setComponentRegistry(&registry);
    ASSERT_TRUE(loaded.load(bp_path.string()));

    const auto* slider_loaded = require_node(loaded.model().current(), loaded.interner(), "slider_rt");
    const auto* knob_loaded = require_node(loaded.model().current(), loaded.interner(), "knob_rt");
    const auto* gauge_loaded = require_node(loaded.model().current(), loaded.interner(), "gauge_rt");
    ASSERT_NE(slider_loaded, nullptr);
    ASSERT_NE(knob_loaded, nullptr);
    ASSERT_NE(gauge_loaded, nullptr);

    EXPECT_FLOAT_EQ(resolve_node_content(*slider_loaded, registry, loaded.interner()).min, -10.0f);
    EXPECT_FLOAT_EQ(resolve_node_content(*slider_loaded, registry, loaded.interner()).max, 200.0f);
    EXPECT_FLOAT_EQ(resolve_node_content(*knob_loaded, registry, loaded.interner()).max, 5.0f);
    EXPECT_FLOAT_EQ(resolve_node_content(*gauge_loaded, registry, loaded.interner()).min, -20.0f);
    EXPECT_FLOAT_EQ(resolve_node_content(*gauge_loaded, registry, loaded.interner()).max, 60.0f);

    auto* loaded_win = loaded.windowManager().find(WindowScopeId::root());
    ASSERT_NE(loaded_win, nullptr);
    loaded.input().rebuild_snapshot();

    auto* slider_widget = dynamic_cast<visual::NodeWidget*>(loaded_win->scene.find("slider_rt"));
    auto* knob_widget = dynamic_cast<visual::NodeWidget*>(loaded_win->scene.find("knob_rt"));
    auto* gauge_widget = dynamic_cast<visual::NodeWidget*>(loaded_win->scene.find("gauge_rt"));
    ASSERT_NE(slider_widget, nullptr);
    ASSERT_NE(knob_widget, nullptr);
    ASSERT_NE(gauge_widget, nullptr);

    NodeContent slider_content = slider_widget->currentContent();
    NodeContent knob_content = knob_widget->currentContent();
    NodeContent gauge_content = gauge_widget->currentContent();
    EXPECT_FLOAT_EQ(slider_content.min, -10.0f);
    EXPECT_FLOAT_EQ(slider_content.max, 200.0f);
    EXPECT_FLOAT_EQ(knob_content.max, 5.0f);
    EXPECT_FLOAT_EQ(gauge_content.min, -20.0f);
    EXPECT_FLOAT_EQ(gauge_content.max, 60.0f);

    Viewport vp;
    vp.zoom = 1.0f;
    vp.pan = Pt(0.0f, 0.0f);
    auto host = create_editor_model_host(loaded.model());
    CanvasInput input(loaded.scene(), vp, host.get(), loaded.interner(), loaded.arena(), WindowScopeId::root(), &registry);
    input.simulation_mode = true;
    input.rebuild_snapshot();
    Pt canvas_min(0.0f, 0.0f);

    {
        Bounds cb = slider_widget->contentBounds();
        ASSERT_GT(cb.w, 0.0f);
        ASSERT_GT(cb.h, 0.0f);
        const auto& sem_snapshot = slider_widget->content_semantic_snapshot();
        auto sem_hit = editor::presentation::hit_test_semantic_scene(
            sem_snapshot,
            Pt(cb.x + cb.w * 0.5f, cb.y + cb.h * 0.5f));
        auto* sem_content = std::get_if<editor::presentation::SemanticHitContentRegion>(&sem_hit);
        ASSERT_NE(sem_content, nullptr);
        ASSERT_FALSE(sem_content->object->interactions.empty());
        EXPECT_EQ(sem_content->object->interactions[0].kind,
                  editor::presentation::InteractionKind::DragScalar);
        Pt wpos = slider_widget->worldPos();
        Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);
        Pt node_min = slider_widget->worldMin();
        Pt node_max = slider_widget->worldMax();
        ASSERT_GE(click_world.x, node_min.x);
        ASSERT_GE(click_world.y, node_min.y);
        ASSERT_LE(click_world.x, node_max.x);
        ASSERT_LE(click_world.y, node_max.y);
        auto scene_snapshot = editor::presentation::build_canvas_scene_snapshot(loaded.scene(), loaded.interner());
        auto scene_hit = editor::presentation::hit_test_canvas_scene(scene_snapshot, click_world);
        auto* scene_hit_node = std::get_if<visual::HitNode>(&scene_hit);
        ASSERT_NE(scene_hit_node, nullptr);
        EXPECT_EQ(scene_hit_node->node_id, loaded.interner().intern("slider_rt"));
        ASSERT_TRUE(scene_hit_node->content_interaction.has_value());
        EXPECT_EQ(scene_hit_node->content_interaction->kind,
                  editor::presentation::InteractionKind::DragScalar);
        auto down = input.on_mouse_down(click_world, MouseButton::Left, canvas_min);
        ASSERT_EQ(down.slider_node_id, "slider_rt");
        auto drag = input.on_mouse_drag(MouseButton::Left, Pt(500.0f, 0.0f), canvas_min);
        EXPECT_FLOAT_EQ(drag.slider_value, 200.0f);
        input.on_mouse_up(MouseButton::Left, click_world + Pt(500.0f, 0.0f), canvas_min);
    }

    {
        Bounds cb = knob_widget->contentBounds();
        ASSERT_GT(cb.w, 0.0f);
        ASSERT_GT(cb.h, 0.0f);
        const auto& sem_snapshot = knob_widget->content_semantic_snapshot();
        auto sem_hit = editor::presentation::hit_test_semantic_scene(
            sem_snapshot,
            Pt(cb.x + cb.w * 0.5f, cb.y + cb.h * 0.5f));
        auto* sem_content = std::get_if<editor::presentation::SemanticHitContentRegion>(&sem_hit);
        ASSERT_NE(sem_content, nullptr);
        ASSERT_FALSE(sem_content->object->interactions.empty());
        EXPECT_EQ(sem_content->object->interactions[0].kind,
                  editor::presentation::InteractionKind::DragDiscrete);
        Pt wpos = knob_widget->worldPos();
        Pt click_world(wpos.x + cb.x + cb.w * 0.5f, wpos.y + cb.y + cb.h * 0.5f);
        auto down = input.on_mouse_down(click_world, MouseButton::Left, canvas_min);
        ASSERT_EQ(down.knob_node_id, "knob_rt");
        auto drag = input.on_mouse_drag(MouseButton::Left, Pt(120.0f, 0.0f), canvas_min);
        EXPECT_EQ(drag.knob_position, 4);
        input.on_mouse_up(MouseButton::Left, click_world + Pt(120.0f, 0.0f), canvas_min);
    }

    fs::remove_all(dir);
}

TEST(DocumentSafety, InspectorEditedAzsClosedRoundTripPreservesVerticalToggleAuthority) {
    namespace fs = std::filesystem;

    Document doc;
    ComponentRegistry registry = load_component_registry("library/");
    doc.setComponentRegistry(&registry);

    ui::StringInterner& I = doc.interner();
    bp2::Blueprint bp;
    bp = bp.with_id(I.intern("azs_roundtrip"));
    bp = bp.with_name("AZS Roundtrip");

    auto azs = make_typed_node(I, registry, "azs_rt", "AZS", 40.0f, 20.0f);
    azs.semantic.params[I.intern("closed")] = 0.0f;
    bp = bp.with_node(std::move(azs));

    doc.model().replace_current(std::move(bp));
    doc.rebuildAllWindows();

    {
        const auto* azs_node = require_node(doc.model().current(), I, "azs_rt");
        ASSERT_NE(azs_node, nullptr);
        PropertiesWindow props;
        props.open(*azs_node, "azs_rt", create_editor_model_host(doc.model()), doc.interner(), &registry,
                   [&doc](const std::string&) { doc.rebuildAllWindows(); });
        props.set_pending_param("closed", 1.0f);
        props.apply();
    }

    const fs::path dir = make_temp_dir("an24_doc_azs_roundtrip");
    const fs::path bp_path = dir / "azs_roundtrip.blueprint";
    ASSERT_TRUE(doc.save(bp_path.string()));

    Document loaded;
    loaded.setComponentRegistry(&registry);
    ASSERT_TRUE(loaded.load(bp_path.string()));

    const auto* loaded_azs = require_node(loaded.model().current(), loaded.interner(), "azs_rt");
    ASSERT_NE(loaded_azs, nullptr);
    EXPECT_EQ(loaded_azs->semantic.string_params.at("closed"), "true");

    auto* loaded_win = loaded.windowManager().find(WindowScopeId::root());
    ASSERT_NE(loaded_win, nullptr);
    auto* widget = dynamic_cast<visual::NodeWidget*>(loaded_win->scene.find("azs_rt"));
    ASSERT_NE(widget, nullptr);

    NodeContent content = widget->currentContent();
    EXPECT_EQ(content.type, bp2::NodeContentType::VerticalToggle);
    EXPECT_TRUE(content.state);

    fs::remove_all(dir);
}

TEST(DocumentSafety, InspectorEditedRelayClosedRoundTripPreservesSwitchAuthority) {
    namespace fs = std::filesystem;

    Document doc;
    ComponentRegistry registry = load_component_registry("library/");
    doc.setComponentRegistry(&registry);

    ui::StringInterner& I = doc.interner();
    bp2::Blueprint bp;
    bp = bp.with_id(I.intern("relay_roundtrip"));
    bp = bp.with_name("Relay Roundtrip");

    auto relay = make_typed_node(I, registry, "relay_rt", "Relay", 40.0f, 20.0f);
    relay.semantic.params[I.intern("closed")] = 0.0f;
    bp = bp.with_node(std::move(relay));

    doc.model().replace_current(std::move(bp));
    doc.rebuildAllWindows();

    {
        const auto* relay_node = require_node(doc.model().current(), I, "relay_rt");
        ASSERT_NE(relay_node, nullptr);
        PropertiesWindow props;
        props.open(*relay_node, "relay_rt", create_editor_model_host(doc.model()), doc.interner(), &registry,
                   [&doc](const std::string&) { doc.rebuildAllWindows(); });
        props.set_pending_param("closed", 1.0f);
        props.apply();
    }

    const fs::path dir = make_temp_dir("an24_doc_relay_roundtrip");
    const fs::path bp_path = dir / "relay_roundtrip.blueprint";
    ASSERT_TRUE(doc.save(bp_path.string()));

    Document loaded;
    loaded.setComponentRegistry(&registry);
    ASSERT_TRUE(loaded.load(bp_path.string()));

    const auto* loaded_relay = require_node(loaded.model().current(), loaded.interner(), "relay_rt");
    ASSERT_NE(loaded_relay, nullptr);
    EXPECT_EQ(loaded_relay->semantic.string_params.at("closed"), "true");

    auto* loaded_win = loaded.windowManager().find(WindowScopeId::root());
    ASSERT_NE(loaded_win, nullptr);
    auto* widget = dynamic_cast<visual::NodeWidget*>(loaded_win->scene.find("relay_rt"));
    ASSERT_NE(widget, nullptr);

    NodeContent content = widget->currentContent();
    EXPECT_EQ(content.type, bp2::NodeContentType::Switch);
    EXPECT_TRUE(content.state);

    fs::remove_all(dir);
}

TEST(DocumentSafety, InspectorEditedHoldButtonParamsRoundTripPreservesSwitchLikeAuthority) {
    namespace fs = std::filesystem;

    Document doc;
    ComponentRegistry registry = load_component_registry("library/");
    doc.setComponentRegistry(&registry);

    ui::StringInterner& I = doc.interner();
    bp2::Blueprint bp;
    bp = bp.with_id(I.intern("hold_button_roundtrip"));
    bp = bp.with_name("HoldButton Roundtrip");

    auto btn = make_typed_node(I, registry, "btn_rt", "HoldButton", 40.0f, 20.0f);
    btn.semantic.params[I.intern("idle")] = 0.0f;
    btn.semantic.params[I.intern("g_closed")] = 1000.0f;
    bp = bp.with_node(std::move(btn));

    doc.model().replace_current(std::move(bp));
    doc.rebuildAllWindows();

    {
        const auto* btn_node = require_node(doc.model().current(), I, "btn_rt");
        ASSERT_NE(btn_node, nullptr);
        PropertiesWindow props;
        props.open(*btn_node, "btn_rt", create_editor_model_host(doc.model()), doc.interner(), &registry,
                   [&doc](const std::string&) { doc.rebuildAllWindows(); });
        props.set_pending_param("idle", 2.5f);
        props.set_pending_param("g_closed", 321.0f);
        props.apply();
    }

    const fs::path dir = make_temp_dir("an24_doc_hold_button_roundtrip");
    const fs::path bp_path = dir / "hold_button_roundtrip.blueprint";
    ASSERT_TRUE(doc.save(bp_path.string()));

    Document loaded;
    loaded.setComponentRegistry(&registry);
    ASSERT_TRUE(loaded.load(bp_path.string()));

    const auto* loaded_btn = require_node(loaded.model().current(), loaded.interner(), "btn_rt");
    ASSERT_NE(loaded_btn, nullptr);
    EXPECT_EQ(resolve_node_content(*loaded_btn, registry, loaded.interner()).type, bp2::NodeContentType::Switch);
    EXPECT_FLOAT_EQ(loaded_btn->semantic.params.at(loaded.interner().intern("idle")), 2.5f);
    EXPECT_FLOAT_EQ(loaded_btn->semantic.params.at(loaded.interner().intern("g_closed")), 321.0f);

    auto* loaded_win = loaded.windowManager().find(WindowScopeId::root());
    ASSERT_NE(loaded_win, nullptr);
    auto* widget = dynamic_cast<visual::NodeWidget*>(loaded_win->scene.find("btn_rt"));
    ASSERT_NE(widget, nullptr);

    NodeContent content = widget->currentContent();
    EXPECT_EQ(content.type, bp2::NodeContentType::Switch);
    EXPECT_FALSE(content.state);

    fs::remove_all(dir);
}

TEST(DocumentSafety, SaveEmitsCanonicalDocumentWithoutForbiddenFieldsAndSortedWires) {
    namespace fs = std::filesystem;

    Document doc;
    ComponentRegistry registry = load_component_registry("library/");
    doc.setComponentRegistry(&registry);
    doc.model().replace_current(make_extract_roundtrip_fixture(doc.interner(), registry));

    const fs::path dir = make_temp_dir("an24_doc_canonical_save_scan");
    const fs::path bp_path = dir / "canonical.blueprint";
    ASSERT_TRUE(doc.save(bp_path.string()));

    std::ifstream in(bp_path);
    ASSERT_TRUE(in.is_open());
    nlohmann::json j;
    in >> j;

    EXPECT_EQ(j["format"], "blueprint");
    EXPECT_EQ(j["version"], 1);
    EXPECT_FALSE(j.contains("nested"));
    EXPECT_FALSE(j.contains("pan_x"));
    EXPECT_FALSE(j.contains("pan_y"));
    EXPECT_FALSE(j.contains("zoom"));
    EXPECT_FALSE(j.contains("grid_step"));
    EXPECT_FALSE(j.contains("owner_scope"));
    EXPECT_FALSE(j.contains("group_id"));
    EXPECT_FALSE(j.contains("resolved_iface"));
    EXPECT_FALSE(j.contains("blueprint_path"));

    ASSERT_TRUE(j.contains("wires"));
    ASSERT_EQ(j["wires"].size(), 3u);
    EXPECT_EQ(j["wires"][0]["id"], "w0");
    EXPECT_EQ(j["wires"][1]["id"], "w1");
    EXPECT_EQ(j["wires"][2]["id"], "w2");

    for (const auto& node : j["nodes"]) {
        EXPECT_FALSE(node.contains("content"));
        EXPECT_FALSE(node.contains("render_hint"));
        EXPECT_FALSE(node.contains("resolved_iface"));
        EXPECT_FALSE(node.contains("blueprint_path"));
    }

    fs::remove_all(dir);
}

TEST(DocumentSafety, AddBlueprintToEmbeddedScopeAddsNodeInsideInlineBlueprint) {
    Document doc;
    ComponentRegistry registry = load_component_registry("library/");
    doc.setComponentRegistry(&registry);

    ui::StringInterner& I = doc.interner();

    bp2::Blueprint inner;
    inner = inner.with_id(I.intern("inner_bp"));
    inner = inner.with_name("Inner");

    bp2::Blueprint::Node host;
    host.semantic.id = I.intern("group_1");
    host.semantic.type = I.intern("Group");
    host.view.name = "group_1";
    host.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(inner.with_id(I.intern("Group"))))
    };

    bp2::Blueprint root;
    root = root.with_id(I.intern("root_bp"));
    root = root.with_name("Root");
    root = root.with_node(std::move(host));
    doc.model().replace_current(std::move(root));

    ASSERT_NO_THROW(doc.addBlueprint("FirstOrderLag", Pt{64.0f, 64.0f}, WindowScopeId::embedded({doc.interner().intern("group_1")}), registry));

    const auto* root_added = doc.model().current().find_node(I.lookup("firstorderlag_1"));
    EXPECT_EQ(root_added, nullptr);

    const auto* updated_host = doc.model().current().find_node(I.lookup("group_1"));
    ASSERT_NE(updated_host, nullptr);
    ASSERT_TRUE(updated_host->is_blueprint_instance());
    ASSERT_TRUE(updated_host->blueprint_instance().source.is_embedded());
    const auto* inline_bp = updated_host->blueprint_instance().source.inline_def();
    ASSERT_NE(inline_bp, nullptr);
    EXPECT_NE(inline_bp->find_node(I.lookup("firstorderlag_1")), nullptr);
}

// ============================================================================
// Integration: newly inserted node must be selectable via Document lifecycle
// ============================================================================

TEST(DocumentSafety, NewlyAddedComponentIsImmediatelySelectableViaDocument) {
    Document doc;
    ComponentRegistry registry = load_component_registry("library/");
    doc.setComponentRegistry(&registry);

    // Start with an empty blueprint
    bp2::Blueprint bp;
    bp = bp.with_id(doc.interner().intern("select_test"));
    bp = bp.with_name("Select Test");
    doc.model().replace_current(std::move(bp));
    doc.rebuildAllWindows();

    // Add a component through the full Document path
    doc.addComponent("Resistor", Pt{200.0f, 200.0f}, WindowScopeId::root(), registry);

    // After addComponent, the scene should have the new node widget
    const auto& scene = doc.scene();
    const auto& blueprint = doc.blueprint();
    ASSERT_EQ(blueprint.nodes().size(), 1u);

    // Get the new node's ID
    const auto& new_node = blueprint.nodes()[0];
    std::string_view node_id_sv = doc.interner().resolve(new_node.semantic.id);
    ASSERT_FALSE(node_id_sv.empty());

    // Find the widget in the scene
    auto* widget = scene.find(node_id_sv);
    ASSERT_NE(widget, nullptr) << "Widget for newly added node must exist in scene";
    EXPECT_GT(widget->size().x, 0.0f) << "Widget must have non-zero width";
    EXPECT_GT(widget->size().y, 0.0f) << "Widget must have non-zero height";

    // Hit test at the center of the widget using the retained snapshot
    Pt node_center = widget->worldPos() + widget->size() * 0.5f;
    const Pt canvas_min(0.0f, 0.0f);

    // Click on the new node - should select it
    auto& input = doc.input();
    input.on_mouse_down(node_center, MouseButton::Left, canvas_min);
    EXPECT_EQ(input.state(), InputState::DraggingNode)
        << "Clicking on newly added node must enter DraggingNode";
    input.on_mouse_up(MouseButton::Left, node_center, canvas_min);

    ASSERT_EQ(input.selected_node_ids().size(), 1u);

    // Deselect by clicking empty space
    Pt empty(900.0f, 900.0f);
    input.on_mouse_down(empty, MouseButton::Left, canvas_min);
    input.on_mouse_up(MouseButton::Left, empty, canvas_min);
    EXPECT_TRUE(input.selected_node_ids().empty());

    // Click the node AGAIN — this is the reported bug
    input.on_mouse_down(node_center, MouseButton::Left, canvas_min);
    EXPECT_EQ(input.state(), InputState::DraggingNode)
        << "Re-clicking node after deselect must still select it (regression bug)";
    input.on_mouse_up(MouseButton::Left, node_center, canvas_min);
    ASSERT_EQ(input.selected_node_ids().size(), 1u);
}

// ============================================================================
// Canonical node color persistence and widget reapply round-trip
// ============================================================================

TEST(DocumentSafety, CanonicalNodeColorsPersistAndReapplyRoundTrip) {
    ComponentRegistry registry = load_component_registry("library/");
    namespace fs = std::filesystem;

    const fs::path dir = make_temp_dir("an24_ws_node_colors");
    const fs::path bp_path = dir / "colors.blueprint";

    // --- Phase 1: create document, set colors, save ---
    {
        Document doc;
        doc.setComponentRegistry(&registry);

        bp2::Blueprint bp;
        bp = bp.with_id(doc.interner().intern("color_test"));
        bp = bp.with_name("Color Test");

        bp = bp.with_node(make_typed_node(doc.interner(), registry, "r1", "Resistor", 20.0f, 20.0f));
        bp = bp.with_node(make_typed_node(doc.interner(), registry, "r2", "Resistor", 60.0f, 20.0f));

        doc.model().replace_current(std::move(bp));
        doc.rebuildAllWindows();

        // Set colors on both nodes
        doc.set_node_color_for_scope(WindowScopeId::root(), doc.interner().intern("r1"),
                                     editor::NodeColor{0.9f, 0.1f, 0.2f, 1.0f});
        doc.set_node_color_for_scope(WindowScopeId::root(), doc.interner().intern("r2"),
                                     editor::NodeColor{0.2f, 0.8f, 0.3f, 1.0f});

        // Save blueprint only — node color is canonical authored state.
        ASSERT_TRUE(doc.save(bp_path.string()));
    }

    // --- Phase 2: load into fresh document, verify colors restored ---
    {
        Document doc2;
        doc2.setComponentRegistry(&registry);

        ASSERT_TRUE(doc2.load(bp_path.string()));

        // Verify canonical node colors were restored from the blueprint.
        auto r1_color = doc2.node_color_for_scope(WindowScopeId::root(), doc2.interner().intern("r1"));
        ASSERT_TRUE(r1_color.has_value());
        EXPECT_FLOAT_EQ(r1_color->r, 0.9f);
        EXPECT_FLOAT_EQ(r1_color->g, 0.1f);
        EXPECT_FLOAT_EQ(r1_color->b, 0.2f);

        auto r2_color = doc2.node_color_for_scope(WindowScopeId::root(), doc2.interner().intern("r2"));
        ASSERT_TRUE(r2_color.has_value());
        EXPECT_FLOAT_EQ(r2_color->r, 0.2f);
        EXPECT_FLOAT_EQ(r2_color->g, 0.8f);
        EXPECT_FLOAT_EQ(r2_color->b, 0.3f);

        // Verify widgets reflect restored canonical colors after scene rebuild.
        auto* win = doc2.windowManager().find(WindowScopeId::root());
        ASSERT_NE(win, nullptr);

        auto* r1_widget = dynamic_cast<visual::NodeWidget*>(win->scene.find("r1"));
        ASSERT_NE(r1_widget, nullptr);
        const uint32_t expected_r1_color = editor::NodeColor{0.9f, 0.1f, 0.2f, 1.0f}.to_uint32();
        ASSERT_TRUE(r1_widget->customColor().has_value());
        EXPECT_EQ(r1_widget->customColor().value(), expected_r1_color);

        auto* r2_widget = dynamic_cast<visual::NodeWidget*>(win->scene.find("r2"));
        ASSERT_NE(r2_widget, nullptr);
        const uint32_t expected_r2_color = editor::NodeColor{0.2f, 0.8f, 0.3f, 1.0f}.to_uint32();
        ASSERT_TRUE(r2_widget->customColor().has_value());
        EXPECT_EQ(r2_widget->customColor().value(), expected_r2_color);
    }

    fs::remove_all(dir);
}

TEST(DocumentSafety, WorkspaceSessionOmitsNodeColorsWhenNoneSet) {
    ComponentRegistry registry = load_component_registry("library/");
    namespace fs = std::filesystem;

    const fs::path dir = make_temp_dir("an24_ws_no_colors");
    const fs::path bp_path = dir / "no_colors.blueprint";

    {
        Document doc;
        doc.setComponentRegistry(&registry);

        ui::StringInterner& I = doc.interner();
        bp2::Blueprint bp;
        bp = bp.with_id(I.intern("no_color_test"));
        bp = bp.with_name("No Color Test");

        auto res = make_typed_node(I, registry, "r1", "Resistor", 20.0f, 20.0f);
        bp = bp.with_node(std::move(res));

        doc.model().replace_current(std::move(bp));
        doc.rebuildAllWindows();

        ASSERT_TRUE(doc.save(bp_path.string()));
        ASSERT_TRUE(doc.saveWorkspaceSession());
    }

    // Verify workspace JSON has no node_colors key — color is no longer workspace authority.
    {
        fs::path ws_path = dir / "no_colors.workspace.json";
        std::ifstream in(ws_path);
        ASSERT_TRUE(in.is_open());
        nlohmann::json j;
        in >> j;
        EXPECT_FALSE(j.contains("node_colors"));
    }

    // Loading still works and produces empty color state
    {
        Document doc2;
        doc2.setComponentRegistry(&registry);
        ASSERT_TRUE(doc2.load(bp_path.string()));
        ASSERT_TRUE(doc2.loadWorkspaceSession());

        auto color = doc2.node_color_for_scope(WindowScopeId::root(), doc2.interner().intern("r1"));
        EXPECT_FALSE(color.has_value());
    }

    fs::remove_all(dir);
}

TEST(DocumentSafety, CanonicalNodeColorIsSerializedInBlueprintJson) {
    ComponentRegistry registry = load_component_registry("library/");
    namespace fs = std::filesystem;

    const fs::path dir = make_temp_dir("an24_canonical_node_color_json");
    const fs::path bp_path = dir / "persist.blueprint";

    Document doc;
    doc.setComponentRegistry(&registry);

    bp2::Blueprint bp;
    bp = bp.with_id(doc.interner().intern("persist_color"));
    bp = bp.with_name("Persist Color");
    bp = bp.with_node(make_typed_node(doc.interner(), registry, "switch_1", "Resistor", 10.0f, 20.0f));
    doc.model().replace_current(std::move(bp));

    doc.set_node_color_for_scope(WindowScopeId::root(), doc.interner().intern("switch_1"),
                                 editor::NodeColor{0.8f, 0.2f, 0.3f, 1.0f});
    ASSERT_TRUE(doc.save(bp_path.string()));

    std::ifstream in(bp_path);
    ASSERT_TRUE(in.is_open());
    nlohmann::json j;
    in >> j;

    ASSERT_TRUE(j.contains("nodes"));
    ASSERT_EQ(j["nodes"].size(), 1u);
    ASSERT_TRUE(j["nodes"][0].contains("color"));
    EXPECT_FLOAT_EQ(j["nodes"][0]["color"]["r"], 0.8f);
    EXPECT_FLOAT_EQ(j["nodes"][0]["color"]["g"], 0.2f);
    EXPECT_FLOAT_EQ(j["nodes"][0]["color"]["b"], 0.3f);
    EXPECT_FLOAT_EQ(j["nodes"][0]["color"]["a"], 1.0f);

    fs::remove_all(dir);
}

TEST(DocumentSafety, SettingSameRootNodeColorTwiceDoesNotCreateExtraDirtyHistory) {
    ComponentRegistry registry = load_component_registry("library/");

    Document doc;
    doc.setComponentRegistry(&registry);

    bp2::Blueprint bp;
    bp = bp.with_id(doc.interner().intern("same_color_root"));
    bp = bp.with_name("Same Color Root");
    bp = bp.with_node(make_typed_node(doc.interner(), registry, "n1", "Resistor", 10.0f, 20.0f));
    doc.model().replace_current(std::move(bp));

    doc.set_node_color_for_scope(WindowScopeId::root(), doc.interner().intern("n1"),
                                 editor::NodeColor{0.4f, 0.5f, 0.6f, 1.0f});
    const size_t undo_after_first = doc.model().undo_depth();
    ASSERT_TRUE(doc.model().is_dirty());

    doc.set_node_color_for_scope(WindowScopeId::root(), doc.interner().intern("n1"),
                                 editor::NodeColor{0.4f, 0.5f, 0.6f, 1.0f});
    EXPECT_EQ(doc.model().undo_depth(), undo_after_first);
}

TEST(DocumentSafety, EmbeddedNodeColorRoundTripMarksDirtyAndRestoresAfterLoad) {
    namespace fs = std::filesystem;

    ComponentRegistry registry = load_component_registry("library/");
    Document doc;
    doc.setComponentRegistry(&registry);

    bp2::Blueprint inner;
    inner = inner.with_id(doc.interner().intern("InnerColor"));
    inner = inner.with_name("Inner Color");
    inner = inner.with_node(make_typed_node(doc.interner(), registry, "inner_r", "Resistor", 30.0f, 40.0f));

    bp2::Blueprint::Node host;
    host.semantic.id = doc.interner().intern("group_1");
    host.semantic.type = doc.interner().intern("InnerColor");
    host.view.name = "group_1";
    host.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            std::make_unique<bp2::Blueprint>(std::move(inner)))
    };

    bp2::Blueprint root;
    root = root.with_id(doc.interner().intern("embedded_color_root"));
    root = root.with_name("Embedded Color Root");
    root = root.with_node(std::move(host));
    doc.model().replace_current(std::move(root));
    doc.rebuildAllWindows();

    doc.set_node_color_for_scope(WindowScopeId::embedded({doc.interner().intern("group_1")}), doc.interner().intern("inner_r"),
                                 editor::NodeColor{0.7f, 0.3f, 0.2f, 1.0f});
    EXPECT_TRUE(doc.model().is_dirty());

    const auto* host_after = require_node(doc.model().current(), doc.interner(), "group_1");
    ASSERT_NE(host_after, nullptr);
    ASSERT_TRUE(host_after->has_embedded_blueprint());
    const auto* inner_node = host_after->blueprint_instance().source.inline_def()->find_node(doc.interner().lookup("inner_r"));
    ASSERT_NE(inner_node, nullptr);
    ASSERT_TRUE(inner_node->view.color.has_value());

    const fs::path dir = make_temp_dir("an24_embedded_color_roundtrip");
    const fs::path bp_path = dir / "embedded_color.blueprint";
    ASSERT_TRUE(doc.save(bp_path.string()));

    Document loaded;
    loaded.setComponentRegistry(&registry);
    ASSERT_TRUE(loaded.load(bp_path.string()));

    auto loaded_color = loaded.node_color_for_scope(WindowScopeId::embedded({loaded.interner().intern("group_1")}), loaded.interner().intern("inner_r"));
    ASSERT_TRUE(loaded_color.has_value());
    EXPECT_FLOAT_EQ(loaded_color->r, 0.7f);
    EXPECT_FLOAT_EQ(loaded_color->g, 0.3f);
    EXPECT_FLOAT_EQ(loaded_color->b, 0.2f);
    EXPECT_FLOAT_EQ(loaded_color->a, 1.0f);

    fs::remove_all(dir);
}

TEST(DocumentSafety, WalkBlueprintNodesProducesNestedInstancePaths) {
    ui::StringInterner interner;

    // Build a 3-level nesting: root → group_A → group_B → leaf
    bp2::Blueprint::Node leaf;
    leaf.semantic.id = interner.intern("leaf");
    leaf.semantic.type = interner.intern("Battery");
    leaf.semantic.params[interner.intern("v_nominal")] = 24.0f;

    bp2::Blueprint inner_bp;
    inner_bp = inner_bp.with_node(std::move(leaf));
    inner_bp = inner_bp.with_id(interner.intern("InnerBP"));

    bp2::Blueprint::Node group_b;
    group_b.semantic.id = interner.intern("group_B");
    group_b.semantic.type = interner.intern("Composite");
    group_b.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            std::make_unique<bp2::Blueprint>(std::move(inner_bp)))
    };

    bp2::Blueprint mid_bp;
    mid_bp = mid_bp.with_node(std::move(group_b));
    mid_bp = mid_bp.with_id(interner.intern("MidBP"));

    bp2::Blueprint::Node group_a;
    group_a.semantic.id = interner.intern("group_A");
    group_a.semantic.type = interner.intern("Composite");
    group_a.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            std::make_unique<bp2::Blueprint>(std::move(mid_bp)))
    };

    bp2::Blueprint root_bp;
    root_bp = root_bp.with_node(std::move(group_a));

    // Walk and collect (instance_path, node_id) pairs
    std::vector<std::pair<std::vector<std::string>, std::string>> visited;
    std::vector<ui::InternedId> path;
    editor::walk_blueprint_nodes(root_bp, path, [&](const bp2::Blueprint::Node& node,
                                             std::span<const ui::InternedId> instance_path) {
        std::vector<std::string> path_strs;
        for (auto seg : instance_path) {
            path_strs.push_back(std::string(interner.resolve(seg)));
        }
        visited.push_back({path_strs, std::string(interner.resolve(node.semantic.id))});
    });

    ASSERT_EQ(visited.size(), 3u);

    // group_A at root level — empty instance path
    EXPECT_TRUE(visited[0].first.empty());
    EXPECT_EQ(visited[0].second, "group_A");

    // group_B inside group_A — instance_path = ["group_A"]
    ASSERT_EQ(visited[1].first.size(), 1u);
    EXPECT_EQ(visited[1].first[0], "group_A");
    EXPECT_EQ(visited[1].second, "group_B");

    // leaf inside group_B inside group_A — instance_path = ["group_A", "group_B"]
    ASSERT_EQ(visited[2].first.size(), 2u);
    EXPECT_EQ(visited[2].first[0], "group_A");
    EXPECT_EQ(visited[2].first[1], "group_B");
    EXPECT_EQ(visited[2].second, "leaf");
}
