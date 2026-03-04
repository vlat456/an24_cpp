#include <gtest/gtest.h>
#include "editor/persist.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"
#include "editor/data/wire.h"

/// TDD Step 2: Persist - сначала тесты

TEST(PersistTest, ToJson_EmptyBlueprint) {
    Blueprint bp;
    std::string json = blueprint_to_json(bp);
    EXPECT_FALSE(json.empty());
    // Должен содержать пустой массив nodes
    EXPECT_NE(json.find("nodes"), std::string::npos);
}

TEST(PersistTest, ToJson_FromJson_Roundtrip) {
    Blueprint bp;

    // Добавим узел
    Node n;
    n.id = "batt1";
    n.name = "Battery 28V";
    n.type_name = "Battery";
    n.at(100.0f, 200.0f);
    n.input("v_in");
    n.output("v_out");
    bp.add_node(std::move(n));

    // Добавим провод
    Wire w;
    w.id = "wire1";
    w.start.node_id = "batt1";
    w.start.port_name = "v_out";
    w.end.node_id = "load1";
    w.end.port_name = "v_in";
    bp.add_wire(std::move(w));

    // Roundtrip: to JSON -> from JSON
    std::string json = blueprint_to_json(bp);
    auto bp2 = blueprint_from_json(json);

    ASSERT_TRUE(bp2.has_value());
    EXPECT_EQ(bp2->nodes.size(), 1);
    EXPECT_EQ(bp2->nodes[0].id, "batt1");
    EXPECT_EQ(bp2->nodes[0].pos.x, 100.0f);
    EXPECT_EQ(bp2->nodes[0].pos.y, 200.0f);
    EXPECT_EQ(bp2->nodes[0].inputs.size(), 1);
    EXPECT_EQ(bp2->wires.size(), 1);
    EXPECT_EQ(bp2->wires[0].start.node_id, "batt1");
}

TEST(PersistTest, FromJson_Invalid_ReturnsNullopt) {
    auto bp = blueprint_from_json("invalid json {");
    EXPECT_FALSE(bp.has_value());
}

TEST(PersistTest, ToJson_IncludesViewport) {
    Blueprint bp;
    bp.pan = Pt(50.0f, 75.0f);
    bp.zoom = 1.5f;
    bp.grid_step = 24.0f;

    std::string json = blueprint_to_json(bp);
    auto bp2 = blueprint_from_json(json);

    ASSERT_TRUE(bp2.has_value());
    EXPECT_EQ(bp2->pan.x, 50.0f);
    EXPECT_EQ(bp2->zoom, 1.5f);
    EXPECT_EQ(bp2->grid_step, 24.0f);
}
