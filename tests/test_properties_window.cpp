#include <gtest/gtest.h>
#include "editor/window/properties_window.h"
#include "editor/commands/commands.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/interface/interface.h"
#include "blueprint_v2/interface/port_descriptor.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "ui/core/interned_id.h"
#include "json_parser/json_parser.h"

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
        input_bridge ? bp2::Blueprint::Node::BridgePortSide::Input
                     : bp2::Blueprint::Node::BridgePortSide::Output,
        t,
        input_bridge
            ? bp2::Interface({
                make_port(I, "ext", Domain::Electrical, bp2::Direction::Input, t),
                make_port(I, "port", Domain::Electrical, bp2::Direction::Output, t),
            })
            : bp2::Interface({
                make_port(I, "port", Domain::Electrical, bp2::Direction::Input, t),
                make_port(I, "ext", Domain::Electrical, bp2::Direction::Output, t),
            })
    };
    return n;
}

// =============================================================================
// Test fixture: EditorModel + StringInterner pre-built
// =============================================================================

class PropertiesWindowTest : public ::testing::Test {
protected:
    ui::StringInterner interner;
    bp2::EditorModel   model;
    TypeRegistry       registry;

    void SetUp() override {
        // Register common types used in tests with content types and params
        TypeDefinition battery_def;
        battery_def.classname = "Battery";
        battery_def.cpp_class = true;
        registry.types["Battery"] = battery_def;

        TypeDefinition knob_def;
        knob_def.classname = "KnobSwitch";
        knob_def.params["positions"] = ParamSpec{ParamSchemaType::Int, "2"};
        knob_def.params["initial_position"] = ParamSpec{ParamSchemaType::Int, "0"};
        registry.types["KnobSwitch"] = knob_def;
        registry.presentation.specs["KnobSwitch"].content_type = "Knob";

        TypeDefinition slider_def;
        slider_def.classname = "Slider";
        slider_def.params["min"] = ParamSpec{ParamSchemaType::Float, "0"};
        slider_def.params["max"] = ParamSpec{ParamSchemaType::Float, "100"};
        registry.types["Slider"] = slider_def;
        registry.presentation.specs["Slider"].content_type = "Slider";

        TypeDefinition gauge_def;
        gauge_def.classname = "Gauge";
        gauge_def.params["min"] = ParamSpec{ParamSchemaType::Float, "0"};
        gauge_def.params["max"] = ParamSpec{ParamSchemaType::Float, "30"};
        registry.types["Gauge"] = gauge_def;
        registry.presentation.specs["Gauge"].content_type = "Gauge";

        TypeDefinition voltmeter_def;
        voltmeter_def.classname = "Voltmeter";
        voltmeter_def.params["min"] = ParamSpec{ParamSchemaType::Float, "0"};
        voltmeter_def.params["max"] = ParamSpec{ParamSchemaType::Float, "30"};
        registry.types["Voltmeter"] = voltmeter_def;
        registry.presentation.specs["Voltmeter"].content_type = "Gauge";

        TypeDefinition bus_def;
        bus_def.classname = "Bus";
        bus_def.cpp_class = true;
        registry.types["Bus"] = bus_def;

        TypeDefinition lut_def;
        lut_def.classname = "LookupTable";
        lut_def.cpp_class = true;
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

    win.open(*node_ptr, "bat1", model, interner, nullptr, [](const std::string&) {});
    EXPECT_TRUE(win.is_open());
    EXPECT_EQ(win.target_node_id_str(), "bat1");
}

TEST_F(PropertiesWindowTest, OpenInitializesPendingState) {
    model.add_node(make_node(interner, "bat1", {{"v", 28.0f}, {"r", 0.01f}}));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", model, interner, nullptr, [](const std::string&) {});

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
    win.open(*node_ptr, "lut_1", model, interner, nullptr, [](const std::string&) {});

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
    win.open(*node_ptr, "bp_in_1", model, interner, nullptr, [](const std::string&) {});

    ASSERT_TRUE(win.pending_bridge_port_type().has_value());
    EXPECT_EQ(*win.pending_bridge_port_type(), PortType::V);
}

TEST_F(PropertiesWindowTest, CancelDoesNotMutateLiveNode) {
    model.add_node(make_node(interner, "bat1", {{"v", 28.0f}, {"r", 0.01f}}));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", model, interner, nullptr, [](const std::string&) {});

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
    win.open(*node_ptr, "bat1", model, interner, nullptr, [](const std::string&) {});

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
    win.open(*node_ptr, "bat1", model, interner, nullptr, [](const std::string&) {});
    win.set_pending_param("v", 12.0f);

    // Open again — first session's pending edits are discarded
    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    win.open(*node_ptr, "bat1", model, interner, nullptr, [](const std::string&) {});

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

TEST_F(PropertiesWindowTest, ClosedWindowIsNotOpen) {
    model.add_node(make_node(interner, "bat1", {}));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", model, interner, nullptr, [](const std::string&) {});
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
    win.open(*node_ptr, "bat1", model, interner, nullptr, [](const std::string&) {});

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
    win.open(*node_ptr, "bp_in_1", model, interner, nullptr, [](const std::string&) {});
    win.set_pending_bridge_port_type(PortType::RPM);
    win.apply();

    node_ptr = model.current().find_node(interner.intern("bp_in_1"));
    ASSERT_NE(node_ptr, nullptr);
    const auto iface = model.current().effective_node_iface(*node_ptr);
    ASSERT_EQ(count_inputs(iface), 1u);
    ASSERT_EQ(count_outputs(iface), 1u);
    EXPECT_EQ(get_input_type(iface, 0), PortType::RPM);
    EXPECT_EQ(get_output_type(iface, 0), PortType::RPM);

    ASSERT_TRUE(model.can_undo());
    model.undo();
    node_ptr = model.current().find_node(interner.intern("bp_in_1"));
    ASSERT_NE(node_ptr, nullptr);
    const auto undone_iface = model.current().effective_node_iface(*node_ptr);
    EXPECT_EQ(get_input_type(undone_iface, 0), PortType::V);
    EXPECT_EQ(get_output_type(undone_iface, 0), PortType::V);
}

TEST_F(PropertiesWindowTest, ApplyBridgePortTypePropagatesToCollapsedNodeAndNestedIface) {
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("bp"));

    // Create a blueprint-instance node with embedded source
    // Issue #91: Blueprint-instance interface derives from source authority only.
    // Initialize inner_bp with the interface that will be authoritative.
    bp2::Blueprint inner_bp;
    inner_bp = inner_bp.with_interface(bp2::Interface({
        make_port(interner, "in", Domain::Electrical, bp2::Direction::Input, PortType::V),
    }));

    bp2::Blueprint::Node collapsed;
    collapsed.semantic.id = interner.intern("inst1");
    collapsed.semantic.type = interner.intern("bp_type");
    collapsed.view.name = "inst1";
    // Issue #91: Do NOT set component().iface on blueprint-instance nodes.
    // The interface derives from source authority only.
    
    collapsed.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
        std::make_unique<bp2::Blueprint>(inner_bp.with_id(interner.intern("bp_type")))
    )
    };

    // Create bridge node (now as a regular component node, not nested)
    bp2::Blueprint::Node bridge = make_bridge_node(interner, "inst1:in", true, PortType::V);

    bp = bp.with_node(std::move(bridge));
    bp = bp.with_node(std::move(collapsed));
    model.replace_current(std::move(bp));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("inst1:in"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "inst1:in", model, interner, nullptr, [](const std::string&) {});
    win.set_pending_bridge_port_type(PortType::RPM);
    win.apply();

    const auto* collapsed_after = model.current().find_node(interner.intern("inst1"));
    ASSERT_NE(collapsed_after, nullptr);
    
    // Issue #91: Query interface from source authority using effective_node_iface()
    // since component().iface is no longer mirrored for blueprint-instance nodes.
    auto effective_iface = model.current().effective_node_iface(*collapsed_after);
    ASSERT_EQ(count_inputs(effective_iface), 1u);
    EXPECT_EQ(get_input_type(effective_iface, 0), PortType::RPM);

    // Check that the embedded blueprint's interface is also updated
    ASSERT_TRUE(collapsed_after->has_embedded_blueprint());
    ASSERT_NE(collapsed_after->blueprint_instance().source.inline_def(), nullptr);
    auto embedded_iface = collapsed_after->blueprint_instance().source.inline_def()->iface();
    auto pd = embedded_iface.find(interner.intern("in"));
    ASSERT_TRUE(pd.has_value());
    EXPECT_EQ(pd->domain, Domain::Mechanical);
}

TEST_F(PropertiesWindowTest, ApplyThenUndoRevertsParam) {
    model.add_node(make_node(interner, "bat1", {{"v", 28.0f}, {"r", 0.01f}}));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", model, interner, nullptr, [](const std::string&) {});

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
    win.open(*node_ptr, "bat1", model, interner, nullptr, [](const std::string&) {});

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
    win.open(*node_ptr, "bat1", model, interner, nullptr, [&](const std::string& nid) {
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
    win.open(*node_ptr, "bat1", model, interner, nullptr, [](const std::string&) {});

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
    win.open(*node_ptr, "bat1", model, interner, nullptr, [](const std::string&) {});

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
     bus.view.render_hint = "bus";
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
    win.open(*node_ptr, "bus", model, interner, nullptr, [](const std::string&) {});
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
    win.open(*node_ptr, "bat1", model, interner, nullptr, [](const std::string&) {});

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
    win.open(*node_ptr, "bat1", model, interner, nullptr, [](const std::string&) {});
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
    win.open(*node_ptr, "bat1", model, interner, nullptr, [](const std::string&) {});

    // Remove node before apply (simulate node deleted externally)
    model.remove_node(interner.intern("bat1"));
    model.clear_history(); // isolate: test only cares that apply() adds nothing

    // apply() should detect the missing node and close without crashing
    win.apply();
    EXPECT_FALSE(win.is_open());
    EXPECT_FALSE(model.can_undo())
        << "No undo entry should be pushed when target node is gone";
}

TEST_F(PropertiesWindowTest, CancelGracefullyWhenNodeRemoved) {
    model.add_node(make_node(interner, "bat1", {{"v", 28.0f}}));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", model, interner, nullptr, [](const std::string&) {});

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
    win.open(*node_ptr, "azs1", model, interner, nullptr, [](const std::string&) {});

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
    win.open(*node_ptr, "azs1", model, interner, nullptr, [](const std::string&) {});

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
    win.open(*node_ptr, "azs1", model, interner, nullptr, [](const std::string&) {});

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
    win.open(*node_ptr, "inst:my_input", model, interner, nullptr, [](const std::string&) {});

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
    win.open(*node_ptr, "inst:my_output", model, interner, nullptr, [](const std::string&) {});

    // Change port type from V to Bool via the dropdown mechanism
    win.set_pending_bridge_port_type(PortType::Bool);
     win.apply();

     // Verify the port type was updated on the node
     node_ptr = model.current().find_node(interner.intern("inst:my_output"));
     ASSERT_NE(node_ptr, nullptr);
     const auto iface = model.current().effective_node_iface(*node_ptr);
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
    n.view.content_type = bp2::NodeContentType::Knob;
    n.view.content_max = 2.0f;  // Initial: 2 positions
    n.semantic.params[interner.intern("positions")] = 2.0f;
    model.add_node(std::move(n));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("knob1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_FLOAT_EQ(node_ptr->view.content_max, 2.0f);

    PropertiesWindow win;
    win.open(*node_ptr, "knob1", model, interner, &registry, [](const std::string&) {});

    // User changes positions from 2 to 5
    win.set_pending_param("positions", 5.0f);
    win.apply();

    node_ptr = model.current().find_node(interner.intern("knob1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_FLOAT_EQ(node_ptr->semantic.params.at(interner.intern("positions")), 5.0f);
    EXPECT_FLOAT_EQ(node_ptr->view.content_max, 5.0f)
        << "content_max must be synced from 'positions' param for Knob nodes";
}

// Verify content_max sync for Knob via undo: after undo, content_max must
// revert to the original value.
TEST_F(PropertiesWindowTest, ApplyKnobPositionsSyncsContentMax_UndoReverts) {
    bp2::Blueprint::Node n;
    n.semantic.id = interner.intern("knob1");
    n.semantic.type = interner.intern("KnobSwitch");
    n.view.name = "knob1";
    n.view.content_type = bp2::NodeContentType::Knob;
    n.view.content_max = 2.0f;
    n.semantic.params[interner.intern("positions")] = 2.0f;
    model.add_node(std::move(n));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("knob1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "knob1", model, interner, &registry, [](const std::string&) {});
    win.set_pending_param("positions", 5.0f);
    win.apply();

    ASSERT_TRUE(model.can_undo());
    model.undo();

    node_ptr = model.current().find_node(interner.intern("knob1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_FLOAT_EQ(node_ptr->view.content_max, 2.0f)
        << "Undo must revert content_max for Knob nodes";
}

// When the user changes "min" or "max" param for a Slider-type node,
// apply() must sync content_min / content_max.
TEST_F(PropertiesWindowTest, ApplySliderMinMaxSyncsContentRange) {
    bp2::Blueprint::Node n;
    n.semantic.id = interner.intern("slider1");
    n.semantic.type = interner.intern("Slider");
    n.view.name = "slider1";
    n.view.content_type = bp2::NodeContentType::Slider;
    n.view.content_min = 0.0f;
    n.view.content_max = 100.0f;
    n.semantic.params[interner.intern("min")] = 0.0f;
    n.semantic.params[interner.intern("max")] = 100.0f;
    model.add_node(std::move(n));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("slider1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "slider1", model, interner, &registry, [](const std::string&) {});
    win.set_pending_param("min", -10.0f);
    win.set_pending_param("max", 200.0f);
    win.apply();

    node_ptr = model.current().find_node(interner.intern("slider1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_FLOAT_EQ(node_ptr->view.content_min, -10.0f)
        << "content_min must be synced from 'min' param for Slider nodes";
    EXPECT_FLOAT_EQ(node_ptr->view.content_max, 200.0f)
        << "content_max must be synced from 'max' param for Slider nodes";
}

// When the user changes "min" or "max" param for a Gauge-type node,
// apply() must sync content_min / content_max.
TEST_F(PropertiesWindowTest, ApplyGaugeMinMaxSyncsContentRange) {
    bp2::Blueprint::Node n;
    n.semantic.id = interner.intern("gauge1");
    n.semantic.type = interner.intern("Voltmeter");
    n.view.name = "gauge1";
    n.view.content_type = bp2::NodeContentType::Gauge;
    n.view.content_min = 0.0f;
    n.view.content_max = 30.0f;
    n.semantic.params[interner.intern("min")] = 0.0f;
    n.semantic.params[interner.intern("max")] = 30.0f;
    model.add_node(std::move(n));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("gauge1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "gauge1", model, interner, &registry, [](const std::string&) {});
    win.set_pending_param("max", 60.0f);
    win.apply();

    node_ptr = model.current().find_node(interner.intern("gauge1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_FLOAT_EQ(node_ptr->view.content_min, 0.0f)
        << "content_min unchanged for Gauge nodes";
    EXPECT_FLOAT_EQ(node_ptr->view.content_max, 60.0f)
        << "content_max must be synced from 'max' param for Gauge nodes";
}

TEST_F(PropertiesWindowTest, ApplyKnobPositionsPreservesLiveContentValue) {
    bp2::Blueprint::Node n;
    n.semantic.id = interner.intern("knob1");
    n.semantic.type = interner.intern("KnobSwitch");
    n.view.name = "knob1";
    n.view.content_type = bp2::NodeContentType::Knob;
    n.view.content_value = 3.0f;
    n.view.content_max = 5.0f;
    n.semantic.params[interner.intern("positions")] = 5.0f;
    model.add_node(std::move(n));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("knob1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "knob1", model, interner, &registry, [](const std::string&) {});
    win.set_pending_param("positions", 7.0f);
    win.apply();

    node_ptr = model.current().find_node(interner.intern("knob1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_FLOAT_EQ(node_ptr->view.content_value, 3.0f)
        << "apply() must preserve live content_value while rehydrating static knob fields";
    EXPECT_FLOAT_EQ(node_ptr->view.content_max, 7.0f)
        << "apply() must still refresh static knob range from semantic params";
}

TEST_F(PropertiesWindowTest, ApplySwitchClosedReseedsLiveContentState) {
    bp2::Blueprint::Node n;
    n.semantic.id = interner.intern("switch1");
    n.semantic.type = interner.intern("Switch");
    n.view.name = "switch1";
    n.view.content_type = bp2::NodeContentType::Switch;
    n.view.content_state = false;
    n.semantic.params[interner.intern("closed")] = 0.0f;
    model.add_node(std::move(n));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("switch1"));
    ASSERT_NE(node_ptr, nullptr);

    TypeDefinition switch_def;
    switch_def.classname = "Switch";
    switch_def.params["closed"] = ParamSpec{ParamSchemaType::Bool, "false"};
    registry.types["Switch"] = switch_def;
    registry.presentation.specs["Switch"].content_type = "Switch";

    PropertiesWindow win;
    win.open(*node_ptr, "switch1", model, interner, &registry, [](const std::string&) {});
    win.set_pending_param("closed", 1.0f);
    win.apply();

    node_ptr = model.current().find_node(interner.intern("switch1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_TRUE(node_ptr->view.content_state)
        << "apply() must reseed live switch state when the semantic default 'closed' changes";
}

TEST_F(PropertiesWindowTest, ApplyAzsClosedReseedsLiveVerticalToggleState) {
    TypeDefinition azs_def;
    azs_def.classname = "AZS";
    azs_def.params["closed"] = ParamSpec{ParamSchemaType::Bool, "false"};
    registry.types["AZS"] = azs_def;
    registry.presentation.specs["AZS"].content_type = "VerticalToggle";

    bp2::Blueprint::Node n;
    n.semantic.id = interner.intern("azs1");
    n.semantic.type = interner.intern("AZS");
    n.view.name = "azs1";
    n.view.content_type = bp2::NodeContentType::VerticalToggle;
    n.view.content_state = false;
    n.semantic.params[interner.intern("closed")] = 0.0f;
    model.add_node(std::move(n));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("azs1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "azs1", model, interner, &registry, [](const std::string&) {});
    win.set_pending_param("closed", 1.0f);
    win.apply();

    node_ptr = model.current().find_node(interner.intern("azs1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_TRUE(node_ptr->view.content_state)
        << "apply() must reseed live AZS toggle state when the semantic default 'closed' changes";
}

TEST_F(PropertiesWindowTest, ApplyRelayClosedReseedsLiveSwitchState) {
    TypeDefinition relay_def;
    relay_def.classname = "Relay";
    relay_def.params["closed"] = ParamSpec{ParamSchemaType::Bool, "false"};
    registry.types["Relay"] = relay_def;
    registry.presentation.specs["Relay"].content_type = "Switch";

    bp2::Blueprint::Node n;
    n.semantic.id = interner.intern("relay1");
    n.semantic.type = interner.intern("Relay");
    n.view.name = "relay1";
    n.view.content_type = bp2::NodeContentType::Switch;
    n.view.content_state = false;
    n.semantic.params[interner.intern("closed")] = 0.0f;
    model.add_node(std::move(n));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("relay1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "relay1", model, interner, &registry, [](const std::string&) {});
    win.set_pending_param("closed", 1.0f);
    win.apply();

    node_ptr = model.current().find_node(interner.intern("relay1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_TRUE(node_ptr->view.content_state)
        << "apply() must reseed live Relay switch state when the semantic default 'closed' changes";
}
