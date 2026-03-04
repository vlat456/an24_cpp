#include <gtest/gtest.h>
#include "editor/render.h"
#include "editor/data/blueprint.h"
#include "editor/data/node.h"
#include "editor/data/wire.h"
#include "editor/viewport.h"

/// TDD Step 5: Rendering

/// Тест: пустой Blueprint не крашится
TEST(RenderTest, EmptyBlueprint_DoesNotCrash) {
    Blueprint bp;
    MockDrawList dl;
    Viewport vp;
    render_blueprint(bp, &dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f));
    // Ожидаем только вызовы для сетки (не для nodes/wires)
}

/// Тест: рендер узла
TEST(RenderTest, Node_RendersRect) {
    Blueprint bp;
    Node n;
    n.id = "batt1";
    n.name = "Battery";
    n.type_name = "Battery";
    n.at(100.0f, 50.0f);
    n.size_wh(120.0f, 80.0f);
    bp.add_node(std::move(n));

    MockDrawList dl;
    Viewport vp;
    render_blueprint(bp, &dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f));

    // Проверяем что rect был добавлен
    EXPECT_TRUE(dl.had_rect());
}

/// Тест: рендер провода
TEST(RenderTest, Wire_RendersLine) {
    Blueprint bp;

    Node n1;
    n1.id = "n1";
    n1.at(0.0f, 0.0f);
    n1.size_wh(100.0f, 50.0f);
    bp.add_node(std::move(n1));

    Node n2;
    n2.id = "n2";
    n2.at(200.0f, 0.0f);
    n2.size_wh(100.0f, 50.0f);
    bp.add_node(std::move(n2));

    Wire w;
    w.id = "w1";
    w.start.node_id = "n1";
    w.start.port_name = "out";
    w.end.node_id = "n2";
    w.end.port_name = "in";
    bp.add_wire(std::move(w));

    MockDrawList dl;
    Viewport vp;
    render_blueprint(bp, &dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f));

    // Проверяем что линия была добавлена
    EXPECT_TRUE(dl.had_line());
}

/// Тест: сетка не крашится
TEST(RenderTest, Grid_DoesNotCrash) {
    MockDrawList dl;
    Viewport vp;
    vp.grid_step = 16.0f;
    vp.zoom = 1.0f;

    // Просто проверяем что не крашится
    render_grid(&dl, vp, Pt(0.0f, 0.0f), Pt(800.0f, 600.0f));
}
