#include <gtest/gtest.h>
#include "editor/window/properties_window.h"
#include "editor/commands/commands.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "ui/core/interned_id.h"

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
    n.id   = I.intern(id);
    n.type = I.intern("Battery");
    n.name = id;
    for (auto& [k, v] : params)
        n.params[I.intern(k)] = v;
    return n;
}

static bp2::Blueprint::Node make_bridge_node(ui::StringInterner& I,
                                             const char* id,
                                             bool input_bridge,
                                             PortType t) {
    bp2::Blueprint::Node n;
    n.id = I.intern(id);
    n.type = I.intern(input_bridge ? "BlueprintInput" : "BlueprintOutput");
    n.name = id;
    if (input_bridge) {
        n.inputs.emplace_back(I.intern("ext"), PortSide::Input, t);
        n.outputs.emplace_back(I.intern("port"), PortSide::Output, t);
    } else {
        n.inputs.emplace_back(I.intern("port"), PortSide::Input, t);
        n.outputs.emplace_back(I.intern("ext"), PortSide::Output, t);
    }
    return n;
}

// =============================================================================
// Test fixture: EditorModel + StringInterner pre-built
// =============================================================================

class PropertiesWindowTest : public ::testing::Test {
protected:
    ui::StringInterner interner;
    bp2::EditorModel   model;
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

    win.open(*node_ptr, "bat1", model, interner, [](const std::string&) {});
    EXPECT_TRUE(win.is_open());
    EXPECT_EQ(win.target_node_id_str(), "bat1");
}

TEST_F(PropertiesWindowTest, OpenInitializesPendingState) {
    model.add_node(make_node(interner, "bat1", {{"v", 28.0f}, {"r", 0.01f}}));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", model, interner, [](const std::string&) {});

    // Pending state should mirror the node's current values
    EXPECT_EQ(win.pending_name(), "bat1");
    EXPECT_FLOAT_EQ(win.pending_params().at("v"), 28.0f);
    EXPECT_FLOAT_EQ(win.pending_params().at("r"), 0.01f);
}

TEST_F(PropertiesWindowTest, OpenKeepsLutTableAsStringParam) {
    bp2::Blueprint::Node lut;
    lut.id = interner.intern("lut_1");
    lut.type = interner.intern("LUT");
    lut.name = "lut_1";
    lut.string_params["table"] = "0:0; 100:100";
    model.add_node(std::move(lut));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("lut_1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "lut_1", model, interner, [](const std::string&) {});

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
    win.open(*node_ptr, "bp_in_1", model, interner, [](const std::string&) {});

    ASSERT_TRUE(win.pending_bridge_port_type().has_value());
    EXPECT_EQ(*win.pending_bridge_port_type(), PortType::V);
}

TEST_F(PropertiesWindowTest, CancelDoesNotMutateLiveNode) {
    model.add_node(make_node(interner, "bat1", {{"v", 28.0f}, {"r", 0.01f}}));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", model, interner, [](const std::string&) {});

    // Simulate user editing pending state
    win.set_pending_param("v", 12.0f);
    win.set_pending_name("modified_name");

    // Cancel should NOT touch the live node
    win.close();

    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    auto v_it = node_ptr->params.find(interner.intern("v"));
    auto r_it = node_ptr->params.find(interner.intern("r"));
    ASSERT_NE(v_it, node_ptr->params.end());
    ASSERT_NE(r_it, node_ptr->params.end());
    EXPECT_FLOAT_EQ(v_it->second, 28.0f) << "Cancel must not mutate live node";
    EXPECT_FLOAT_EQ(r_it->second, 0.01f) << "Untouched params preserved";
    EXPECT_EQ(node_ptr->name, "bat1") << "Cancel must not mutate live name";
    EXPECT_FALSE(win.is_open());
}

TEST_F(PropertiesWindowTest, LiveNodeUntouchedDuringEditing) {
    model.add_node(make_node(interner, "bat1", {{"v", 28.0f}}));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", model, interner, [](const std::string&) {});

    // Edit pending params
    win.set_pending_param("v", 99.0f);
    win.set_pending_name("CHANGED");

    // Live node must remain untouched while editing is in progress
    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    auto v_it = node_ptr->params.find(interner.intern("v"));
    ASSERT_NE(v_it, node_ptr->params.end());
    EXPECT_FLOAT_EQ(v_it->second, 28.0f) << "Live node must not change during editing";
    EXPECT_EQ(node_ptr->name, "bat1") << "Live name must not change during editing";
}

TEST_F(PropertiesWindowTest, OpenTwiceDiscardsFirstSession) {
    model.add_node(make_node(interner, "bat1", {{"v", 28.0f}}));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;

    // First open
    win.open(*node_ptr, "bat1", model, interner, [](const std::string&) {});
    win.set_pending_param("v", 12.0f);

    // Open again — first session's pending edits are discarded
    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    win.open(*node_ptr, "bat1", model, interner, [](const std::string&) {});

    // Live node was never mutated
    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    auto v_it = node_ptr->params.find(interner.intern("v"));
    ASSERT_NE(v_it, node_ptr->params.end());
    EXPECT_FLOAT_EQ(v_it->second, 28.0f) << "Live node must not have been mutated by first session";

    // Pending state should be fresh from the live node
    EXPECT_FLOAT_EQ(win.pending_params().at("v"), 28.0f)
        << "Second open() must re-snapshot from live node";

    // Cancel the second open
    win.close();
    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    v_it = node_ptr->params.find(interner.intern("v"));
    ASSERT_NE(v_it, node_ptr->params.end());
    EXPECT_FLOAT_EQ(v_it->second, 28.0f);
}

TEST_F(PropertiesWindowTest, ClosedWindowIsNotOpen) {
    model.add_node(make_node(interner, "bat1", {}));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", model, interner, [](const std::string&) {});
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
    win.open(*node_ptr, "bat1", model, interner, [](const std::string&) {});

    // Simulate user changing voltage via pending state
    win.set_pending_param("v", 14.0f);

    // Apply — should snapshot and apply changes to undo stack
    win.apply();

    EXPECT_FALSE(win.is_open());
    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    auto v_it = node_ptr->params.find(interner.intern("v"));
    auto r_it = node_ptr->params.find(interner.intern("r"));
    ASSERT_NE(v_it, node_ptr->params.end());
    ASSERT_NE(r_it, node_ptr->params.end());
    EXPECT_FLOAT_EQ(v_it->second, 14.0f) << "Applied value must persist";
    EXPECT_FLOAT_EQ(r_it->second, 0.01f) << "Untouched param preserved";
    EXPECT_TRUE(model.can_undo()) << "Undo stack must have an entry";
}

TEST_F(PropertiesWindowTest, ApplyBridgePortTypeUpdatesBothPortsAndUndoRestores) {
    model.add_node(make_bridge_node(interner, "bp_in_1", true, PortType::V));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bp_in_1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bp_in_1", model, interner, [](const std::string&) {});
    win.set_pending_bridge_port_type(PortType::RPM);
    win.apply();

    node_ptr = model.current().find_node(interner.intern("bp_in_1"));
    ASSERT_NE(node_ptr, nullptr);
    ASSERT_EQ(node_ptr->inputs.size(), 1u);
    ASSERT_EQ(node_ptr->outputs.size(), 1u);
    EXPECT_EQ(node_ptr->inputs[0].type, PortType::RPM);
    EXPECT_EQ(node_ptr->outputs[0].type, PortType::RPM);

    ASSERT_TRUE(model.can_undo());
    model.undo();
    node_ptr = model.current().find_node(interner.intern("bp_in_1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_EQ(node_ptr->inputs[0].type, PortType::V);
    EXPECT_EQ(node_ptr->outputs[0].type, PortType::V);
}

TEST_F(PropertiesWindowTest, ApplyBridgePortTypePropagatesToCollapsedNodeAndNestedIface) {
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("bp"));

    bp2::Blueprint::Node bridge = make_bridge_node(interner, "inst1:in", true, PortType::V);
    bridge.group_id = "inst1";

    bp2::Blueprint::Node collapsed;
    collapsed.id = interner.intern("inst1");
    collapsed.type = interner.intern("bp_type");
    collapsed.name = "inst1";
    collapsed.inputs.emplace_back(interner.intern("in"), PortSide::Input, PortType::V);

    bp2::Blueprint::Nested nested;
    nested.id = interner.intern("inst1");
    nested.blueprint_id = interner.intern("bp_type");
    nested.embedded = true;
    nested.iface = bp2::Interface({
        {interner.intern("in"), Domain::Electrical, bp2::Direction::Input},
    });

    bp = bp.with_node(std::move(bridge));
    bp = bp.with_node(std::move(collapsed));
    bp = bp.with_nested(std::move(nested));
    model.replace_current(std::move(bp));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("inst1:in"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "inst1:in", model, interner, [](const std::string&) {});
    win.set_pending_bridge_port_type(PortType::RPM);
    win.apply();

    const auto* collapsed_after = model.current().find_node(interner.intern("inst1"));
    ASSERT_NE(collapsed_after, nullptr);
    ASSERT_EQ(collapsed_after->inputs.size(), 1u);
    EXPECT_EQ(collapsed_after->inputs[0].type, PortType::RPM);

    const auto* nested_after = model.current().find_nested(interner.intern("inst1"));
    ASSERT_NE(nested_after, nullptr);
    auto pd = nested_after->iface.find(interner.intern("in"));
    ASSERT_TRUE(pd.has_value());
    EXPECT_EQ(pd->domain, Domain::Mechanical);
}

TEST_F(PropertiesWindowTest, ApplyThenUndoRevertsParam) {
    model.add_node(make_node(interner, "bat1", {{"v", 28.0f}, {"r", 0.01f}}));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", model, interner, [](const std::string&) {});

    win.set_pending_param("v", 14.0f);
    win.set_pending_param("r", 0.05f);
    win.apply();

    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_FLOAT_EQ(node_ptr->params.at(interner.intern("v")), 14.0f);
    EXPECT_FLOAT_EQ(node_ptr->params.at(interner.intern("r")), 0.05f);

    // Undo
    ASSERT_TRUE(model.can_undo());
    model.undo();

    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_FLOAT_EQ(node_ptr->params.at(interner.intern("v")), 28.0f) << "Undo must revert v";
    EXPECT_FLOAT_EQ(node_ptr->params.at(interner.intern("r")), 0.01f) << "Undo must revert r";

    // Redo
    model.redo();
    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_FLOAT_EQ(node_ptr->params.at(interner.intern("v")), 14.0f) << "Redo must restore v";
    EXPECT_FLOAT_EQ(node_ptr->params.at(interner.intern("r")), 0.05f) << "Redo must restore r";
}

TEST_F(PropertiesWindowTest, ApplyNoChangesDoesNotPushUndo) {
    model.add_node(make_node(interner, "bat1", {{"v", 28.0f}}));
    model.clear_history(); // setup only — not part of undo test

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", model, interner, [](const std::string&) {});

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
    win.open(*node_ptr, "bat1", model, interner,
        [&](const std::string& nid) {
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
    n.name = "OriginalName";
    model.add_node(std::move(n));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", model, interner, [](const std::string&) {});

    win.set_pending_name("NewName");
    win.apply();

    EXPECT_TRUE(model.can_undo()) << "Name change should push to undo stack";
    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_EQ(node_ptr->name, "NewName");
}

TEST_F(PropertiesWindowTest, NameChangeUndoRestoresOldName) {
    auto n = make_node(interner, "bat1", {{"v", 28.0f}});
    n.name = "OriginalName";
    model.add_node(std::move(n));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", model, interner, [](const std::string&) {});

    win.set_pending_name("NewName");
    win.apply();

    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_EQ(node_ptr->name, "NewName");

    // Undo
    model.undo();

    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_EQ(node_ptr->name, "OriginalName") << "Undo should restore original name";
}

TEST_F(PropertiesWindowTest, NameChangePreservesNodeAndWireOrder) {
    bp2::Blueprint bp;

    bp2::Blueprint::Node src;
    src.id = interner.intern("src");
    src.type = interner.intern("Battery");
    src.name = "src";
    src.outputs.emplace_back(interner.intern("v_out"), PortSide::Output, PortType::V);

    bp2::Blueprint::Node bus;
    bus.id = interner.intern("bus");
    bus.type = interner.intern("Bus");
    bus.name = "bus";
    bus.render_hint = "bus";
    bus.inputs.emplace_back(interner.intern("v"), PortSide::InOut, PortType::V);
    bus.outputs.emplace_back(interner.intern("v"), PortSide::InOut, PortType::V);

    bp2::Blueprint::Node load;
    load.id = interner.intern("load");
    load.type = interner.intern("Lamp");
    load.name = "load";
    load.inputs.emplace_back(interner.intern("v_in"), PortSide::Input, PortType::V);

    bp = bp.with_node(src);
    bp = bp.with_node(bus);
    bp = bp.with_node(load);

    bp2::Blueprint::Wire w0;
    w0.id = interner.intern("wire_0");
    w0.source = bp2::Path{};
    w0.target = bp2::Path{};
    bp = bp.with_wire(w0);

    bp2::Blueprint::Wire w1;
    w1.id = interner.intern("wire_1");
    w1.source = bp2::Path{};
    w1.target = bp2::Path{};
    bp = bp.with_wire(w1);

    model.replace_current(std::move(bp));

    std::vector<ui::InternedId> node_order_before;
    for (const auto& n : model.current().nodes()) node_order_before.push_back(n.id);

    std::vector<ui::InternedId> wire_order_before;
    for (const auto& w : model.current().wires()) wire_order_before.push_back(w.id);

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bus"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bus", model, interner, [](const std::string&) {});
    win.set_pending_name("bus_renamed");
    win.apply();

    std::vector<ui::InternedId> node_order_after;
    for (const auto& n : model.current().nodes()) node_order_after.push_back(n.id);

    std::vector<ui::InternedId> wire_order_after;
    for (const auto& w : model.current().wires()) wire_order_after.push_back(w.id);

    EXPECT_EQ(node_order_after, node_order_before);
    EXPECT_EQ(wire_order_after, wire_order_before);
}

TEST_F(PropertiesWindowTest, ParamAndNameChangeSingleUndo) {
    auto n = make_node(interner, "bat1", {{"v", 28.0f}});
    n.name = "OriginalName";
    model.add_node(std::move(n));
    model.clear_history(); // setup only — not part of undo test

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", model, interner, [](const std::string&) {});

    // Change both param and name in one "Apply"
    win.set_pending_param("v", 14.0f);
    win.set_pending_name("NewName");
    win.apply();

    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_FLOAT_EQ(node_ptr->params.at(interner.intern("v")), 14.0f);
    EXPECT_EQ(node_ptr->name, "NewName");

    // A single Ctrl+Z should revert BOTH changes
    ASSERT_TRUE(model.can_undo());
    model.undo();

    node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_FLOAT_EQ(node_ptr->params.at(interner.intern("v")), 28.0f) << "Single undo must revert param";
    EXPECT_EQ(node_ptr->name, "OriginalName") << "Single undo must revert name";
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
    win.open(*node_ptr, "bat1", model, interner, [](const std::string&) {});
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
    win.open(*node_ptr, "bat1", model, interner, [](const std::string&) {});

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
    win.open(*node_ptr, "bat1", model, interner, [](const std::string&) {});

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
    n.id   = interner.intern("azs1");
    n.type = interner.intern("AZS");
    n.name = "AZS";
    n.inputs.push_back(EditorPort(interner.intern("v_in"),  PortSide::Input,  PortType::V));
    n.outputs.push_back(EditorPort(interner.intern("v_out"), PortSide::Output, PortType::V));
    n.outputs.push_back(EditorPort(interner.intern("state"), PortSide::Output, PortType::Bool));
    model.add_node(std::move(n));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("azs1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "azs1", model, interner, [](const std::string&) {});

    // Set port layout overrides
    std::vector<bp2::Blueprint::Node::PortLayoutOverride> overrides;
    overrides.push_back({"v_in",  std::string("top"),   std::nullopt});
    overrides.push_back({"v_out", std::string("right"), 0});
    win.set_pending_layout_overrides(overrides);

    win.apply();

    // Verify the node's layout_overrides were updated
    node_ptr = model.current().find_node(interner.intern("azs1"));
    ASSERT_NE(node_ptr, nullptr);
    ASSERT_EQ(node_ptr->layout_overrides.size(), 2u);
    EXPECT_EQ(node_ptr->layout_overrides[0].port_name, "v_in");
    EXPECT_EQ(node_ptr->layout_overrides[0].side, std::string("top"));
    EXPECT_EQ(node_ptr->layout_overrides[1].port_name, "v_out");
    EXPECT_EQ(node_ptr->layout_overrides[1].side, std::string("right"));
    EXPECT_EQ(node_ptr->layout_overrides[1].position, 0);
}

TEST_F(PropertiesWindowTest, PortLayoutOverride_UndoRestoresOriginal) {
    bp2::Blueprint::Node n;
    n.id   = interner.intern("azs1");
    n.type = interner.intern("AZS");
    n.name = "AZS";
    n.inputs.push_back(EditorPort(interner.intern("v_in"),  PortSide::Input,  PortType::V));
    n.outputs.push_back(EditorPort(interner.intern("v_out"), PortSide::Output, PortType::V));
    model.add_node(std::move(n));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("azs1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_TRUE(node_ptr->layout_overrides.empty()) << "Initial layout_overrides should be empty";

    PropertiesWindow win;
    win.open(*node_ptr, "azs1", model, interner, [](const std::string&) {});

    // Add port layout override
    std::vector<bp2::Blueprint::Node::PortLayoutOverride> overrides;
    overrides.push_back({"v_in", std::string("bottom"), std::nullopt});
    win.set_pending_layout_overrides(overrides);

    win.apply();

    node_ptr = model.current().find_node(interner.intern("azs1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_EQ(node_ptr->layout_overrides.size(), 1u);

    // Undo should restore empty layout_overrides
    ASSERT_TRUE(model.can_undo());
    model.undo();

    node_ptr = model.current().find_node(interner.intern("azs1"));
    ASSERT_NE(node_ptr, nullptr);
    EXPECT_TRUE(node_ptr->layout_overrides.empty()) << "Undo should restore empty layout_overrides";
}

TEST_F(PropertiesWindowTest, PortLayoutOverride_NoChangesDoesNotPushUndo) {
    bp2::Blueprint::Node n;
    n.id   = interner.intern("azs1");
    n.type = interner.intern("AZS");
    n.name = "AZS";
    n.inputs.push_back(EditorPort(interner.intern("v_in"), PortSide::Input, PortType::V));
    model.add_node(std::move(n));
    model.clear_history(); // setup only — not part of undo test

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("azs1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "azs1", model, interner, [](const std::string&) {});

    // No changes to layout overrides (still empty)
    win.apply();

    EXPECT_FALSE(model.can_undo()) << "No changes should not push to undo stack";
}

// =============================================================================
// Phase 6: Bridge Node Duplicate Control Regression (Bug 2)
// =============================================================================

// Regression: BlueprintInput/BlueprintOutput nodes carry exposed_type and
// exposed_direction as string_params (from param_defaults).  The properties
// dialog must NOT render these as generic text-input controls because they are
// already covered by the dedicated PortType dropdown
// (render_bridge_port_type_section).  This test verifies that the data layer
// correctly carries these keys in pending_string_params_ so the rendering
// skip logic has something to filter.  The actual skip is in the #ifndef
// EDITOR_TESTING render() path (see properties_window.cpp line ~210).
TEST_F(PropertiesWindowTest, BridgeNode_ExposedParamsInStringParams) {
    auto n = make_bridge_node(interner, "inst:my_input", true, PortType::V);
    n.string_params["exposed_type"]      = "V";
    n.string_params["exposed_direction"] = "In";
    model.add_node(std::move(n));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("inst:my_input"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "inst:my_input", model, interner, [](const std::string&) {});

    // The keys must be present in pending_string_params — they exist as data
    const auto& sp = win.pending_string_params();
    EXPECT_NE(sp.find("exposed_type"), sp.end())
        << "exposed_type must be present in pending string params";
    EXPECT_NE(sp.find("exposed_direction"), sp.end())
        << "exposed_direction must be present in pending string params";
    EXPECT_EQ(sp.at("exposed_type"), "V");
    EXPECT_EQ(sp.at("exposed_direction"), "In");

    // The bridge port type dropdown should be initialized from the node ports
    EXPECT_TRUE(win.pending_bridge_port_type().has_value());
    EXPECT_EQ(*win.pending_bridge_port_type(), PortType::V);
}

// Regression: changing bridge port type via the dropdown must NOT produce a
// second (duplicate) undo entry from the string params path.  Verify that
// apply() with only a bridge port type change (no string param edits) still
// round-trips cleanly.
TEST_F(PropertiesWindowTest, BridgeNode_PortTypeChangeAppliesCleanly) {
    auto n = make_bridge_node(interner, "inst:my_output", false, PortType::V);
    n.string_params["exposed_type"]      = "V";
    n.string_params["exposed_direction"] = "Out";
    model.add_node(std::move(n));

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("inst:my_output"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "inst:my_output", model, interner, [](const std::string&) {});

    // Change port type from V to Bool via the dropdown mechanism
    win.set_pending_bridge_port_type(PortType::Bool);
    win.apply();

    // Verify the port type was updated on the node
    node_ptr = model.current().find_node(interner.intern("inst:my_output"));
    ASSERT_NE(node_ptr, nullptr);
    ASSERT_FALSE(node_ptr->inputs.empty());
    EXPECT_EQ(node_ptr->inputs.front().type, PortType::Bool);

    // String params should still carry the original exposed_type (not auto-updated
    // from the dropdown — that's a separate serialization concern)
    EXPECT_EQ(node_ptr->string_params.at("exposed_type"), "V");
}
