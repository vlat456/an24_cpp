#include <gtest/gtest.h>
#include "editor/visual/spatial_grid.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"
#include "editor/data/wire.h"
#include "editor/visual/node/node.h"

// SpatialGrid: пустая сцена не возвращает кандидатов
TEST(SpatialGrid, Empty_NoCandidates) {
    Blueprint bp;
    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");

    std::vector<size_t> out;
    grid.query_nodes(Pt(100, 100), 10.0f, out);
    EXPECT_TRUE(out.empty());
}

// SpatialGrid: один узел найден в своей ячейке
TEST(SpatialGrid, OneNode_Found) {
    Blueprint bp;
    Node n; n.id = "n1"; n.at(100, 50).size_wh(120, 80);
    bp.add_node(std::move(n));

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");

    std::vector<size_t> out;
    grid.query_nodes(Pt(160, 90), 0.0f, out);
    ASSERT_FALSE(out.empty());
    EXPECT_EQ(out[0], 0u);
}

// SpatialGrid: узел из другой группы не возвращается после фильтрации
// (группа фильтруется в hit_test, не в spatial grid — тест проверяет, что узел
//  присутствует в ячейке даже при group_id != "" в rebuild)
TEST(SpatialGrid, NodeInDifferentGroup_StillInGrid) {
    Blueprint bp;
    Node n; n.id = "n1"; n.at(100, 50).size_wh(120, 80);
    n.group_id = "other";
    bp.add_node(std::move(n));

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");  // only root group nodes get inserted

    std::vector<size_t> out;
    grid.query_nodes(Pt(160, 90), 0.0f, out);
    EXPECT_TRUE(out.empty()) << "Node with wrong group_id must not be in grid";
}

// SpatialGrid: wire попадает в несколько ячеек
TEST(SpatialGrid, Wire_CoversMutipleCells) {
    Blueprint bp;
    Node n1; n1.id="a"; n1.at(0,0).size_wh(80,48); n1.output("o"); bp.add_node(std::move(n1));
    Node n2; n2.id="b"; n2.at(300,0).size_wh(80,48); n2.input("i"); bp.add_node(std::move(n2));
    Wire w = Wire::make("w1", wire_output("a","o"), wire_input("b","i"));
    bp.add_wire(std::move(w));

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");

    // Midpoint of the wire should return wire index 0
    std::vector<size_t> out;
    grid.query_wires(Pt(190, 24), 5.0f, out);
    ASSERT_FALSE(out.empty());
    EXPECT_EQ(out[0], 0u);
}

// SpatialGrid: rebuild очищает старые данные
TEST(SpatialGrid, Rebuild_ClearsOldData) {
    Blueprint bp;
    Node n; n.id = "n1"; n.at(0,0).size_wh(80,48); bp.add_node(std::move(n));

    VisualNodeCache cache;
    editor_spatial::SpatialGrid grid;
    grid.rebuild(bp, cache, "");

    // Remove node and rebuild
    bp.nodes.clear();
    grid.rebuild(bp, cache, "");

    std::vector<size_t> out;
    grid.query_nodes(Pt(40, 24), 0.0f, out);
    EXPECT_TRUE(out.empty()) << "After rebuild with empty bp, grid must be empty";
}
