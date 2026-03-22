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

TEST_F(PropertiesWindowTest, ParamAndNameChangeSingleUndo) {
    auto n = make_node(interner, "bat1", {{"v", 28.0f}});
    n.name = "OriginalName";
    model.add_node(std::move(n));

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

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("bat1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "bat1", model, interner, [](const std::string&) {});

    // Remove node before apply
    model.remove_node(interner.intern("bat1"));

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

    const bp2::Blueprint::Node* node_ptr = model.current().find_node(interner.intern("azs1"));
    ASSERT_NE(node_ptr, nullptr);

    PropertiesWindow win;
    win.open(*node_ptr, "azs1", model, interner, [](const std::string&) {});

    // No changes to layout overrides (still empty)
    win.apply();

    EXPECT_FALSE(model.can_undo()) << "No changes should not push to undo stack";
}
