#include <gtest/gtest.h>
#include "editor/window/properties_window.h"
#include "editor/commands/commands.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/interface/interface.h"
#include "blueprint_v2/interface/port_descriptor.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "ui/core/interned_id.h"
#include "core/model/component_registry.h"
#include "editor/data/node_content.h"
#include "input/editing_host.h"

// Shared bp2 test helpers (make_port, set_iface, count_inputs, count_outputs)
#include "bp2_test_helpers.h"



// Allow gtest to print InternedId values on assertion failure
namespace ui {
inline std::ostream& operator<<(std::ostream& os, InternedId id) {
    return os << "InternedId(" << id.raw() << ")";
}
}

// =============================================================================
// Helpers
// =============================================================================

/// Build a simple bp2::Blueprint::Node with given string id and float params.
static bp2::Blueprint::Node make_node(ui::StringInterner& I,
                                       const char* id,
                                       std::initializer_list<std::pair<const char*, float>> params = {}) {
    bp2::Blueprint::Node n;
    n.semantic.id   = I.intern(id);
    n.semantic.type = I.intern("Battery");
    n.view.name = id;
    for (auto& [k, v] : params)
        n.semantic.params[I.intern(k)] = v;
    return n;
}

static bp2::Blueprint::Node make_bridge_node(ui::StringInterner& I,
                                             const char* id,
                                             bool input_bridge,
                                             PortType t) {
    bp2::Blueprint::Node n;
    n.semantic.id = I.intern(id);
    n.semantic.type = I.intern("BridgePort");
    n.view.name = id;
    n.content = bp2::Blueprint::Node::BridgePortData{
        I.intern(id),
        input_bridge ? bp2::BridgeDirection::Input
                     : bp2::BridgeDirection::Output,
        t,
    };
    return n;
}

static NodeContent resolve_test_content(const bp2::Blueprint::Node& node,
                                        ComponentRegistry& registry,
                                        ui::StringInterner& interner) {
    const std::string type_name(interner.resolve(node.semantic.type));
    const auto* def = registry.get(type_name);
    const auto* pres = registry.presentation.get(type_name);
    EXPECT_NE(def, nullptr);
    return create_node_content(*def, pres, node.semantic.params, node.semantic.string_params, interner);
}

// =============================================================================
// Test fixture: EditorModel + StringInterner pre-built
// =============================================================================

class PropertiesWindowTest : public ::testing::Test {
protected:
    ui::StringInterner interner;
    bp2::EditorModel   model;
    ComponentRegistry       registry;

    void SetUp() override {
        // Register common types used in tests with content types and params
        PrimitiveSpec battery_def;
        battery_def.classname = "Battery";
        registry.types["Battery"] = battery_def;

        PrimitiveSpec knob_def;
        knob_def.classname = "KnobSwitch";
        knob_def.params["positions"] = ParamSpec{ParamSchemaType::Int, "2"};
        knob_def.params["initial_position"] = ParamSpec{ParamSchemaType::Int, "0"};
        registry.types["KnobSwitch"] = knob_def;
        registry.presentation.specs["KnobSwitch"].content_type = "Knob";

        PrimitiveSpec slider_def;
        slider_def.classname = "Slider";
        slider_def.params["min"] = ParamSpec{ParamSchemaType::Float, "0"};
        slider_def.params["max"] = ParamSpec{ParamSchemaType::Float, "100"};
        registry.types["Slider"] = slider_def;
        registry.presentation.specs["Slider"].content_type = "Slider";

        PrimitiveSpec gauge_def;
        gauge_def.classname = "Gauge";
        gauge_def.params["min"] = ParamSpec{ParamSchemaType::Float, "0"};
        gauge_def.params["max"] = ParamSpec{ParamSchemaType::Float, "30"};
        registry.types["Gauge"] = gauge_def;
        registry.presentation.specs["Gauge"].content_type = "Gauge";

        PrimitiveSpec voltmeter_def;
        voltmeter_def.classname = "Voltmeter";
        voltmeter_def.params["min"] = ParamSpec{ParamSchemaType::Float, "0"};
        voltmeter_def.params["max"] = ParamSpec{ParamSchemaType::Float, "30"};
        registry.types["Voltmeter"] = voltmeter_def;
        registry.presentation.specs["Voltmeter"].content_type = "Gauge";

        PrimitiveSpec bus_def;
        bus_def.classname = "Bus";
        registry.types["Bus"] = bus_def;

        PrimitiveSpec lut_def;
        lut_def.classname = "LookupTable";
        registry.types["LookupTable"] = lut_def;
    }
};

// =============================================================================
// Phase 2: PropertiesWindow Tests — Shadow Editing Model
// =============================================================================

TEST_F(PropertiesWindowTest, OpenSetsTarget) {
    model.add_node(make_node(interner, "bat1", {{"v", 28.0f}, {"r", 0.01f}}));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    EXPECT_FALSE(win.is_open());

    win.open(*node_ptr, "bat1", create_editor_model_host(model), interner, nullptr, [](const std::string&) {});
    EXPECT_TRUE(win.is_open());
    EXPECT_EQ(win.target_node_id_str(), "bat1");
}

TEST_F(PropertiesWindowTest, OpenInitializesPendingState) {
    model.add_node(make_node(interner, "bat1", {{"v", 28.0f}, {"r", 0.01f}}));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", create_editor_model_host(model), interner, nullptr, [](const std::string&) {});

    // Pending state should mirror the node's current values
    EXPECT_EQ(win.pending_name(), "bat1");
    EXPECT_FLOAT_EQ(win.pending_params().at("v"), 28.0f);
    EXPECT_FLOAT_EQ(win.pending_params().at("r"), 0.01f);
}

TEST_F(PropertiesWindowTest, OpenKeepsLutTableAsStringParam) {
    bp2::Blueprint::Node lut;
    lut.semantic.id = interner.intern("lut_1");
    lut.semantic.type = interner.intern("LUT");
    lut.view.name = "lut_1";
    lut.semantic.string_params["table"] = "0:0; 100:100";
    model.add_node(std::move(lut));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("lut_1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "lut_1", create_editor_model_host(model), interner, nullptr, [](const std::string&) {});

    EXPECT_EQ(win.pending_string_params().count("table"), 1u)
        << "LUT table must be edited via string_params table editor";
    EXPECT_EQ(win.pending_params().count("table"), 0u)
        << "LUT table must not be treated as generic float param";
    EXPECT_EQ(win.pending_string_params().at("table"), "0:0; 100:100");
}

TEST_F(PropertiesWindowTest, OpenInitializesPendingBridgePortType) {
    model.add_node(make_bridge_node(interner, "bp_in_1", true, PortType::V));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bp_in_1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bp_in_1", create_editor_model_host(model), interner, nullptr, [](const std::string&) {});

    ASSERT_TRUE(win.pending_bridge_port_type().has_value());
    EXPECT_EQ(*win.pending_bridge_port_type(), PortType::V);
}

TEST_F(PropertiesWindowTest, CancelDoesNotMutateLiveNode) {
    model.add_node(make_node(interner, "bat1", {{"v", 28.0f}, {"r", 0.01f}}));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", create_editor_model_host(model), interner, nullptr, [](const std::string&) {});

    // Simulate user editing pending state
    win.set_pending_param("v", 12.0f);
    win.set_pending_name("modified_name");

    // Cancel should NOT touch the live node
    win.close();

    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    auto v_it = node_ptr->semantic.params.find(interner.intern("v"));
    auto r_it = node_ptr->semantic.params.find(interner.intern("r"));
    ASSERT_NE(v_it, node_ptr->semantic.params.end());
    ASSERT_NE(r_it, node_ptr->semantic.params.end());
    EXPECT_FLOAT_EQ(v_it->second, 28.0f) << "Cancel must not mutate live node";
    EXPECT_FLOAT_EQ(r_it->second, 0.01f) << "Untouched params preserved";
    EXPECT_EQ(node_ptr->view.name, "bat1") << "Cancel must not mutate live name";
    EXPECT_FALSE(win.is_open());
}

TEST_F(PropertiesWindowTest, LiveNodeUntouchedDuringEditing) {
    model.add_node(make_node(interner, "bat1", {{"v", 28.0f}}));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", create_editor_model_host(model), interner, nullptr, [](const std::string&) {});

    // Edit pending params
    win.set_pending_param("v", 99.0f);
    win.set_pending_name("CHANGED");

    // Live node must remain untouched while editing is in progress
    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    auto v_it = node_ptr->semantic.params.find(interner.intern("v"));
    ASSERT_NE(v_it, node_ptr->semantic.params.end());
    EXPECT_FLOAT_EQ(v_it->second, 28.0f) << "Live node must not change during editing";
    EXPECT_EQ(node_ptr->view.name, "bat1") << "Live name must not change during editing";
}

TEST_F(PropertiesWindowTest, OpenTwiceDiscardsFirstSession) {
    model.add_node(make_node(interner, "bat1", {{"v", 28.0f}}));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;

    // First open
    win.open(*node_ptr, "bat1", create_editor_model_host(model), interner, nullptr, [](const std::string&) {});
    win.set_pending_param("v", 12.0f);

    // Open again — first session's pending edits are discarded
    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    win.open(*node_ptr, "bat1", create_editor_model_host(model), interner, nullptr, [](const std::string&) {});

    // Live node was never mutated
    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    auto v_it = node_ptr->semantic.params.find(interner.intern("v"));
    ASSERT_NE(v_it, node_ptr->semantic.params.end());
    EXPECT_FLOAT_EQ(v_it->second, 28.0f) << "Live node must not have been mutated by first session";

    // Pending state should be fresh from the live node
    EXPECT_FLOAT_EQ(win.pending_params().at("v"), 28.0f)
        << "Second open() must re-snapshot from live node";

    // Cancel the second open
    win.close();
    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    v_it = node_ptr->semantic.params.find(interner.intern("v"));
    ASSERT_NE(v_it, node_ptr->semantic.params.end());
    EXPECT_FLOAT_EQ(v_it->second, 28.0f);
}

TEST_F(PropertiesWindowTest, EmbeddedOwnedHostSurvivesWindowClosureAndAppliesToNestedBlueprint) {
    bp2::Blueprint inner;
    inner = inner.with_id(interner.intern("inner_bp"));
    inner = inner.with_node(make_node(interner, "bat1", {{"v", 28.0f}}));

    bp2::Blueprint::Node host;
    host.semantic.id = interner.intern("group_1");
    host.semantic.type = interner.intern("Group");
    host.view.name = "group_1";
    host.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            std::make_unique<bp2::Blueprint>(inner.with_id(interner.intern("Group"))))
    };

    bp2::Blueprint root;
    root = root.with_id(interner.intern("root_bp"));
    root = root.with_node(std::move(host));
    model.replace_current(std::move(root));

    auto embedded_host = create_pathful_embedded_host(model, {interner.intern("group_1")});
    const bp2::Blueprint::Node* node_ptr = embedded_host->find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", std::move(embedded_host), interner, nullptr, [](const std::string&) {});
    win.set_pending_param("v", 14.0f);
    win.apply();

    const auto* host_after = model.current().find_node(interner.intern("group_1"));
    ASSERT_NE(host_after, nullptr);
    ASSERT_TRUE(host_after->has_embedded_blueprint());
    const auto* inner_after = host_after->blueprint_instance().source.inline_def();
    ASSERT_NE(inner_after, nullptr);
    const auto* nested_after = inner_after->find_node(interner.intern("bat1"));
    ASSERT_NE(nested_after, nullptr);
    EXPECT_FLOAT_EQ(nested_after->semantic.params.at(interner.intern("v")), 14.0f);
}

TEST_F(PropertiesWindowTest, ClosedWindowIsNotOpen) {
    model.add_node(make_node(interner, "bat1", {}));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", create_editor_model_host(model), interner, nullptr, [](const std::string&) {});
    EXPECT_TRUE(win.is_open());

    win.close();
    EXPECT_FALSE(win.is_open());
}

// =============================================================================
// Phase 3: Apply + Undo Stack Integration Tests
// =============================================================================

TEST_F(PropertiesWindowTest, ApplyEmitsCmdSetParam) {
    model.add_node(make_node(interner, "bat1", {{"v", 28.0f}, {"r", 0.01f}}));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", create_editor_model_host(model), interner, nullptr, [](const std::string&) {});

    // Simulate user changing voltage via pending state
    win.set_pending_param("v", 14.0f);

    // Apply — should snapshot and apply changes to undo stack
    win.apply();

    EXPECT_FALSE(win.is_open());
    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    auto v_it = node_ptr->semantic.params.find(interner.intern("v"));
    auto r_it = node_ptr->semantic.params.find(interner.intern("r"));
    ASSERT_NE(v_it, node_ptr->semantic.params.end());
    ASSERT_NE(r_it, node_ptr->semantic.params.end());
    EXPECT_FLOAT_EQ(v_it->second, 14.0f) << "Applied value must persist";
    EXPECT_FLOAT_EQ(r_it->second, 0.01f) << "Untouched param preserved";
    EXPECT_TRUE(model.can_undo()) << "Undo stack must have an entry";
}

TEST_F(PropertiesWindowTest, ApplyBridgePortTypeUpdatesBothPortsAndUndoRestores) {
    model.add_node(make_bridge_node(interner, "bp_in_1", true, PortType::V));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bp_in_1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bp_in_1", create_editor_model_host(model), interner, nullptr, [](const std::string&) {});
    win.set_pending_bridge_port_type(PortType::RPM);
    win.apply();

    node_ptr = model.current().find_node(interner.intern("bp_in_1"));
    ASSERT_NE(node_ptr, nullptr);
    const auto iface = model.current().resolve_node_iface(*node_ptr, bp2::Blueprint::NodeIfaceAuthority{interner});
    ASSERT_EQ(count_inputs(iface), 1u);
    ASSERT_EQ(count_outputs(iface), 1u);
    EXPECT_EQ(get_input_type(iface, 0), PortType::RPM);
    EXPECT_EQ(get_output_type(iface, 0), PortType::RPM);

    ASSERT_TRUE(model.can_undo());
    model.undo();
    node_ptr = model.current().find_node(interner.intern("bp_in_1"));
    ASSERT_NE(node_ptr, nullptr);
    const auto undone_iface = model.current().resolve_node_iface(*node_ptr, bp2::Blueprint::NodeIfaceAuthority{interner});
    EXPECT_EQ(get_input_type(undone_iface, 0), PortType::V);
    EXPECT_EQ(get_output_type(undone_iface, 0), PortType::V);
}

TEST_F(PropertiesWindowTest, ApplyBridgePortTypePropagatesToCollapsedNodeAndNestedIface) {
    // Production-realistic scenario: bridge node lives INSIDE the embedded
    // blueprint, not at root level.  Changing its type must update both the
    // bridge node itself and the embedded blueprint's Interface so the parent's
    // view of instance ports stays consistent.

    bp2::Blueprint inner_bp;
    inner_bp = inner_bp.with_interface(bp2::Interface({
        make_port(interner, "in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    }));

    // Bridge node inside the embedded blueprint — exposed_port matches the
    // interface port name (production convention from create_bridge_nodes_for_side).
    bp2::Blueprint::Node bridge;
    bridge.semantic.id = interner.intern("bp_in_in");
    bridge.semantic.type = interner.intern("BridgePort");
    bridge.view.name = "in";
    bridge.content = bp2::Blueprint::Node::BridgePortData{
        interner.intern("in"),
        bp2::BridgeDirection::Input,
        PortType::V,
    };
    inner_bp = inner_bp.with_node(std::move(bridge));

    bp2::Blueprint::Node collapsed;
    collapsed.semantic.id = interner.intern("inst1");
    collapsed.semantic.type = interner.intern("bp_type");
    collapsed.view.name = "inst1";
    collapsed.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            std::make_unique<bp2::Blueprint>(inner_bp.with_id(interner.intern("bp_type"))))
    };

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("bp"));
    bp = bp.with_node(std::move(collapsed));
    model.replace_current(std::move(bp));

    // Open properties for the bridge node inside the embedded blueprint
    auto embedded_host = create_pathful_embedded_host(model, {interner.intern("inst1")});
    const bp2::Blueprint::Node* bridge_ptr = embedded_host->find_node(interner.intern("bp_in_in"));
    ASSERT_NE(bridge_ptr, nullptr);

    PropertiesWindow win;
    win.open(*bridge_ptr, "bp_in_in", std::move(embedded_host), interner, nullptr, [](const std::string&) {});
    win.set_pending_bridge_port_type(PortType::RPM);
    win.apply();

    // Verify the embedded blueprint's interface was updated to match
    const auto* collapsed_after = model.current().find_node(interner.intern("inst1"));
    ASSERT_NE(collapsed_after, nullptr);
    ASSERT_TRUE(collapsed_after->has_embedded_blueprint());
    ASSERT_NE(collapsed_after->blueprint_instance().source.inline_def(), nullptr);
    auto embedded_iface = collapsed_after->blueprint_instance().source.inline_def()->iface();
    auto pd = embedded_iface.find(interner.intern("in"));
    ASSERT_TRUE(pd.has_value());
    EXPECT_EQ(pd->domain, Domain::Mechanical);
    EXPECT_EQ(pd->port_type, PortType::RPM);
}

TEST_F(PropertiesWindowTest, ApplyBridgePortTypeRootLevelBridgeUpdatesRootIface) {
    // A bridge node at root level with a matching root interface port —
    // sync_iface_port_type must update the root blueprint's interface.
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("bp"));
    bp = bp.with_interface(bp2::Interface({
        make_port(interner, "in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    }));

    bp2::Blueprint::Node bridge;
    bridge.semantic.id = interner.intern("bp_in_in");
    bridge.semantic.type = interner.intern("BridgePort");
    bridge.view.name = "in";
    bridge.content = bp2::Blueprint::Node::BridgePortData{
        interner.intern("in"),
        bp2::BridgeDirection::Input,
        PortType::V,
    };
    bp = bp.with_node(std::move(bridge));
    model.replace_current(std::move(bp));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bp_in_in"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bp_in_in", create_editor_model_host(model), interner, nullptr, [](const std::string&) {});
    win.set_pending_bridge_port_type(PortType::Signal);
    win.apply();

    // Root interface must be updated
    auto root_iface = model.current().iface();
    auto pd = root_iface.find(interner.intern("in"));
    ASSERT_TRUE(pd.has_value());
    EXPECT_EQ(pd->port_type, PortType::Signal);
    EXPECT_EQ(pd->domain, Domain::Logical);
}

TEST_F(PropertiesWindowTest, ApplyThenUndoRevertsParam) {
    model.add_node(make_node(interner, "bat1", {{"v", 28.0f}, {"r", 0.01f}}));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", create_editor_model_host(model), interner, nullptr, [](const std::string&) {});

    win.set_pending_param("v", 14.0f);
    win.set_pending_param("r", 0.05f);
    win.apply();

    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_FLOAT_EQ(node_ptr->semantic.params.at(interner.intern("v")), 14.0f);
    EXPECT_FLOAT_EQ(node_ptr->semantic.params.at(interner.intern("r")), 0.05f);

    // Undo
    ASSERT_TRUE(model.can_undo());
    model.undo();

    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_FLOAT_EQ(node_ptr->semantic.params.at(interner.intern("v")), 28.0f) << "Undo must revert v";
    EXPECT_FLOAT_EQ(node_ptr->semantic.params.at(interner.intern("r")), 0.01f) << "Undo must revert r";

    // Redo
    model.redo();
    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_FLOAT_EQ(node_ptr->semantic.params.at(interner.intern("v")), 14.0f) << "Redo must restore v";
    EXPECT_FLOAT_EQ(node_ptr->semantic.params.at(interner.intern("r")), 0.05f) << "Redo must restore r";
}

TEST_F(PropertiesWindowTest, ApplyNoChangesDoesNotPushUndo) {
    model.add_node(make_node(interner, "bat1", {{"v", 28.0f}}));
    model.clear_history(); // setup only — not part of undo test

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", create_editor_model_host(model), interner, nullptr, [](const std::string&) {});

    // No edits — just apply
    win.apply();

    EXPECT_FALSE(model.can_undo())
        << "Applying with no changes should not push to undo stack";
}

TEST_F(PropertiesWindowTest, ApplyInvokesCallback) {
    model.add_node(make_node(interner, "bat1", {{"v", 28.0f}}));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    bool callback_invoked = false;
    std::string callback_node_id;

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", create_editor_model_host(model), interner, nullptr, [&](const std::string& nid) {
            callback_invoked = true;
            callback_node_id = nid;
        });

    win.set_pending_param("v", 14.0f);
    win.apply();

    EXPECT_TRUE(callback_invoked) << "Apply must invoke on_apply callback";
    EXPECT_EQ(callback_node_id, "bat1");
}

TEST_F(PropertiesWindowTest, NameChangePushesUndo) {
    auto n = make_node(interner, "bat1", {{"v", 28.0f}});
    n.view.name = "OriginalName";
    model.add_node(std::move(n));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", create_editor_model_host(model), interner, nullptr, [](const std::string&) {});

    win.set_pending_name("NewName");
    win.apply();

    EXPECT_TRUE(model.can_undo()) << "Name change should push to undo stack";
    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_EQ(node_ptr->view.name, "NewName");
}

TEST_F(PropertiesWindowTest, NameChangeUndoRestoresOldName) {
    auto n = make_node(interner, "bat1", {{"v", 28.0f}});
    n.view.name = "OriginalName";
    model.add_node(std::move(n));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", create_editor_model_host(model), interner, nullptr, [](const std::string&) {});

    win.set_pending_name("NewName");
    win.apply();

    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_EQ(node_ptr->view.name, "NewName");

    // Undo
    model.undo();

    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_EQ(node_ptr->view.name, "OriginalName") << "Undo should restore original name";
}

TEST_F(PropertiesWindowTest, NameChangePreservesNodeAndWireOrder) {
    bp2::Blueprint bp;

     bp2::Blueprint::Node src;
     src.semantic.id = interner.intern("src");
     src.semantic.type = interner.intern("Battery");
     src.view.name = "src";
     set_iface(src, {
         make_port(interner, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
     });

     bp2::Blueprint::Node bus;
     bus.semantic.id = interner.intern("bus");
     bus.semantic.type = interner.intern("Bus");
     bus.view.name = "bus";
     set_iface(bus, {
         make_port(interner, "v", Domain::Electrical, bp2::Direction::InOut, PortType::V),
         make_port(interner, "v", Domain::Electrical, bp2::Direction::InOut, PortType::V),
     });

     bp2::Blueprint::Node load;
     load.semantic.id = interner.intern("load");
     load.semantic.type = interner.intern("Lamp");
     load.view.name = "load";
     set_iface(load, {
         make_port(interner, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
     });

    bp = bp.with_node(src);
    bp = bp.with_node(bus);
    bp = bp.with_node(load);

    bp2::Blueprint::Wire w0;
    w0.id = interner.intern("wire_0");
    w0.source = bp2::WireEndpoint{interner.intern("src"), interner.intern("v_out")};
    w0.target = bp2::WireEndpoint{interner.intern("bus"), interner.intern("v")};
    bp = bp.with_wire(w0);

    bp2::Blueprint::Wire w1;
    w1.id = interner.intern("wire_1");
    w1.source = bp2::WireEndpoint{interner.intern("bus"), interner.intern("v")};
    w1.target = bp2::WireEndpoint{interner.intern("load"), interner.intern("v_in")};
    bp = bp.with_wire(w1);

    model.replace_current(std::move(bp));

    std::vector<ui::InternedId> node_order_before;
    for (const auto& n : model.current().nodes()) node_order_before.push_back(n.semantic.id);

    std::vector<ui::InternedId> wire_order_before;
    for (const auto& w : model.current().wires()) wire_order_before.push_back(w.id);

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bus"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bus", create_editor_model_host(model), interner, nullptr, [](const std::string&) {});
    win.set_pending_name("bus_renamed");
    win.apply();

    std::vector<ui::InternedId> node_order_after;
    for (const auto& n : model.current().nodes()) node_order_after.push_back(n.semantic.id);

    std::vector<ui::InternedId> wire_order_after;
    for (const auto& w : model.current().wires()) wire_order_after.push_back(w.id);

    EXPECT_EQ(node_order_after, node_order_before);
    EXPECT_EQ(wire_order_after, wire_order_before);
}

TEST_F(PropertiesWindowTest, ParamAndNameChangeSingleUndo) {
    auto n = make_node(interner, "bat1", {{"v", 28.0f}});
    n.view.name = "OriginalName";
    model.add_node(std::move(n));
    model.clear_history(); // setup only — not part of undo test

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", create_editor_model_host(model), interner, nullptr, [](const std::string&) {});

    // Change both param and name in one "Apply"
    win.set_pending_param("v", 14.0f);
    win.set_pending_name("NewName");
    win.apply();

    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_FLOAT_EQ(node_ptr->semantic.params.at(interner.intern("v")), 14.0f);
    EXPECT_EQ(node_ptr->view.name, "NewName");

    // A single Ctrl+Z should revert BOTH changes
    ASSERT_TRUE(model.can_undo());
    model.undo();

    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_FLOAT_EQ(node_ptr->semantic.params.at(interner.intern("v")), 28.0f) << "Single undo must revert param";
    EXPECT_EQ(node_ptr->view.name, "OriginalName") << "Single undo must revert name";
    EXPECT_FALSE(model.can_undo()) << "Only one undo entry should have been pushed";
}

// =============================================================================
// Phase 4: Pointer Safety — Node disappears while window is open
// =============================================================================

TEST_F(PropertiesWindowTest, CloseGracefullyWhenNodeRemoved) {
    model.add_node(make_node(interner, "bat1", {{"v", 28.0f}}));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", create_editor_model_host(model), interner, nullptr, [](const std::string&) {});
    EXPECT_TRUE(win.is_open());

    // Simulate the node being removed (e.g. by undo of a CmdAddNode)
    model.remove_node(interner.intern("bat1"));
    EXPECT_EQ(model.current().find_node(interner.intern("bat1")), nullptr) << "Node must be gone";

    // render() should detect the missing node and close silently
    win.render();
    EXPECT_FALSE(win.is_open())
        << "Window must auto-close when target node is removed";
}

TEST_F(PropertiesWindowTest, ApplyGracefullyWhenNodeRemoved) {
    model.add_node(make_node(interner, "bat1", {{"v", 28.0f}}));
    model.clear_history(); // setup only — not part of undo test

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", create_editor_model_host(model), interner, nullptr, [](const std::string&) {});

    // Remove node before apply (simulate node deleted externally)
    model.remove_node(interner.intern("bat1"));
    model.clear_history(); // isolate: test only cares that apply() adds nothing

    // apply() should detect the missing node and close without crashing
    win.apply();
    EXPECT_FALSE(win.is_open());
    EXPECT_FALSE(model.can_undo())
        << "No undo entry should be pushed when target node is gone";
}

TEST_F(PropertiesWindowTest, RenderClearsSourceIdWhenNodeRemoved) {
    model.add_node(make_node(interner, "bat1", {{"v", 28.0f}}));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", create_editor_model_host(model), interner, nullptr, [](const std::string&) {});
    win.set_owner_document_id(editor::DocumentId::from_string("doc-1"));

    model.remove_node(interner.intern("bat1"));

    win.render();

    EXPECT_FALSE(win.is_open());
    EXPECT_FALSE(win.owner_document_id().has_value())
        << "Auto-closing a dead properties session must clear its owner tag";
}

TEST_F(PropertiesWindowTest, ApplyClearsSourceIdWhenNodeRemoved) {
    model.add_node(make_node(interner, "bat1", {{"v", 28.0f}}));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", create_editor_model_host(model), interner, nullptr, [](const std::string&) {});
    win.set_owner_document_id(editor::DocumentId::from_string("doc-1"));

    model.remove_node(interner.intern("bat1"));

    win.apply();

    EXPECT_FALSE(win.is_open());
    EXPECT_FALSE(win.owner_document_id().has_value())
        << "Applying a dead properties session must clear its owner tag";
}

TEST_F(PropertiesWindowTest, CancelGracefullyWhenNodeRemoved) {
    model.add_node(make_node(interner, "bat1", {{"v", 28.0f}}));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", create_editor_model_host(model), interner, nullptr, [](const std::string&) {});

    // Remove node before cancel
    model.remove_node(interner.intern("bat1"));

    // close() (cancel) should not crash even though node is gone
    win.close();
    EXPECT_FALSE(win.is_open());
}

// =============================================================================
// Phase 5: Port Layout Override Tests
// =============================================================================

TEST_F(PropertiesWindowTest, PortLayoutOverride_ApplyChanges) {
    bp2::Blueprint::Node n;
    n.semantic.id = interner.intern("azs1");
    n.semantic.type = interner.intern("AZS");
    n.view.name = "AZS";
    set_iface(n, {
        make_port(interner, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(interner, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
        make_port(interner, "state", Domain::Logical, bp2::Direction::Output, PortType::Bool),
    });
    model.add_node(std::move(n));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("azs1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "azs1", create_editor_model_host(model), interner, nullptr, [](const std::string&) {});

    // Set port layout overrides
    std::vector<bp2::Blueprint::Node::PortLayoutOverride> overrides;
    overrides.push_back({"v_in",  std::string("top"),   std::nullopt});
    overrides.push_back({"v_out", std::string("right"), 0});
    win.set_pending_layout_overrides(overrides);

    win.apply();

    // Verify the node's layout_overrides were updated
    node_ptr = model.current().find_node(interner.intern("azs1"));
    ASSERT_NE(node_ptr, nullptr);
    ASSERT_EQ(node_ptr->layout.layout_overrides.size(), 2u);
    EXPECT_EQ(node_ptr->layout.layout_overrides[0].port_name, "v_in");
    EXPECT_EQ(node_ptr->layout.layout_overrides[0].side, std::string("top"));
    EXPECT_EQ(node_ptr->layout.layout_overrides[1].port_name, "v_out");
    EXPECT_EQ(node_ptr->layout.layout_overrides[1].side, std::string("right"));
    EXPECT_EQ(node_ptr->layout.layout_overrides[1].position, 0);
}

TEST_F(PropertiesWindowTest, PortLayoutOverride_UndoRestoresOriginal) {
    bp2::Blueprint::Node n;
    n.semantic.id = interner.intern("azs1");
    n.semantic.type = interner.intern("AZS");
    n.view.name = "AZS";
    set_iface(n, {
        make_port(interner, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(interner, "v_out", Domain::Electrical, bp2::Direction::Output, PortType::V),
    });
    model.add_node(std::move(n));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("azs1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_TRUE(node_ptr->layout.layout_overrides.empty()) << "Initial layout_overrides should be empty";

    PropertiesWindow win;
    win.open(*node_ptr, "azs1", create_editor_model_host(model), interner, nullptr, [](const std::string&) {});

    // Add port layout override
    std::vector<bp2::Blueprint::Node::PortLayoutOverride> overrides;
    overrides.push_back({"v_in", std::string("bottom"), std::nullopt});
    win.set_pending_layout_overrides(overrides);

    win.apply();

    node_ptr = model.current().find_node(interner.intern("azs1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_EQ(node_ptr->layout.layout_overrides.size(), 1u);

    // Undo should restore empty layout_overrides
    ASSERT_TRUE(model.can_undo());
    model.undo();

    node_ptr = model.current().find_node(interner.intern("azs1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_TRUE(node_ptr->layout.layout_overrides.empty()) << "Undo should restore empty layout_overrides";
}

TEST_F(PropertiesWindowTest, PortLayoutOverride_NoChangesDoesNotPushUndo) {
    bp2::Blueprint::Node n;
    n.semantic.id = interner.intern("azs1");
    n.semantic.type = interner.intern("AZS");
    n.view.name = "AZS";
    set_iface(n, {
        make_port(interner, "v_in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    });
    model.add_node(std::move(n));
    model.clear_history(); // setup only — not part of undo test

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("azs1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "azs1", create_editor_model_host(model), interner, nullptr, [](const std::string&) {});

    // No changes to layout overrides (still empty)
    win.apply();

    EXPECT_FALSE(model.can_undo()) << "No changes should not push to undo stack";
}

// =============================================================================
// Phase 6: Bridge Node Duplicate Control Regression (Bug 2)
// =============================================================================

TEST_F(PropertiesWindowTest, BridgeNodeUsesDedicatedPortTypeState) {
    auto n = make_bridge_node(interner, "inst:my_input", true, PortType::V);
    model.add_node(std::move(n));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("inst:my_input"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "inst:my_input", create_editor_model_host(model), interner, nullptr, [](const std::string&) {});

    EXPECT_TRUE(win.pending_string_params().empty());
    EXPECT_TRUE(win.pending_bridge_port_type().has_value());
    EXPECT_EQ(*win.pending_bridge_port_type(), PortType::V);
}

// Regression: changing bridge port type via the dropdown must NOT produce a
// second (duplicate) undo entry from the string params path.  Verify that
// apply() with only a bridge port type change (no string param edits) still
// round-trips cleanly.
TEST_F(PropertiesWindowTest, BridgeNode_PortTypeChangeAppliesCleanly) {
    auto n = make_bridge_node(interner, "inst:my_output", false, PortType::V);
    model.add_node(std::move(n));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("inst:my_output"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "inst:my_output", create_editor_model_host(model), interner, nullptr, [](const std::string&) {});

    // Change port type from V to Bool via the dropdown mechanism
    win.set_pending_bridge_port_type(PortType::Bool);
     win.apply();

     // Verify the port type was updated on the node
     node_ptr = model.current().find_node(interner.intern("inst:my_output"));
     ASSERT_NE(node_ptr, nullptr);
     const auto iface = model.current().resolve_node_iface(*node_ptr, bp2::Blueprint::NodeIfaceAuthority{interner});
     ASSERT_GT(count_inputs(iface), 0u);
     EXPECT_EQ(get_input_type(iface, 0), PortType::Bool);
     EXPECT_TRUE(node_ptr->semantic.string_params.empty());
  }

// =============================================================================
// Bug 1 regression: Apply syncs content_max/min from params
// =============================================================================

// When the user changes the "positions" param for a Knob-type node via the
// inspector, apply() must update content_max so the semantic knob content renders
// the correct number of tick marks.
TEST_F(PropertiesWindowTest, ApplyKnobPositionsSyncsContentMax) {
    bp2::Blueprint::Node n;
    n.semantic.id = interner.intern("knob1");
    n.semantic.type = interner.intern("KnobSwitch");
    n.view.name = "knob1";
    n.semantic.params[interner.intern("positions")] = 2.0f;
    model.add_node(std::move(n));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("knob1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_FLOAT_EQ(resolve_test_content(*node_ptr, registry, interner).max, 2.0f);

    PropertiesWindow win;
    win.open(*node_ptr, "knob1", create_editor_model_host(model), interner, &registry, [](const std::string&) {});

    // User changes positions from 2 to 5
    win.set_pending_param("positions", 5.0f);
    win.apply();

    node_ptr = model.current().find_node(interner.intern("knob1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_FLOAT_EQ(node_ptr->semantic.params.at(interner.intern("positions")), 5.0f);
    EXPECT_FLOAT_EQ(resolve_test_content(*node_ptr, registry, interner).max, 5.0f)
        << "content_max must be synced from 'positions' param for Knob nodes";
}

// Verify content_max sync for Knob via undo: after undo, content_max must
// revert to the original value.
TEST_F(PropertiesWindowTest, ApplyKnobPositionsSyncsContentMax_UndoReverts) {
    bp2::Blueprint::Node n;
    n.semantic.id = interner.intern("knob1");
    n.semantic.type = interner.intern("KnobSwitch");
    n.view.name = "knob1";
    n.semantic.params[interner.intern("positions")] = 2.0f;
    model.add_node(std::move(n));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("knob1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "knob1", create_editor_model_host(model), interner, &registry, [](const std::string&) {});
    win.set_pending_param("positions", 5.0f);
    win.apply();

    ASSERT_TRUE(model.can_undo());
    model.undo();

    node_ptr = model.current().find_node(interner.intern("knob1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_FLOAT_EQ(resolve_test_content(*node_ptr, registry, interner).max, 2.0f)
        << "Undo must revert content_max for Knob nodes";
}

// When the user changes "min" or "max" param for a Slider-type node,
// apply() must sync content_min / content_max.
TEST_F(PropertiesWindowTest, ApplySliderMinMaxSyncsContentRange) {
    bp2::Blueprint::Node n;
    n.semantic.id = interner.intern("slider1");
    n.semantic.type = interner.intern("Slider");
    n.view.name = "slider1";
    n.semantic.params[interner.intern("min")] = 0.0f;
    n.semantic.params[interner.intern("max")] = 100.0f;
    model.add_node(std::move(n));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("slider1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "slider1", create_editor_model_host(model), interner, &registry, [](const std::string&) {});
    win.set_pending_param("min", -10.0f);
    win.set_pending_param("max", 200.0f);
    win.apply();

    node_ptr = model.current().find_node(interner.intern("slider1"));
    ASSERT_NE(node_ptr, nullptr);
    const NodeContent content = resolve_test_content(*node_ptr, registry, interner);
    EXPECT_FLOAT_EQ(content.min, -10.0f)
        << "content_min must be synced from 'min' param for Slider nodes";
    EXPECT_FLOAT_EQ(content.max, 200.0f)
        << "content_max must be synced from 'max' param for Slider nodes";
}

// When the user changes "min" or "max" param for a Gauge-type node,
// apply() must sync content_min / content_max.
TEST_F(PropertiesWindowTest, ApplyGaugeMinMaxSyncsContentRange) {
    bp2::Blueprint::Node n;
    n.semantic.id = interner.intern("gauge1");
    n.semantic.type = interner.intern("Voltmeter");
    n.view.name = "gauge1";
    n.semantic.params[interner.intern("min")] = 0.0f;
    n.semantic.params[interner.intern("max")] = 30.0f;
    model.add_node(std::move(n));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("gauge1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "gauge1", create_editor_model_host(model), interner, &registry, [](const std::string&) {});
    win.set_pending_param("max", 60.0f);
    win.apply();

    node_ptr = model.current().find_node(interner.intern("gauge1"));
    ASSERT_NE(node_ptr, nullptr);
    const NodeContent content = resolve_test_content(*node_ptr, registry, interner);
    EXPECT_FLOAT_EQ(content.min, 0.0f)
        << "content_min unchanged for Gauge nodes";
    EXPECT_FLOAT_EQ(content.max, 60.0f)
        << "content_max must be synced from 'max' param for Gauge nodes";
}

TEST_F(PropertiesWindowTest, ApplyKnobPositionsUpdatesCanonicalRangeOnly) {
    bp2::Blueprint::Node n;
    n.semantic.id = interner.intern("knob1");
    n.semantic.type = interner.intern("KnobSwitch");
    n.view.name = "knob1";
    n.semantic.params[interner.intern("positions")] = 5.0f;
    model.add_node(std::move(n));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("knob1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "knob1", create_editor_model_host(model), interner, &registry, [](const std::string&) {});
    win.set_pending_param("positions", 7.0f);
    win.apply();

    node_ptr = model.current().find_node(interner.intern("knob1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_FLOAT_EQ(resolve_test_content(*node_ptr, registry, interner).max, 7.0f)
        << "apply() must refresh knob range from canonical params";
}

TEST_F(PropertiesWindowTest, ApplySwitchClosedUpdatesCanonicalDefaultOnly) {
    bp2::Blueprint::Node n;
    n.semantic.id = interner.intern("switch1");
    n.semantic.type = interner.intern("Switch");
    n.view.name = "switch1";
    n.semantic.params[interner.intern("closed")] = 0.0f;
    model.add_node(std::move(n));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("switch1"));
    ASSERT_NE(node_ptr, nullptr);

    PrimitiveSpec switch_def;
    switch_def.classname = "Switch";
    switch_def.params["closed"] = ParamSpec{ParamSchemaType::Bool, "false"};
    registry.types["Switch"] = switch_def;
    registry.presentation.specs["Switch"].content_type = "Switch";

    PropertiesWindow win;
    win.open(*node_ptr, "switch1", create_editor_model_host(model), interner, &registry, [](const std::string&) {});
    win.set_pending_param("closed", 1.0f);
    win.apply();

    node_ptr = model.current().find_node(interner.intern("switch1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_EQ(node_ptr->semantic.string_params.at("closed"), "true");
    EXPECT_TRUE(resolve_test_content(*node_ptr, registry, interner).state)
        << "apply() must update canonical switch default; runtime state is external";
}

TEST_F(PropertiesWindowTest, ApplyAzsClosedUpdatesCanonicalVerticalToggleDefaultOnly) {
    PrimitiveSpec azs_def;
    azs_def.classname = "AZS";
    azs_def.params["closed"] = ParamSpec{ParamSchemaType::Bool, "false"};
    registry.types["AZS"] = azs_def;
    registry.presentation.specs["AZS"].content_type = "VerticalToggle";

    bp2::Blueprint::Node n;
    n.semantic.id = interner.intern("azs1");
    n.semantic.type = interner.intern("AZS");
    n.view.name = "azs1";
    n.semantic.params[interner.intern("closed")] = 0.0f;
    model.add_node(std::move(n));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("azs1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "azs1", create_editor_model_host(model), interner, &registry, [](const std::string&) {});
    win.set_pending_param("closed", 1.0f);
    win.apply();

    node_ptr = model.current().find_node(interner.intern("azs1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_EQ(node_ptr->semantic.string_params.at("closed"), "true");
    EXPECT_TRUE(resolve_test_content(*node_ptr, registry, interner).state);
}

TEST_F(PropertiesWindowTest, ApplyRelayClosedUpdatesCanonicalSwitchDefaultOnly) {
    PrimitiveSpec relay_def;
    relay_def.classname = "Relay";
    relay_def.params["closed"] = ParamSpec{ParamSchemaType::Bool, "false"};
    registry.types["Relay"] = relay_def;
    registry.presentation.specs["Relay"].content_type = "Switch";

    bp2::Blueprint::Node n;
    n.semantic.id = interner.intern("relay1");
    n.semantic.type = interner.intern("Relay");
    n.view.name = "relay1";
    n.semantic.params[interner.intern("closed")] = 0.0f;
    model.add_node(std::move(n));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("relay1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "relay1", create_editor_model_host(model), interner, &registry, [](const std::string&) {});
    win.set_pending_param("closed", 1.0f);
    win.apply();

    node_ptr = model.current().find_node(interner.intern("relay1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_EQ(node_ptr->semantic.string_params.at("closed"), "true");
    EXPECT_TRUE(resolve_test_content(*node_ptr, registry, interner).state);
}
