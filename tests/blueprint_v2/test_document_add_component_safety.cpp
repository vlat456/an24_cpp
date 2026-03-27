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
