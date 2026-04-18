#include <gtest/gtest.h>
#include "ui/core/interned_id.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/library/blueprint_library.h"
#include "blueprint_v2/validation/invariant_checker.h"
#include "blueprint_v2/interface/type_definition_interface.h"
#include "json_parser/json_parser.h"
#include "editor/subwindow_open_target.h"
#include <random>

TEST(EditorModel, EmptyByDefault) {
     bp2::EditorModel model;
     EXPECT_TRUE(model.current().nodes().empty());
     EXPECT_TRUE(model.current().wires().empty());
 }

TEST(EditorModel, ConstructWithBlueprint) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("test"));
    bp2::EditorModel model(bp);
    EXPECT_EQ(interner.resolve(model.current().id()), "test");
}

TEST(EditorModel, AddNode) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("n1");
    node.semantic.type = interner.intern("Battery");

    EXPECT_TRUE(model.add_node(std::move(node)));
    EXPECT_EQ(model.current().nodes().size(), 1u);
}

TEST(EditorModel, DuplicateNodeRejected) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    bp2::Blueprint::Node n1, n2;
    n1.semantic.id = interner.intern("same_id");
    n1.semantic.type = interner.intern("Battery");
    n2.semantic.id = interner.intern("same_id");
    n2.semantic.type = interner.intern("Resistor");

    EXPECT_TRUE(model.add_node(std::move(n1)));
    EXPECT_FALSE(model.add_node(std::move(n2)));
    EXPECT_EQ(model.current().nodes().size(), 1u);
    EXPECT_EQ(model.undo_depth(), 1u);
}

TEST(EditorModel, RemoveNode) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("n1");
    node.semantic.type = interner.intern("Battery");
    model.add_node(std::move(node));

    EXPECT_TRUE(model.remove_node(interner.intern("n1")));
    EXPECT_EQ(model.current().nodes().size(), 0u);
}

TEST(EditorModel, RemoveNonexistentNode) {
    ui::StringInterner interner;
    bp2::EditorModel model;
    EXPECT_FALSE(model.remove_node(interner.intern("nope")));
}

TEST(EditorModel, RemoveHostNodeAlsoRemovesEmbeddedBlueprint) {
     ui::StringInterner interner;

     bp2::Blueprint inner;
     inner = inner.with_id(interner.intern("CompositeType"));

     bp2::Blueprint::Node host;
     host.semantic.id = interner.intern("host1");
     host.semantic.type = interner.intern("CompositeType");
     host.content = bp2::Blueprint::Node::BlueprintInstanceData{
         bp2::Blueprint::Node::BlueprintSource::make_embedded(
         std::make_unique<bp2::Blueprint>(inner))
     };

     bp2::Blueprint bp;
     bp = bp.with_node(std::move(host));

     bp2::EditorModel model(bp);
     EXPECT_TRUE(model.remove_node(interner.intern("host1")));
     EXPECT_EQ(model.current().find_node(interner.intern("host1")), nullptr);
 }

TEST(EditorModel, AddWire) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::EditorModel model;

    bp2::Blueprint::Node n1, n2;
    n1.semantic.id = interner.intern("b1");
    n1.semantic.type = interner.intern("Battery");
    n2.semantic.id = interner.intern("r1");
    n2.semantic.type = interner.intern("Resistor");
    model.add_node(std::move(n1));
    model.add_node(std::move(n2));

    bp2::Blueprint::Wire wire;
    wire.id = interner.intern("w1");
    wire.source = bp2::WireEndpoint{interner.intern("b1"), interner.intern("v_out")};
    wire.target = bp2::WireEndpoint{interner.intern("r1"), interner.intern("in")};

    EXPECT_TRUE(model.add_wire(std::move(wire)));
    EXPECT_EQ(model.current().wires().size(), 1u);
}

TEST(EditorModel, RejectsSelfLoopWire) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::EditorModel model;

    bp2::Blueprint::Node n1;
    n1.semantic.id = interner.intern("n1");
    n1.semantic.type = interner.intern("Battery");
    model.add_node(std::move(n1));

    bp2::Blueprint::Wire w;
    w.id = interner.intern("w_self");
    auto port = bp2::WireEndpoint{interner.intern("n1"), interner.intern("v_out")};
    w.source = port;
    w.target = port;  // self-loop

    EXPECT_FALSE(model.add_wire(std::move(w)));
}


TEST(EditorModel, UndoRestoresPreviousState) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("n1");
    node.semantic.type = interner.intern("Battery");

    model.add_node(std::move(node));
    EXPECT_EQ(model.current().nodes().size(), 1u);

    model.undo();
    EXPECT_EQ(model.current().nodes().size(), 0u);
}

TEST(EditorModel, RedoAfterUndo) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("n1");
    node.semantic.type = interner.intern("Battery");

    model.add_node(std::move(node));
    model.undo();
    EXPECT_TRUE(model.can_redo());
    EXPECT_EQ(model.redo_depth(), 1u);

    model.redo();
    EXPECT_EQ(model.current().nodes().size(), 1u);
}

TEST(EditorModel, NewActionClearsRedoStack) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    bp2::Blueprint::Node n1, n2;
    n1.semantic.id = interner.intern("n1");
    n1.semantic.type = interner.intern("Battery");
    n2.semantic.id = interner.intern("n2");
    n2.semantic.type = interner.intern("Resistor");

    model.add_node(std::move(n1));
    model.undo();
    model.add_node(std::move(n2));
    EXPECT_FALSE(model.can_redo());
    EXPECT_EQ(model.redo_depth(), 0u);
}

TEST(EditorModel, UpdateNodePosition) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("n1");
    node.semantic.type = interner.intern("Battery");
    node.layout.x = 100.0f;
    node.layout.y = 200.0f;
    model.add_node(std::move(node));

    model.update_node_position(interner.intern("n1"), 300.0f, 400.0f);

    auto* found = model.current().find_node(interner.intern("n1"));
    ASSERT_NE(found, nullptr);
    EXPECT_FLOAT_EQ(found->layout.x, 300.0f);
    EXPECT_FLOAT_EQ(found->layout.y, 400.0f);
}

TEST(EditorModel, UpdateNodePositionCreatesCheckpoint) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    bp2::Blueprint::Node node;
    node.semantic.id = interner.intern("n1");
    node.semantic.type = interner.intern("Battery");
    model.add_node(std::move(node));

    EXPECT_EQ(model.undo_depth(), 1u);  // add_node checkpoint
    model.update_node_position(interner.intern("n1"), 50.0f, 60.0f);
    EXPECT_EQ(model.undo_depth(), 2u);  // update_node_position checkpoint

    model.undo();
    auto* found = model.current().find_node(interner.intern("n1"));
    ASSERT_NE(found, nullptr);
    EXPECT_FLOAT_EQ(found->layout.x, 0.0f);  // Original position
}


TEST(EditorModel, UpdateNodeCannotOverrideEmbeddedCompositeIfaceAuthority) {
     ui::StringInterner interner;

     bp2::Blueprint inner;
     inner = inner.with_interface(bp2::Interface({
         {interner.intern("inner_only"), Domain::Electrical, bp2::Direction::Input, PortType::V},
     }));

bp2::Blueprint::Node collapsed;
      collapsed.semantic.id = interner.intern("sub1");
      collapsed.semantic.type = interner.intern("CompositeType");
      collapsed.content = bp2::Blueprint::Node::BlueprintInstanceData{
          bp2::Blueprint::Node::BlueprintSource::make_embedded(
          std::make_unique<bp2::Blueprint>(inner.with_id(interner.intern("CompositeType"))))
      };

      bp2::Blueprint root;
      root = root.with_node(collapsed);

      bp2::EditorModel model(root);
      const auto* node_before = model.current().find_node(interner.intern("sub1"));
      ASSERT_NE(node_before, nullptr);

      // The node's source authority should expose the embedded blueprint iface
      ASSERT_TRUE(node_before->is_blueprint_instance());
      ASSERT_NE(node_before->blueprint_instance().source.inline_def(), nullptr);
      EXPECT_EQ(node_before->blueprint_instance().source.inline_def()->iface().find(interner.intern("inner_only")).has_value(), true);
  }

// Regression: constructor must canonicalize embedded blueprint interface.
TEST(EditorModel, ConstructorCanonicalizesEmbeddedCompositeHostIface) {
     ui::StringInterner interner;

     bp2::Blueprint inner;
     inner = inner.with_interface(bp2::Interface({
         {interner.intern("authoritative_port"), Domain::Electrical, bp2::Direction::Input, PortType::V},
     }));

     bp2::Blueprint::Node collapsed;
collapsed.semantic.id = interner.intern("sub1");
      collapsed.semantic.type = interner.intern("CompositeType");
      collapsed.content = bp2::Blueprint::Node::BlueprintInstanceData{
          bp2::Blueprint::Node::BlueprintSource::make_embedded(
          std::make_unique<bp2::Blueprint>(inner.with_id(interner.intern("CompositeType"))))
      };

      bp2::Blueprint root;
      root = root.with_node(std::move(collapsed));

      bp2::EditorModel model(root);

       const auto* node = model.current().find_node(interner.intern("sub1"));
      ASSERT_NE(node, nullptr);
      EXPECT_TRUE(node->is_blueprint_instance());
      EXPECT_TRUE(node->has_embedded_blueprint());
      // Interface authority comes from source
      ASSERT_TRUE(node->is_blueprint_instance());
      ASSERT_NE(node->blueprint_instance().source.inline_def(), nullptr);
      EXPECT_TRUE(node->blueprint_instance().source.inline_def()->iface().find(interner.intern("authoritative_port")).has_value());
  }

TEST(EditorModel, ReplaceCurrentCanonicalizesEmbeddedCompositeIfaceAuthority) {
     ui::StringInterner interner;

     bp2::Blueprint inner;
     inner = inner.with_interface(bp2::Interface({
         {interner.intern("inner_only"), Domain::Electrical, bp2::Direction::Input, PortType::V},
     }));

bp2::Blueprint::Node collapsed;
      collapsed.semantic.id = interner.intern("sub1");
      collapsed.semantic.type = interner.intern("CompositeType");
      collapsed.content = bp2::Blueprint::Node::BlueprintInstanceData{
          bp2::Blueprint::Node::BlueprintSource::make_embedded(
          std::make_unique<bp2::Blueprint>(inner.with_id(interner.intern("CompositeType"))))
      };

      bp2::Blueprint root;
      root = root.with_node(std::move(collapsed));

      bp2::EditorModel model;
      model.replace_current(std::move(root));

       const auto* updated = model.current().find_node(interner.intern("sub1"));
      ASSERT_NE(updated, nullptr);
      EXPECT_TRUE(updated->is_blueprint_instance());
      ASSERT_TRUE(updated->is_blueprint_instance());
      ASSERT_NE(updated->blueprint_instance().source.inline_def(), nullptr);
      EXPECT_TRUE(updated->blueprint_instance().source.inline_def()->iface().find(interner.intern("inner_only")).has_value());
  }


TEST(EditorModel, CanUndoInitiallyFalse) {
    bp2::EditorModel model;
    EXPECT_FALSE(model.can_undo());
}

TEST(EditorModel, CanRedoInitiallyFalse) {
    bp2::EditorModel model;
    EXPECT_FALSE(model.can_redo());
}

TEST(EditorModel, UndoDoesNothingWhenEmpty) {
    bp2::EditorModel model;
    model.undo();
    EXPECT_TRUE(model.current().nodes().empty());
}

TEST(EditorModel, RedoDoesNothingWhenEmpty) {
    bp2::EditorModel model;
    model.redo();
    EXPECT_TRUE(model.current().nodes().empty());
}

TEST(EditorModel, NodesInRectReturnsOnlyContainedNodes) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    bp2::Blueprint::Node n1;
    n1.semantic.id = interner.intern("n1");
    n1.semantic.type = interner.intern("Battery");
    n1.layout.x = 10.0f;
    n1.layout.y = 10.0f;

    bp2::Blueprint::Node n2;
    n2.semantic.id = interner.intern("n2");
    n2.semantic.type = interner.intern("Resistor");
    n2.layout.x = 100.0f;
    n2.layout.y = 100.0f;

    model.add_node(std::move(n1));
    model.add_node(std::move(n2));

    bp2::Rect rect;
    rect.x_min = 0.0f;
    rect.y_min = 0.0f;
    rect.x_max = 50.0f;
    rect.y_max = 50.0f;

    auto inside = model.nodes_in_rect(rect);
    ASSERT_EQ(inside.size(), 1u);
    EXPECT_EQ(inside[0], interner.intern("n1"));
}

TEST(EditorModel, NodesInRectReflectsNodeMovement) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    bp2::Blueprint::Node n1;
    n1.semantic.id = interner.intern("n1");
    n1.semantic.type = interner.intern("Battery");
    n1.layout.x = 10.0f;
    n1.layout.y = 10.0f;
    model.add_node(std::move(n1));

    bp2::Rect rect;
    rect.x_min = 0.0f;
    rect.y_min = 0.0f;
    rect.x_max = 50.0f;
    rect.y_max = 50.0f;

    auto before = model.nodes_in_rect(rect);
    ASSERT_EQ(before.size(), 1u);

    model.update_node_position(interner.intern("n1"), 200.0f, 200.0f);
    auto after = model.nodes_in_rect(rect);
    EXPECT_TRUE(after.empty());
}

TEST(EditorModel, WireExistsTracksAddAndRemove) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::EditorModel model;

    bp2::Blueprint::Node src;
    src.semantic.id = interner.intern("b1");
    src.semantic.type = interner.intern("Battery");

    bp2::Blueprint::Node dst;
    dst.semantic.id = interner.intern("r1");
    dst.semantic.type = interner.intern("Resistor");

    model.add_node(std::move(src));
    model.add_node(std::move(dst));

    bp2::WireEndpoint source{interner.intern("b1"), interner.intern("v_out")};
    bp2::WireEndpoint target{interner.intern("r1"), interner.intern("in")};

    bp2::Blueprint::Wire wire;
    wire.id = interner.intern("w1");
    wire.source = source;
    wire.target = target;

    EXPECT_FALSE(model.wire_exists(source, target));
    EXPECT_TRUE(model.add_wire(std::move(wire)));
    EXPECT_TRUE(model.wire_exists(source, target));

    EXPECT_TRUE(model.remove_wire(interner.intern("w1")));
    EXPECT_FALSE(model.wire_exists(source, target));
}

TEST(EditorModel, IsDirtyAfterUndoStackTruncation) {
    ui::StringInterner interner;
    bp2::EditorModel model;

    // Fill undo stack to exactly max_history_ (100)
    for (int i = 0; i < 100; ++i) {
        bp2::Blueprint::Node node;
        node.semantic.id = interner.intern(("a_" + std::to_string(i)).c_str());
        node.semantic.type = interner.intern("Battery");
        model.add_node(std::move(node));
    }

    // save_depth_ = 100
    model.mark_saved();
    EXPECT_FALSE(model.is_dirty());

    // One more add triggers truncation: erase front, push back.
    // Stack size goes 100 -> 99 -> 100. save_depth_ stays 100.
    // is_dirty() = (100 != 100) = false — BUG: saved state was evicted!
    bp2::Blueprint::Node extra;
    extra.semantic.id = interner.intern("extra");
    extra.semantic.type = interner.intern("Battery");
    model.add_node(std::move(extra));

    // The saved state (was at undo_stack_[0]) has been evicted.
    // Document should be dirty.
    EXPECT_TRUE(model.is_dirty());
}

TEST(EditorModel, RandomizedEditsMaintainInvariants) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry registry = load_type_registry("library/");
    TypeDefinition battery;
    battery.classname = "Battery";
    battery.cpp_class = true;
    battery.ports["v_out"] = Port{PortDirection::Out, PortType::V, Domain::Electrical, false};
    battery.ports["v_in"] = Port{PortDirection::In, PortType::V, Domain::Electrical, false};
    registry.types["Battery"] = std::move(battery);
    bp2::EditorModel model;

    std::mt19937 rng(1337u);
    int next_node = 0;
    int next_wire = 0;

    auto validate_now = [&]() {
        auto r = bp2::InvariantChecker::validate(model.current(), arena, registry, interner);
        ASSERT_TRUE(r.valid) << r.error;
    };

    auto choose_index = [&](size_t size) -> size_t {
        std::uniform_int_distribution<size_t> d(0, size - 1);
        return d(rng);
    };

    for (int step = 0; step < 600; ++step) {
        std::uniform_int_distribution<int> op_dist(0, 6);
        const int op = op_dist(rng);

        if (op == 0) {
            bp2::Blueprint::Node n;
            n.semantic.id = interner.intern("n_" + std::to_string(next_node++));
            n.semantic.type = interner.intern("Battery");
            std::uniform_real_distribution<float> pos(-500.0f, 500.0f);
            n.layout.x = pos(rng);
            n.layout.y = pos(rng);
            // Populate interface from registry (required by strict validation)
            const auto* def = registry.get("Battery");
            if (def) {
                n.component().iface = bp2::interface_from_type_definition(*def, interner);
            }
            EXPECT_TRUE(model.add_node(std::move(n)));
        } else if (op == 1) {
            const auto& nodes = model.current().nodes();
            if (nodes.size() >= 2) {
                const size_t src_i = choose_index(nodes.size());
                size_t dst_i = choose_index(nodes.size());
                if (dst_i == src_i) {
                    dst_i = (dst_i + 1) % nodes.size();
                }

                bp2::Blueprint::Wire w;
                w.id = interner.intern("w_" + std::to_string(next_wire++));
                 w.source = bp2::WireEndpoint{nodes[src_i].semantic.id, interner.intern("v_out")};
                 w.target = bp2::WireEndpoint{nodes[dst_i].semantic.id, interner.intern("v_in")};
                EXPECT_TRUE(model.add_wire(std::move(w)));
            }
        } else if (op == 2) {
            const auto& wires = model.current().wires();
            if (!wires.empty()) {
                const auto& w = wires[choose_index(wires.size())];
                EXPECT_TRUE(model.remove_wire(w.id));
            }
        } else if (op == 3) {
            const auto& nodes = model.current().nodes();
            if (!nodes.empty()) {
                const auto node_id = nodes[choose_index(nodes.size())].semantic.id;

                std::vector<ui::InternedId> incident;
                for (const auto& w : model.current().wires()) {
                    if (w.source.node == node_id) {
                        incident.push_back(w.id);
                        continue;
                    }
                    if (w.target.node == node_id) {
                        incident.push_back(w.id);
                    }
                }
                for (auto wid : incident) {
                    EXPECT_TRUE(model.remove_wire(wid));
                }
                EXPECT_TRUE(model.remove_node(node_id));
            }
        } else if (op == 4) {
            const auto& nodes = model.current().nodes();
            if (!nodes.empty()) {
                const auto node_id = nodes[choose_index(nodes.size())].semantic.id;
                std::uniform_real_distribution<float> pos(-1000.0f, 1000.0f);
                EXPECT_TRUE(model.update_node_position(node_id, pos(rng), pos(rng)));
            }
        } else if (op == 5) {
            if (model.can_undo()) {
                model.undo();
            }
        } else {
            if (model.can_redo()) {
                model.redo();
            }
        }

        validate_now();
    }
}

// =============================================================================
// replace_*_preserve_order free functions
// =============================================================================

TEST(ReplacePreserveOrder, NodeReplacementKeepsInsertionOrder) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    bp2::Blueprint::Node n1;
    n1.semantic.id = interner.intern("a");
    n1.semantic.type = interner.intern("Battery");
    n1.layout.x = 10.0f;

    bp2::Blueprint::Node n2;
    n2.semantic.id = interner.intern("b");
    n2.semantic.type = interner.intern("Resistor");

    bp2::Blueprint::Node n3;
    n3.semantic.id = interner.intern("c");
    n3.semantic.type = interner.intern("Lamp");

    bp = bp.with_node(n1).with_node(n2).with_node(n3);

    // Update n2's position
    bp2::Blueprint::Node updated = n2;
    updated.layout.x = 99.0f;

    bp2::Blueprint result = bp2::replace_node_preserve_order(bp, std::move(updated));

    ASSERT_EQ(result.nodes().size(), 3u);
    EXPECT_EQ(result.nodes()[0].semantic.id, interner.intern("a"));
    EXPECT_EQ(result.nodes()[1].semantic.id, interner.intern("b"));
    EXPECT_EQ(result.nodes()[2].semantic.id, interner.intern("c"));
    EXPECT_FLOAT_EQ(result.nodes()[1].layout.x, 99.0f);
}

TEST(ReplacePreserveOrder, NodeAppendsIfNotFound) {
    ui::StringInterner interner;
    bp2::Blueprint bp;

    bp2::Blueprint::Node n1;
    n1.semantic.id = interner.intern("a");
    n1.semantic.type = interner.intern("Battery");
    bp = bp.with_node(n1);

    bp2::Blueprint::Node new_node;
    new_node.semantic.id = interner.intern("z");
    new_node.semantic.type = interner.intern("Resistor");

    bp2::Blueprint result = bp2::replace_node_preserve_order(bp, std::move(new_node));

    ASSERT_EQ(result.nodes().size(), 2u);
    EXPECT_EQ(result.nodes()[0].semantic.id, interner.intern("a"));
    EXPECT_EQ(result.nodes()[1].semantic.id, interner.intern("z"));
}

TEST(ReplacePreserveOrder, WireReplacementKeepsInsertionOrder) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::Blueprint bp;

    auto make_wire = [&](const char* id) {
        bp2::Blueprint::Wire w;
        w.id = interner.intern(id);
        w.source = bp2::WireEndpoint{interner.intern("n1"), interner.intern("out")};
        w.target = bp2::WireEndpoint{interner.intern("n2"), interner.intern("in")};
        return w;
    };

    bp = bp.with_wire(make_wire("w1")).with_wire(make_wire("w2")).with_wire(make_wire("w3"));

    bp2::Blueprint::Wire updated = make_wire("w2");
    updated.domain = Domain::Mechanical;

    bp2::Blueprint result = bp2::replace_wire_preserve_order(bp, std::move(updated));

    ASSERT_EQ(result.wires().size(), 3u);
    EXPECT_EQ(result.wires()[0].id, interner.intern("w1"));
    EXPECT_EQ(result.wires()[1].id, interner.intern("w2"));
    EXPECT_EQ(result.wires()[2].id, interner.intern("w3"));
    EXPECT_EQ(result.wires()[1].domain, Domain::Mechanical);
}


TEST(ReplacePreserveOrder, ReplacementPreservesMetadata) {
    ui::StringInterner interner;
    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("test_bp"));
    bp = bp.with_name("Test Blueprint");
    bp = bp.with_interface(bp2::Interface({
        {interner.intern("ext_in"), Domain::Electrical, bp2::Direction::Input},
    }));

    bp2::Blueprint::Node n;
    n.semantic.id = interner.intern("n1");
    n.semantic.type = interner.intern("Battery");
    bp = bp.with_node(n);

    bp2::Blueprint result = bp2::replace_node_preserve_order(bp, std::move(n));

    EXPECT_EQ(result.id(), interner.intern("test_bp"));
    EXPECT_EQ(result.name(), "Test Blueprint");
    ASSERT_EQ(result.iface().ports().size(), 1u);
    EXPECT_EQ(result.iface().ports()[0].name, interner.intern("ext_in"));
}

TEST(ReplacePreserveOrder, WireReplacementPreservesMetadata) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("test_bp2"));
    bp = bp.with_name("Test Blueprint 2");
    bp = bp.with_interface(bp2::Interface({
        {interner.intern("ext_out"), Domain::Mechanical, bp2::Direction::Output},
    }));

    bp2::Blueprint::Wire w;
    w.id = interner.intern("w1");
    w.source = bp2::WireEndpoint{interner.intern("n1"), interner.intern("out")};
    w.target = bp2::WireEndpoint{interner.intern("n2"), interner.intern("in")};
    bp = bp.with_wire(w);

    bp2::Blueprint result = bp2::replace_wire_preserve_order(bp, std::move(w));

    EXPECT_EQ(result.id(), interner.intern("test_bp2"));
    EXPECT_EQ(result.name(), "Test Blueprint 2");
    ASSERT_EQ(result.iface().ports().size(), 1u);
    EXPECT_EQ(result.iface().ports()[0].name, interner.intern("ext_out"));
}
