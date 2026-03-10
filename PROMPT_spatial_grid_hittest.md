# Spatial Grid Hit Test — Инструкция по замене

## Цель

Заменить перебор всех объектов сцены при mouse hover на **spatial hash grid** — структуру,
которая отвечает "какие объекты есть рядом с точкой" за O(1) в среднем, без итерации по всему массиву.

Сцена редактора содержит три вида объектов:

- **Узлы (Node)** — прямоугольники ~80–200px, фиксированный размер
- **Порты (Port)** — точки на краях узлов, radius hit = 10 world units
- **Wire-сегменты** — отрезки, которые могут пересекать много ячеек сетки; их может быть много
  через один квадрат сетки

---

## Текущий код — где всё происходит

### Главный hit-test

**`src/editor/visual/hittest.cpp`** — функция `hit_test(...)`:

```
for each node  → visual->containsPoint(world_pos)           // O(N_nodes)
for each wire  → for each routing_point → dist check        // O(N_wires × N_rp)
for each wire  → for each segment → dist_to_segment check   // O(N_wires × N_segments)
```

**`src/editor/visual/hittest.cpp`** — функция `hit_test_ports(...)`:

```
for each node → for each port → dist check                  // O(N_nodes × N_ports)
```

Оба вызываются из `VisualScene::hitTest()` и `VisualScene::hitTestPorts()` в
**`src/editor/visual/scene/scene.h`** (строки 63–70).

### Зависимости и типы данных

- `Pt` — `struct { float x, y; }` (`src/editor/data/pt.h`)
- `Node` — `struct { Pt pos, size; std::string id, group_id; ... }` (`src/editor/data/node.h`)
- `Wire` — `struct { std::string id; WireEnd start, end; std::vector<Pt> routing_points; }`
- `VisualNode` — имеет `containsPoint(Pt)`, `getPosition()`, `getSize()`, `getPortCount()`,
  `getPort(i)` → `VisualPort*` с `worldPosition()`
- `VisualNodeCache` — `get(id)` и `getOrCreate(node, wires)`
- `editor_constants::PORT_HIT_RADIUS = 10.0f` — радиус клика по порту
- `editor_constants::WIRE_SEGMENT_HIT_TOLERANCE = 5.0f` — ширина зоны клика по проводу
- `editor_constants::DEFAULT_GRID_STEP = 16.0f` — опорный размер сетки

---

## Архитектура — что нужно создать

### Ячейка сетки spatial hash

Выбрать шаг сетки `SPATIAL_CELL = 64.0f` (4 × DEFAULT_GRID_STEP).
Это больше, чем PORT_HIT_RADIUS (10), поэтому при запросе точки нужно проверять 3×3 ячейки
(т.к. хит-радиус < размера ячейки / 2).

```
cell_x = (int)floor(world_x / SPATIAL_CELL)
cell_y = (int)floor(world_y / SPATIAL_CELL)
```

Ключ — пара `(cell_x, cell_y)`, хранится в `std::unordered_map` с
`std::pair<int,int>` ключом (или кодировать `int64_t` = `cell_x * 1000003LL + cell_y`).

### Структуры — что хранить в каждой ячейке

```cpp
struct SpatialCell {
    std::vector<size_t> node_indices;   // индексы в bp.nodes
    std::vector<size_t> wire_indices;   // индексы в bp.wires (для сегментов и rp)
};
```

**Узел** занимает несколько ячеек (охватывает bbox узла).
**Провод** покрывает все ячейки, через которые проходит каждый его сегмент (rasterize segment).
**Порт** — не хранится отдельно; при хите мы сначала находим ячейки узлов, затем проверяем порты
только найденных узлов.

### Класс `SpatialGrid`

Создать новый файл: **`src/editor/visual/spatial_grid.h`** (header-only).

```cpp
#pragma once
#include "data/pt.h"
#include "data/blueprint.h"
#include "visual/node/node.h"
#include "layout_constants.h"
#include <unordered_map>
#include <vector>
#include <cmath>

namespace editor_spatial {

constexpr float CELL_SIZE = 64.0f;  // Must be > PORT_HIT_RADIUS * 2

inline int cell_coord(float world) {
    return (int)std::floor(world / CELL_SIZE);
}

// Compact key: encode (cx, cy) into a single int64
inline int64_t cell_key(int cx, int cy) {
    return ((int64_t)(uint32_t)cx << 32) | (uint32_t)cy;
}

struct SpatialCell {
    std::vector<size_t> node_indices;
    std::vector<size_t> wire_indices;  // wires with segments OR routing_points here
};

class SpatialGrid {
public:
    void clear() { cells_.clear(); }

    // Build the grid from scratch. Call when blueprint changes structurally
    // (node added/removed, wire added/removed, node moved).
    void rebuild(const Blueprint& bp, VisualNodeCache& cache,
                 const std::string& group_id) {
        clear();

        // Register nodes
        for (size_t i = 0; i < bp.nodes.size(); i++) {
            const Node& n = bp.nodes[i];
            if (n.group_id != group_id) continue;
            insert_node(i, n.pos, Pt(n.pos.x + n.size.x, n.pos.y + n.size.y));
        }

        // Register wires (segments + routing points)
        for (size_t i = 0; i < bp.wires.size(); i++) {
            const Wire& w = bp.wires[i];
            const Node* sn = bp.find_node(w.start.node_id.c_str());
            const Node* en = bp.find_node(w.end.node_id.c_str());
            if (!sn || !en) continue;
            if (sn->group_id != group_id || en->group_id != group_id) continue;

            // Get resolved endpoints from cache (handles Bus alias ports)
            Pt start_pos = editor_math::get_port_position(
                *sn, w.start.port_name.c_str(), bp.wires, w.id.c_str(), cache);
            Pt end_pos = editor_math::get_port_position(
                *en, w.end.port_name.c_str(), bp.wires, w.id.c_str(), cache);

            // Rasterize all segments (start→rp[0]→rp[1]→...→end)
            Pt prev = start_pos;
            for (const Pt& rp : w.routing_points) {
                insert_segment(i, prev, rp);
                prev = rp;
            }
            insert_segment(i, prev, end_pos);
        }
    }

    // Query: return candidate node indices covering the given point (+tolerance margin).
    // Returns set of unique node indices from 3×3 neighborhood (for port hit radius).
    void query_nodes(Pt world_pos, float margin,
                     std::vector<size_t>& out_nodes) const {
        int cx0 = cell_coord(world_pos.x - margin);
        int cy0 = cell_coord(world_pos.y - margin);
        int cx1 = cell_coord(world_pos.x + margin);
        int cy1 = cell_coord(world_pos.y + margin);
        for (int cx = cx0; cx <= cx1; cx++) {
            for (int cy = cy0; cy <= cy1; cy++) {
                auto it = cells_.find(cell_key(cx, cy));
                if (it == cells_.end()) continue;
                for (size_t idx : it->second.node_indices) {
                    // Deduplicate: only append if not already in out_nodes
                    bool found = false;
                    for (size_t x : out_nodes) { if (x == idx) { found = true; break; } }
                    if (!found) out_nodes.push_back(idx);
                }
            }
        }
    }

    // Query: return candidate wire indices for the given point (+tolerance margin).
    void query_wires(Pt world_pos, float margin,
                     std::vector<size_t>& out_wires) const {
        int cx0 = cell_coord(world_pos.x - margin);
        int cy0 = cell_coord(world_pos.y - margin);
        int cx1 = cell_coord(world_pos.x + margin);
        int cy1 = cell_coord(world_pos.y + margin);
        for (int cx = cx0; cx <= cx1; cx++) {
            for (int cy = cy0; cy <= cy1; cy++) {
                auto it = cells_.find(cell_key(cx, cy));
                if (it == cells_.end()) continue;
                for (size_t idx : it->second.wire_indices) {
                    bool found = false;
                    for (size_t x : out_wires) { if (x == idx) { found = true; break; } }
                    if (!found) out_wires.push_back(idx);
                }
            }
        }
    }

private:
    std::unordered_map<int64_t, SpatialCell> cells_;

    SpatialCell& get_or_create(int cx, int cy) {
        return cells_[cell_key(cx, cy)];
    }

    // Insert node bounding box into all covered cells (expanded by PORT_HIT_RADIUS
    // so that port queries from neighboring cells still find it).
    void insert_node(size_t idx, Pt world_min, Pt world_max) {
        float margin = editor_constants::PORT_HIT_RADIUS;
        int cx0 = cell_coord(world_min.x - margin);
        int cy0 = cell_coord(world_min.y - margin);
        int cx1 = cell_coord(world_max.x + margin);
        int cy1 = cell_coord(world_max.y + margin);
        for (int cx = cx0; cx <= cx1; cx++)
            for (int cy = cy0; cy <= cy1; cy++)
                get_or_create(cx, cy).node_indices.push_back(idx);
    }

    // Rasterize one wire segment into all covered cells (expanded by hit tolerance).
    void insert_segment(size_t wire_idx, Pt a, Pt b) {
        float tol = editor_constants::WIRE_SEGMENT_HIT_TOLERANCE;
        float min_x = std::min(a.x, b.x) - tol;
        float max_x = std::max(a.x, b.x) + tol;
        float min_y = std::min(a.y, b.y) - tol;
        float max_y = std::max(a.y, b.y) + tol;

        int cx0 = cell_coord(min_x), cy0 = cell_coord(min_y);
        int cx1 = cell_coord(max_x), cy1 = cell_coord(max_y);
        for (int cx = cx0; cx <= cx1; cx++) {
            for (int cy = cy0; cy <= cy1; cy++) {
                auto& cell = get_or_create(cx, cy);
                bool found = false;
                for (size_t x : cell.wire_indices) { if (x == wire_idx) { found = true; break; } }
                if (!found) cell.wire_indices.push_back(wire_idx);
            }
        }
    }
};

} // namespace editor_spatial
```

---

## Изменения в существующих файлах

Старый код **полностью заменяется** — никакой поддержки старых сигнатур не нужно.

### 1. `src/editor/visual/scene/scene.h`

Добавить `SpatialGrid` как поле `VisualScene`:

```cpp
// Добавить include в начало файла:
#include "visual/spatial_grid.h"

// Добавить приватное поле в class VisualScene:
private:
    editor_spatial::SpatialGrid spatial_grid_;
    bool spatial_grid_dirty_ = true;  // needs rebuild
```

Добавить метод-инвалидатор и rebuild-хелпер рядом с существующими hit-test методами:

```cpp
// Call after any structural mutation: node add/remove/move, wire add/remove.
void invalidateSpatialGrid() { spatial_grid_dirty_ = true; }

// Rebuild only when dirty. Call at start of hitTest/hitTestPorts.
void ensureSpatialGrid() {
    if (!spatial_grid_dirty_) return;
    spatial_grid_.rebuild(*bp_, cache_, group_id_);
    spatial_grid_dirty_ = false;
}
```

Изменить существующие методы:

```cpp
HitResult hitTest(Pt world_pos) {
    ensureSpatialGrid();
    return hit_test(*bp_, cache_, world_pos, vp_, group_id_, spatial_grid_);
}

HitResult hitTestPorts(Pt world_pos) {
    ensureSpatialGrid();
    return hit_test_ports(*bp_, cache_, world_pos, group_id_, spatial_grid_);
}
```

Добавить `invalidateSpatialGrid()` в каждый метод-мутатор сцены:

- `addNode(...)` — после добавления
- `removeNodes(...)` — после удаления
- `moveNode(...)` — после перемещения
- `addWire(...)` — после добавления
- `removeWire(...)` — при наличии
- `swapWirePortsOnBus(...)` — после свапа (меняет визуальные позиции)
- `loadBlueprint(...)` / `setBlueprint(...)` — если есть

Найди эти методы в `scene.h` (строки ~210–280) и добавь вызов `invalidateSpatialGrid()` в каждый.

---

### 2. `src/editor/visual/hittest.h`

**Полностью заменить** содержимое файла. Убрать старые сигнатуры без `grid`, оставить только
версии с обязательным параметром `const editor_spatial::SpatialGrid&`:

```cpp
#pragma once

#include "data/blueprint.h"
#include "viewport/viewport.h"
#include "data/pt.h"
#include "visual/spatial_grid.h"

class VisualNodeCache;

HitResult hit_test(const Blueprint& bp, VisualNodeCache& cache, Pt world_pos,
                   const Viewport& vp, const std::string& group_id,
                   const editor_spatial::SpatialGrid& grid);

HitResult hit_test_ports(const Blueprint& bp, VisualNodeCache& cache, Pt world_pos,
                          const std::string& group_id,
                          const editor_spatial::SpatialGrid& grid);
```

(Сохрани все имеющиеся includes и forward declarations, нужные для типов полей `HitResult`,
— не удаляй их. Добавь только `#include "visual/spatial_grid.h"` и убери старые сигнатуры.)

---

### 3. `src/editor/visual/hittest.cpp`

**Полностью заменить** обе функции. Старые O(N) тела удалить; оставить только fast-path версии.
Добавить include нового заголовка:

```cpp
#include "visual/spatial_grid.h"
```

#### Функция `hit_test`:

```cpp
HitResult hit_test(const Blueprint& bp, VisualNodeCache& cache, Pt world_pos,
                   const Viewport& vp, const std::string& group_id,
                   const editor_spatial::SpatialGrid& grid) {
    HitResult result;
    (void)vp;

    // --- Узлы ---
    {
        std::vector<size_t> candidates;
        candidates.reserve(8);
        grid.query_nodes(world_pos, 0.0f, candidates);
        for (size_t i : candidates) {
            if (i >= bp.nodes.size()) continue;
            const auto& n = bp.nodes[i];
            if (n.group_id != group_id) continue;
            auto* visual = cache.getOrCreate(n, bp.wires);
            if (visual->containsPoint(world_pos)) {
                result.type = HitType::Node;
                result.node_index = i;
                return result;
            }
        }
    }

    // --- Routing points и сегменты проводов ---
    {
        std::vector<size_t> candidates;
        candidates.reserve(8);
        float margin = std::max(editor_constants::ROUTING_POINT_HIT_RADIUS,
                                editor_constants::WIRE_SEGMENT_HIT_TOLERANCE);
        grid.query_wires(world_pos, margin, candidates);

        // Routing points — приоритет выше сегментов
        for (size_t wire_idx : candidates) {
            if (wire_idx >= bp.wires.size()) continue;
            const auto& w = bp.wires[wire_idx];
            const Node* sn = bp.find_node(w.start.node_id.c_str());
            const Node* en = bp.find_node(w.end.node_id.c_str());
            if (!sn || !en || sn->group_id != group_id || en->group_id != group_id) continue;
            for (size_t rp_idx = 0; rp_idx < w.routing_points.size(); rp_idx++) {
                if (editor_math::distance(world_pos, w.routing_points[rp_idx])
                        <= editor_constants::ROUTING_POINT_HIT_RADIUS) {
                    result.type = HitType::RoutingPoint;
                    result.wire_index = wire_idx;
                    result.routing_point_index = rp_idx;
                    return result;
                }
            }
        }

        // Сегменты
        for (size_t wire_idx : candidates) {
            if (wire_idx >= bp.wires.size()) continue;
            const auto& w = bp.wires[wire_idx];
            const Node* sn = bp.find_node(w.start.node_id.c_str());
            const Node* en = bp.find_node(w.end.node_id.c_str());
            if (!sn || !en || sn->group_id != group_id || en->group_id != group_id) continue;

            Pt start_pos = editor_math::get_port_position(
                *sn, w.start.port_name.c_str(), bp.wires, w.id.c_str(), cache);
            Pt end_pos = editor_math::get_port_position(
                *en, w.end.port_name.c_str(), bp.wires, w.id.c_str(), cache);

            Pt prev = start_pos;
            bool hit_found = false;
            for (const auto& rp : w.routing_points) {
                if (editor_math::distance_to_segment(world_pos, prev, rp)
                        < editor_constants::WIRE_SEGMENT_HIT_TOLERANCE) {
                    hit_found = true; break;
                }
                prev = rp;
            }
            if (!hit_found && editor_math::distance_to_segment(world_pos, prev, end_pos)
                    < editor_constants::WIRE_SEGMENT_HIT_TOLERANCE)
                hit_found = true;

            if (hit_found) {
                result.type = HitType::Wire;
                result.wire_index = wire_idx;
                return result;
            }
        }
    }

    return result;
}
```

#### Функция `hit_test_ports`:

```cpp
HitResult hit_test_ports(const Blueprint& bp, VisualNodeCache& cache, Pt world_pos,
                          const std::string& group_id,
                          const editor_spatial::SpatialGrid& grid) {
    HitResult result;
    const float PORT_HIT_RADIUS = editor_constants::PORT_HIT_RADIUS;

    std::vector<size_t> candidates;
    candidates.reserve(8);
    grid.query_nodes(world_pos, PORT_HIT_RADIUS, candidates);

    for (size_t node_idx : candidates) {
        if (node_idx >= bp.nodes.size()) continue;
        const auto& node = bp.nodes[node_idx];
        if (node.group_id != group_id) continue;

        auto* visual = cache.getOrCreate(node, bp.wires);
        if (!visual) continue;

        for (size_t port_idx = 0; port_idx < visual->getPortCount(); port_idx++) {
            const auto* port = visual->getPort(port_idx);
            if (!port) break;
            Pt port_pos = port->worldPosition();
            if (editor_math::distance(world_pos, port_pos) <= PORT_HIT_RADIUS) {
                result.type = HitType::Port;
                result.node_index = node_idx;
                result.port_index = port_idx;
                result.port_node_id = node.id;
                result.port_name = port->logicalName();
                result.port_position = port_pos;
                result.port_side = port->side();
                result.port_wire_id = port->wireId();
                return result;
            }
        }
    }
    return result;
}
```

**Проверь** в оригинальном `hittest.cpp` (строки ~80–120) точные имена методов
`port->logicalName()`, `port->side()`, `port->wireId()` — и используй те же.

---

## Вызов `invalidateSpatialGrid` при редактировании

В `canvas_input.cpp` при drag ноды вызывается `vis->setPosition(new_pos)` каждый кадр.
Чтобы spatial grid не перестраивался каждый frame при drag:

- `invalidateSpatialGrid()` вызывать только при **отпускании** кнопки мыши (в `on_mouse_up`),
  а не при каждом `on_mouse_drag`.
- Пока нода в drag — hit test всё равно точный, т.к. при drag мы кликнули уже и курсор
  ведёт ноду, hover на других объектах в этот момент не нужен.

Для этого в `CanvasInput::on_mouse_up` (после `leave_state()`) добавить:

```cpp
scene_.invalidateSpatialGrid();
```

И в `CanvasInput::on_mouse_down` **не** вызывать invalidate.

---

## Добавить в тесты: `tests/test_spatial_grid.cpp`

Создать файл и добавить его в `tests/CMakeLists.txt` (в список source файлов для
`editor_widget_tests` или отдельный target).

```cpp
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
```

---

## Добавить test target в CMakeLists.txt

Найди в **`tests/CMakeLists.txt`** строку, где перечислены source файлы для
`editor_hittest_tests` или `editor_widget_tests` (или создай отдельный target):

```cmake
add_executable(spatial_grid_tests
    test_spatial_grid.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/wire.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/layout.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/trigonometry.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
)
target_include_directories(spatial_grid_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/editor)
target_link_libraries(spatial_grid_tests gtest_main spdlog json_parser)
add_test(NAME SpatialGridTests COMMAND spatial_grid_tests)
```

Либо, проще — добавь `test_spatial_grid.cpp` в уже существующий `editor_widget_tests` список source файлов.
Смотри строки в `tests/CMakeLists.txt` где перечислен список `test_widget.cpp`.

---

## Порядок выполнения

1. Создать `src/editor/visual/spatial_grid.h` с классом `SpatialGrid` (см. выше).
2. Создать `tests/test_spatial_grid.cpp` с 5 тестами (см. выше).
3. Добавить `test_spatial_grid.cpp` в `tests/CMakeLists.txt`.
4. **Заменить** `src/editor/visual/hittest.h` — новые сигнатуры с `SpatialGrid&`.
5. **Заменить** тела обеих функций в `src/editor/visual/hittest.cpp` — O(1) fast path.
6. Обновить все вызывающие места в тестах (`test_hittest.cpp` и др.) — добавить grid параметр (см. секцию "Обновление тестов" ниже).
7. Добавить поля и методы в `src/editor/visual/scene/scene.h`.
8. Добавить `invalidateSpatialGrid()` во все мутаторы сцены.
9. Добавить `scene_.invalidateSpatialGrid()` в `canvas_input.cpp::on_mouse_up`.
10. Скомпилировать и запустить полный набор тестов: `make -j8 && ctest --output-on-failure`.
    Ожидается: 880+ тестов, 1 заранее известный провал (`RouterTest.RouteWithPortDeparture`).

---

## Обновление тестов

Старые тесты в `test_hittest.cpp`, `test_routing.cpp`, `test_visual_scene.cpp` вызывают
`hit_test(bp, cache, pos, vp)` без `grid`. После замены сигнатур они перестанут компилироваться.
**Обнови каждый такой вызов**: создай `SpatialGrid` и сделай `rebuild` перед вызовом:

```cpp
// В каждом тесте, где вызывается hit_test / hit_test_ports:
VisualNodeCache cache;
editor_spatial::SpatialGrid grid;
grid.rebuild(bp, cache, "");  // или нужный group_id

// Вместо: hit_test(bp, cache, pos, vp, "")
HitResult r = hit_test(bp, cache, pos, vp, "", grid);

// Вместо: hit_test_ports(bp, cache, pos, "")
HitResult r = hit_test_ports(bp, cache, pos, "", grid);
```

Для тестов с `group_id != ""` подставь соответствующий id в `grid.rebuild(...)`.

Тесты через `VisualScene::hitTest()` / `hitTestPorts()` менять **не нужно** — `scene.h`
уже содержит grid внутри и перестроит его автоматически.

---

## Что НЕ менять

- Не менять `data/blueprint.h`, `data/node.h`, `data/wire.h`, `data/pt.h`.
- Не менять ничего кроме файлов, перечисленных в этой инструкции.

---

## Ожидаемый результат по производительности

| Сцена                     | До (O)    | После (O)          |
| ------------------------- | --------- | ------------------ |
| 100 узлов, 150 проводов   | O(250)    | O(1–8 кандидатов)  |
| 1000 узлов, 2000 проводов | O(3000)   | O(1–12 кандидатов) |
| каждый кадр, 60 fps       | 60 × O(N) | 60 × O(1)          |

Mouse hover вызывает hit_test примерно 60 раз в секунду. При большой схеме это ощутимо.
