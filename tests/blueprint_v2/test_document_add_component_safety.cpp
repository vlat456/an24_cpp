#include <gtest/gtest.h>

#include "editor/document.h"
#include "json_parser/json_parser.h"

TEST(DocumentSafety, AddComponentUnknownTypeDoesNotCrashOrMutate) {
    Document doc;
    TypeRegistry registry = load_type_registry("library/");

    const size_t before_nodes = doc.model().current().nodes().size();
    const size_t before_wires = doc.model().current().wires().size();

    EXPECT_NO_THROW(doc.addComponent("DefinitelyUnknownComponent", Pt{64.0f, 64.0f}, "", registry));

    EXPECT_EQ(doc.model().current().nodes().size(), before_nodes);
    EXPECT_EQ(doc.model().current().wires().size(), before_wires);
}

TEST(DocumentSafety, AddComponentBridgeInGroupCreatesSingleUndoStep) {
    Document doc;
    TypeRegistry registry = load_type_registry("library/");
    doc.setTypeRegistry(&registry);

    bp2::Blueprint::Node composite;
    composite.semantic.id = doc.interner().intern("comp_1");
    composite.semantic.type = doc.interner().intern("CompositeType");
    composite.view.expandable = true;
    composite.view.name = "comp_1";

    bp2::Blueprint inner;
    auto nested = bp2::Blueprint::Nested::make_embedded(
        doc.interner().intern("comp_1"), doc.interner().intern("CompositeType"),
        std::make_unique<bp2::Blueprint>(inner));

    doc.model().replace_current(
        doc.model().current().with_node(std::move(composite)).with_nested(std::move(nested)));

    const size_t undo_before = doc.model().undo_depth();
    doc.addComponent("BlueprintInput", Pt{64.0f, 64.0f}, "comp_1", registry);

    ASSERT_EQ(doc.model().undo_depth(), undo_before + 1);
    ASSERT_TRUE(doc.performUndo());
    EXPECT_EQ(doc.model().current().find_node(doc.interner().intern("comp_1:BlueprintInput_1")), nullptr);
    ASSERT_NE(doc.model().current().find_nested(doc.interner().intern("comp_1")), nullptr);
    EXPECT_TRUE(doc.model().current().find_nested(doc.interner().intern("comp_1"))->inline_def()->iface().empty());
}

TEST(DocumentSafety, AddBlueprintCreatesSingleUndoStep) {
    Document doc;
    TypeRegistry registry = load_type_registry("library/");
    doc.setTypeRegistry(&registry);

    const size_t undo_before = doc.model().undo_depth();
    doc.addBlueprint("GroundPower", Pt{64.0f, 64.0f}, "", registry);

    ASSERT_EQ(doc.model().undo_depth(), undo_before + 1);
    ASSERT_TRUE(doc.performUndo());
    EXPECT_TRUE(doc.model().current().nodes().empty());
    EXPECT_TRUE(doc.model().current().nested().empty());
}
