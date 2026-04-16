#include <gtest/gtest.h>

#include "editor/document.h"
#include "editor/commands/blueprint_checksum.h"
#include "editor/commands/commands.h"
#include "editor/commands/extract_blueprint.h"
#include "editor/input/canvas_input.h"
#include "editor/input/input_types.h"
#include "editor/visual/node/visual_node.h"
#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/interface/type_definition_interface.h"
#include "blueprint_v2/library/library_index.h"
#include "json_parser/json_parser.h"

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

/// Build a Component node whose interface matches the TypeRegistry exactly.
bp2::Blueprint::Node make_typed_node(ui::StringInterner& I,
                                     const TypeRegistry& registry,
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
        n.semantic.iface = bp2::interface_from_type_definition(*def, I);
    }
    return n;
}

/// Build an extract-roundtrip fixture using real registered types.
///
/// Topology (all Electrical domain):
///   ext_in (BlueprintInput)  --port-->  a (ElectricalSource) .v_in
///                                       a.v_out  -->  b (Resistor) .v_in
///   ext_out (BlueprintOutput) <--port--  b.v_out
///
/// Wire naming:
///   w0: ext_in.port  → a.v_in
///   w1: a.v_out      → b.v_in       (internal to selection {a,b})
///   w2: b.v_out      → ext_out.port
bp2::Blueprint make_extract_roundtrip_fixture(ui::StringInterner& I,
                                              const TypeRegistry& registry) {
    bp2::Blueprint bp;
    bp = bp.with_id(I.intern("bp_extract_doc"));
    bp = bp.with_name("ExtractDoc");

    bp = bp.with_node(make_typed_node(I, registry, "ext_in",  "BlueprintInput",  0.0f,  0.0f));
    bp = bp.with_node(make_typed_node(I, registry, "a",       "ElectricalSource", 20.0f, 0.0f));
    bp = bp.with_node(make_typed_node(I, registry, "b",       "Resistor",        40.0f, 0.0f));
    bp = bp.with_node(make_typed_node(I, registry, "ext_out", "BlueprintOutput", 60.0f, 0.0f));

    // BlueprintInput.port → ElectricalSource.v_in
    bp = bp.with_wire(make_wire(I, "w0", "ext_in", "port", "a",       "v_in"));
    // ElectricalSource.v_out → Resistor.v_in  (internal wire)
    bp = bp.with_wire(make_wire(I, "w1", "a",      "v_out", "b",      "v_in"));
    // Resistor.v_out → BlueprintOutput.port
    bp = bp.with_wire(make_wire(I, "w2", "b",      "v_out", "ext_out", "port"));
    return bp;
}

} // namespace

TEST(DocumentSafety, AddComponentUnknownTypeDoesNotCrashOrMutate) {
    Document doc;
    TypeRegistry registry = load_type_registry("library/");

    const size_t before_nodes = doc.model().current().nodes().size();
    const size_t before_wires = doc.model().current().wires().size();

    EXPECT_NO_THROW(doc.addComponent("DefinitelyUnknownComponent", Pt{64.0f, 64.0f}, "", registry));

    EXPECT_EQ(doc.model().current().nodes().size(), before_nodes);
    EXPECT_EQ(doc.model().current().wires().size(), before_wires);
}

TEST(DocumentSafety, LoadHydratesRootNodeViewFromTypeRegistry) {
    namespace fs = std::filesystem;

    Document doc;
    TypeRegistry registry = load_type_registry("library/");
    doc.setTypeRegistry(&registry);

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
    EXPECT_EQ(loaded_slider->view.content_type, bp2::NodeContentType::Slider);
    EXPECT_FLOAT_EQ(loaded_slider->view.content_min, 0.0f);
    EXPECT_FLOAT_EQ(loaded_slider->view.content_max, 1.0f);

    const auto* loaded_value = require_node(doc.model().current(), doc.interner(), "value1");
    ASSERT_NE(loaded_value, nullptr);
    EXPECT_EQ(loaded_value->view.render_hint, "ref");

    fs::remove_all(dir);
}

TEST(DocumentSafety, LoadHydratesEmbeddedInlineBlueprintNodeViewFromTypeRegistry) {
    namespace fs = std::filesystem;

    Document doc;
    TypeRegistry registry = load_type_registry("library/");
    doc.setTypeRegistry(&registry);

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
    host.kind = bp2::Blueprint::Node::Kind::BlueprintInstance;
    host.semantic.id = interner.intern("host1");
    host.semantic.type = interner.intern("Group");
    host.layout.collapsed = true;
    host.source = bp2::Blueprint::Node::BlueprintSource::make_embedded(
        interner.intern("Group"),
        std::make_unique<bp2::Blueprint>(inline_bp));

    bp2::Blueprint root;
    root = root.with_id(interner.intern("root_embedded_hydrate"));
    root = root.with_name("Embedded Hydrate");
    root = root.with_node(std::move(host));

    write_file(bp_path, bp2::BlueprintCodec::encode(root, interner, arena, &registry));

    ASSERT_TRUE(doc.load(bp_path.string()));

    const auto* loaded_host = require_node(doc.model().current(), doc.interner(), "host1");
    ASSERT_NE(loaded_host, nullptr);
    ASSERT_TRUE(loaded_host->source.has_value());
    ASSERT_TRUE(loaded_host->source->is_embedded());
    ASSERT_NE(loaded_host->source->inline_def(), nullptr);

    const auto* loaded_slider = require_node(*loaded_host->source->inline_def(), doc.interner(), "inner_slider");
    ASSERT_NE(loaded_slider, nullptr);
    EXPECT_EQ(loaded_slider->view.content_type, bp2::NodeContentType::Slider);
    EXPECT_FLOAT_EQ(loaded_slider->view.content_min, 0.0f);
    EXPECT_FLOAT_EQ(loaded_slider->view.content_max, 1.0f);

    fs::remove_all(dir);
}

TEST(DocumentSafety, SetSliderValuePreservesCanonicalStaticContent) {
    Document doc;
    TypeRegistry registry = load_type_registry("library/");
    doc.setTypeRegistry(&registry);

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
    TypeRegistry registry = load_type_registry("library/");
    doc.setTypeRegistry(&registry);

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

TEST(DocumentSafety, LoadNormalizesLegacyAutosizeWithoutDirtyingOrCreatingUndoHistory) {
    namespace fs = std::filesystem;

    Document doc;
    TypeRegistry registry = load_type_registry("library/");
    doc.setTypeRegistry(&registry);

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
    EXPECT_LT(*legacy_node->layout.width, 160.0f);
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

TEST(DocumentSafety, OpenExternalRefWindowHydratesNodeViewFromTypeRegistry) {
    namespace fs = std::filesystem;

    Document doc;
    TypeRegistry registry = load_type_registry("library/");
    doc.setTypeRegistry(&registry);

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
    ref_host.kind = bp2::Blueprint::Node::Kind::BlueprintInstance;
    ref_host.semantic.id = doc.interner().intern("external_node");
    ref_host.semantic.type = doc.interner().intern("external_test");
    ref_host.source = bp2::Blueprint::Node::BlueprintSource::make_reference(
        doc.interner().intern("external_test"),
        bp2::Interface{});
    doc.model().replace_current(doc.model().current().with_node(std::move(ref_host)));

    doc.openSubWindow("external_node");

    auto* win = doc.windowManager().find(WindowScopeId::external("external_node"));
    ASSERT_NE(win, nullptr);
    ASSERT_TRUE(win->external_blueprint.has_value());

    const auto* loaded_slider = require_node(*win->external_blueprint, *win->external_interner, "external_slider");
    ASSERT_NE(loaded_slider, nullptr);
    EXPECT_EQ(loaded_slider->view.content_type, bp2::NodeContentType::Slider);

    const auto* loaded_value = require_node(*win->external_blueprint, *win->external_interner, "external_value");
    ASSERT_NE(loaded_value, nullptr);
    EXPECT_EQ(loaded_value->view.render_hint, "ref");

    fs::remove_all(dir);
}

TEST(DocumentSafety, ExplicitNormalizeNodeSizesCreatesUndoableShrinkAndClearsManualIntent) {
    Document doc;
    TypeRegistry registry = load_type_registry("library/");
    doc.setTypeRegistry(&registry);

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
    visual::mutations::rebuild(doc.scene(), doc.blueprint(), doc.interner(), doc.arena(), "", TypeRegistry{});

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

TEST(DocumentSafety, SaveLoadDropsSessionOnlyNodeColor) {
    namespace fs = std::filesystem;

    Document doc;
    TypeRegistry registry = load_type_registry("library/");
    doc.setTypeRegistry(&registry);

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

    bp2::Blueprint colored = doc.model().current();
    bp2::Blueprint::Node updated = *require_node(colored, doc.interner(), "slider1");
    updated.view.has_color = true;
    updated.view.color_r = 0.8f;
    updated.view.color_g = 0.2f;
    updated.view.color_b = 0.1f;
    updated.view.color_a = 1.0f;
    doc.model().replace_current(bp2::replace_node_preserve_order(colored, std::move(updated)));

    ASSERT_TRUE(doc.save(bp_path.string()));

    Document loaded;
    loaded.setTypeRegistry(&registry);
    ASSERT_TRUE(loaded.load(bp_path.string()));

    const auto* loaded_node = require_node(loaded.model().current(), loaded.interner(), "slider1");
    ASSERT_NE(loaded_node, nullptr);
    EXPECT_FALSE(loaded_node->view.has_color);

    fs::remove_all(dir);
}

TEST(DocumentSafety, ExtractSaveLoadRoundTripPreservesEmbeddedBlueprintStructure) {
    namespace fs = std::filesystem;

    Document doc;
    TypeRegistry registry = load_type_registry("library/");
    doc.setTypeRegistry(&registry);

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
    loaded.setTypeRegistry(&registry);
    ASSERT_TRUE(loaded.load(bp_path.string()));

    const auto* collapsed = require_node(loaded.model().current(), loaded.interner(), "extract_inst_1");
    ASSERT_NE(collapsed, nullptr);
    ASSERT_TRUE(collapsed->has_embedded_blueprint());
    ASSERT_NE(collapsed->source->inline_def(), nullptr);

    const auto& inner = *collapsed->source->inline_def();
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
    TypeRegistry registry = load_type_registry("library/");
    doc.setTypeRegistry(&registry);

    bp2::Blueprint bp;
    bp = bp.with_id(doc.interner().intern("delete_roundtrip"));
    bp = bp.with_name("DeleteRoundtrip");
    bp = bp.with_node(make_typed_node(doc.interner(), registry, "bat", "ElectricalSource", 0.0f, 0.0f));
    bp = bp.with_node(make_typed_node(doc.interner(), registry, "res", "Resistor", 20.0f, 0.0f));
    bp = bp.with_node(make_typed_node(doc.interner(), registry, "sink", "BlueprintOutput", 40.0f, 0.0f));
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
    loaded.setTypeRegistry(&registry);
    ASSERT_TRUE(loaded.load(bp_path.string()));

    EXPECT_EQ(loaded.model().current().find_node(loaded.interner().lookup("res")), nullptr);
    EXPECT_EQ(loaded.model().current().wires().size(), 0u);

    fs::remove_all(dir);
}

TEST(DocumentSafety, SaveEmitsCanonicalDocumentWithoutForbiddenFieldsAndSortedWires) {
    namespace fs = std::filesystem;

    Document doc;
    TypeRegistry registry = load_type_registry("library/");
    doc.setTypeRegistry(&registry);
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
        EXPECT_FALSE(node.contains("color"));
        EXPECT_FALSE(node.contains("render_hint"));
        EXPECT_FALSE(node.contains("resolved_iface"));
        EXPECT_FALSE(node.contains("blueprint_path"));
    }

    fs::remove_all(dir);
}

TEST(DocumentSafety, AddBlueprintToEmbeddedScopeAddsNodeInsideInlineBlueprint) {
    Document doc;
    TypeRegistry registry = load_type_registry("library/");
    doc.setTypeRegistry(&registry);

    ui::StringInterner& I = doc.interner();

    bp2::Blueprint inner;
    inner = inner.with_id(I.intern("inner_bp"));
    inner = inner.with_name("Inner");

    bp2::Blueprint::Node host;
    host.kind = bp2::Blueprint::Node::Kind::BlueprintInstance;
    host.semantic.id = I.intern("group_1");
    host.semantic.type = I.intern("Group");
    host.view.name = "group_1";
    host.source = bp2::Blueprint::Node::BlueprintSource::make_embedded(
        I.intern("Group"),
        std::make_unique<bp2::Blueprint>(inner));

    bp2::Blueprint root;
    root = root.with_id(I.intern("root_bp"));
    root = root.with_name("Root");
    root = root.with_node(std::move(host));
    doc.model().replace_current(std::move(root));

    ASSERT_NO_THROW(doc.addBlueprint("FirstOrderLag", Pt{64.0f, 64.0f}, "group_1", registry));

    const auto* root_added = doc.model().current().find_node(I.lookup("firstorderlag_1"));
    EXPECT_EQ(root_added, nullptr);

    const auto* updated_host = doc.model().current().find_node(I.lookup("group_1"));
    ASSERT_NE(updated_host, nullptr);
    ASSERT_TRUE(updated_host->source.has_value());
    ASSERT_TRUE(updated_host->source->is_embedded());
    const auto* inline_bp = updated_host->source->inline_def();
    ASSERT_NE(inline_bp, nullptr);
    EXPECT_NE(inline_bp->find_node(I.lookup("firstorderlag_1")), nullptr);
}

// ============================================================================
// Integration: newly inserted node must be selectable via Document lifecycle
// ============================================================================

TEST(DocumentSafety, NewlyAddedComponentIsImmediatelySelectableViaDocument) {
    Document doc;
    TypeRegistry registry = load_type_registry("library/");
    doc.setTypeRegistry(&registry);

    // Start with an empty blueprint
    bp2::Blueprint bp;
    bp = bp.with_id(doc.interner().intern("select_test"));
    bp = bp.with_name("Select Test");
    doc.model().replace_current(std::move(bp));
    doc.rebuildAllWindows();

    // Add a component through the full Document path
    doc.addComponent("Resistor", Pt{200.0f, 200.0f}, "", registry);

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
