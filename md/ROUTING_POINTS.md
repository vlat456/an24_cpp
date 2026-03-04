# Routing Points - план реализации

## Концепция

Routing points (точки изгиба провода) - это промежуточные точки, через которые проходит провод. Позволяют пользователю создавать провода с изгибами.

### Ключевые операции
1. **Hit test** - определение что курсор близко к routing point
2. **Drag** - перетаскивание routing point
3. **Insert** - двойной клик на провод добавляет новую точку изгиба
4. **Delete** - двойной клик на routing point удаляет его
5. **Render** - рисование точек изгиба и провода через них

---

## TDD: Сначала тесты

### Тест 1: Hit test - попадание в routing point

```cpp
// tests/test_routing.cpp

#include <gtest/gtest.h>
#include "editor/data/blueprint.h"
#include "editor/wires/hittest.h"

TEST(RoutingTest, HitRoutingPoint) {
    Blueprint bp;
    // Создаем 2 узла
    Node n1;
    n1.id = "n1";
    n1.outputs.push_back(Port("out", PortSide::Output));
    bp.nodes.push_back(n1);

    Node n2;
    n2.id = "n2";
    n2.inputs.push_back(Port("in", PortSide::Input));
    bp.nodes.push_back(n2);

    // Создаем провод с routing point
    Wire w;
    w.id = "w1";
    w.start.node_id = "n1";
    w.start.port_name = "out";
    w.end.node_id = "n2";
    w.end.port_name = "in";
    w.routing_points.push_back(Pt(200.0f, 50.0f)); // Добавляем точку изгиба
    bp.wires.push_back(w);

    // Hit test рядом с routing point
    HitResult hit = hit_test_wire(bp, Pt(200.0f, 51.0f));
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->type, HitType::RoutingPoint);
    EXPECT_EQ(hit->wire_index, 0);
    EXPECT_EQ(hit->routing_point_index, 0);
}
```

### Тест 2: Insert routing point - двойной клик добавляет точку

```cpp
// tests/test_routing.cpp

TEST(RoutingTest, InsertRoutingPoint) {
    Blueprint bp;
    // ... создаем схему как выше ...

    // Двойной клик на провод добавляет точку
    Pt click_pos(150.0f, 50.0f); // середина провода
    insert_routing_point(bp, 0, click_pos);

    EXPECT_EQ(bp.wires[0].routing_points.size(), 1);
    // Точка должна быть привязана к сетке
    EXPECT_EQ(bp.wires[0].routing_points[0].x, 144.0f); // snap to grid 16
}
```

### Тест 3: Delete routing point - двойной клик удаляет

```cpp
// tests/test_routing.cpp

TEST(RoutingTest, DeleteRoutingPoint) {
    Blueprint bp;
    // ... создаем схему с одной точкой ...

    // Двойной клик на routing point удаляет её
    remove_routing_point(bp, 0, 0);

    EXPECT_EQ(bp.wires[0].routing_points.size(), 0);
}
```

### Тест 4: Drag routing point

```cpp
// tests/test_routing.cpp

TEST(RoutingTest, DragRoutingPoint) {
    Blueprint bp;
    // ... создаем схему с одной точкой ...

    Pt original = bp.wires[0].routing_points[0];

    // Drag
    Pt delta(10.0f, 5.0f);
    move_routing_point(bp, 0, 0, delta);

    EXPECT_EQ(bp.wires[0].routing_points[0].x, original.x + 10.0f);
    EXPECT_EQ(bp.wires[0].routing_points[0].y, original.y + 5.0f);
}
```

---

## Реализация

### 1. Обновить Wire структуру (уже есть!)

В `data/wire.h`:
```cpp
struct Wire {
    std::string id;
    WireEnd start;
    WireEnd end;
    std::vector<Pt> routing_points;  // Уже есть!
};
```

### 2. Hit test для routing points

В `editor/wires/hittest.h`:
```cpp
#pragma once

#include "data/blueprint.h"
#include "data/pt.h"
#include <optional>

// Результат hit test для wire
struct WireHitResult {
    HitType type;
    size_t wire_index;
    size_t routing_point_index;  // Только для HitType::RoutingPoint
};

// Найти ближайший segment провода (возвращает индекс, куда вставлять)
size_t closest_segment_index(const Blueprint& bp, size_t wire_idx, Pt world_pos);

// Hit test для routing points
std::optional<WireHitResult> hit_test_routing_point(const Blueprint& bp, Pt world_pos);

// Hit test для wire (близость к линии)
std::optional<WireHitResult> hit_test_wire(const Blueprint& bp, Pt world_pos);
```

### 3. Операции с routing points

В `editor/wires/routing.h`:
```cpp
#pragma once

#include "data/blueprint.h"
#include "data/pt.h"

// Вставить routing point в провод (двойной клик)
void insert_routing_point(Blueprint& bp, size_t wire_idx, Pt world_pos, float grid_step = 16.0f);

// Удалить routing point
void remove_routing_point(Blueprint& bp, size_t wire_idx, size_t rp_idx);

// Переместить routing point
void move_routing_point(Blueprint& bp, size_t wire_idx, size_t rp_idx, Pt delta);

// Построить polyline провода с routing points
std::vector<Pt> build_wire_polyline(const Blueprint& bp, const Wire& wire);
```

### 4. Render routing points

В `render.cpp` добавить:
```cpp
// Нарисовать routing points
void draw_routing_points(IDrawList* dl, const Viewport& vp, const Wire& wire, Pt canvas_min);
```

### 5. Обновить app для double-click

В `app.cpp`:
```cpp
// Двойной клик - добавить/удалить routing point
void on_double_click(Pt world_pos) {
    // 1. Сначала проверить hit на routing point - удалить
    // 2. Потом проверить hit на wire - добавить
}
```

---

## Файловая структура

```
src/editor/
├── data/
│   └── wire.h          # Уже есть, routing_points уже там
├── wires/             # НОВОЕ
│   ├── hittest.h
│   ├── hittest.cpp
│   └── routing.h/cpp
├── interact/
│   └── interaction.h  # Обновить Dragging::RoutingPoint
└── render/
    └── render.cpp     # Обновить - рисовать routing points
```

---

## Notes

1. **Polyline**: Провод рисуется как polyline: start_port → [routing_points...] → end_port
2. **Grid snap**: Routing points привязываются к сетке (как в Rust `pos.snap(grid_step)`)
3. **Radius**: Routing point hit test использует радиус (например, 8 пикселей)
4. **Двойной клик**: Используем ImGui `IsMouseDoubleClicked()`
