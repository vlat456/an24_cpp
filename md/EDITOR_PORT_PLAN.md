# План портирования Editor: Rust/egui → C++/imgui

## Подход: TDD (Red → Green)

Пишем сначала **failing tests**, потом код чтобы они прошли.

---

## Step 1: Базовые структуры данных (data layer)

### Цель
Создать минимальные структуры данных для хранения схемы (blueprint).

### Файлы
```
src/editor/
├── data/
│   ├── pt.h           # Pt (Point2D)
│   ├── port.h         # Port, PortSide
│   ├── node.h         # Node
│   ├── wire.h         # Wire, WireEnd
│   ├── blueprint.h    # Blueprint (вместо Circuit - все домены!)
│   └── CMakeLists.txt
```

### TDD: Сначала тесты

```cpp
// tests/test_data.cpp

#include <gtest/gtest.h>
#include "editor/data/pt.h"
#include "editor/data/blueprint.h"

TEST(BlueprintTest, DefaultIsEmpty) {
    Blueprint bp;
    EXPECT_TRUE(bp.nodes.empty());
    EXPECT_TRUE(bp.wires.empty());
}

TEST(BlueprintTest, AddNode) {
    Blueprint bp;
    bp.add_node(Node::new("n1", "Battery", "Battery").at(10.0f, 20.0f));
    EXPECT_EQ(bp.nodes.size(), 1);
}

TEST(PtTest, Constructor) {
    Pt p(10.0f, 20.0f);
    EXPECT_EQ(p.x, 10.0f);
    EXPECT_EQ(p.y, 20.0f);
}

// DEBUG: проверка что Pt работает - только в Debug!
#ifdef DEBUG
    #include <spdlog/spdlog.h>
    #define DEBUG_PRINT_PT(p, msg) spdlog::debug("Pt: {} at ({:.1f}, {:.1f})", msg, p.x, p.y)
#else
    #define DEBUG_PRINT_PT(p, msg) ((void)0)
#endif
```

### Реализация

```cpp
// editor/data/pt.h
#pragma once

#include <cmath>

struct Pt {
    float x, y;

    Pt() : x(0.0f), y(0.0f) {}
    Pt(float x_, float y_) : x(x_), y(y_) {}

    static Pt zero() { return Pt(0.0f, 0.0f); }

    Pt operator+(const Pt& o) const { return Pt(x + o.x, y + o.y); }
    Pt operator-(const Pt& o) const { return Pt(x - o.x, y - o.y); }
    Pt operator*(float s) const { return Pt(x * s, y * s); }

    bool operator==(const Pt& o) const {
        return std::abs(x - o.x) < 1e-6f && std::abs(y - o.y) < 1e-6f;
    }
};
```

---

## Step 2: Сериализация (persist)

### Цель
JSON сохранение/загрузка схемы.

### TDD

```cpp
// tests/test_persist.cpp

#include <gtest/gtest.h>
#include "editor/data/circuit.h"

TEST(PersistTest, Roundtrip) {
    Circuit c;
    c.nodes.push_back(Node::new("n1", "Battery", "Battery").at(10.0f, 20.0f));

    // Сериализация
    std::string json = to_json(c);
    EXPECT_FALSE(json.empty());

    // Десериализация
    auto c2 = from_json(json);
    ASSERT_TRUE(c2.has_value());
    EXPECT_EQ(c.nodes.size(), c2->nodes.size());
}
```

### Реализация

```cpp
// editor/persist.h

#pragma once

#include "editor/data/circuit.h"
#include <string>
#include <optional>

// Сериализация в JSON строку
std::string circuit_to_json(const Circuit& c);

// Десеализация из JSON строки
std::optional<Circuit> circuit_from_json(const std::string& json);

// Сохранение в файл (для будущего GUI)
bool save_circuit_to_file(const Circuit& c, const char* path);

// Загрузка из файла
std::optional<Circuit> load_circuit_from_file(const char* path);
```

### Note
Для JSON использовать nlohmann/json:
```bash
cmake -DJSON_BuildTests=OFF -DJSON_MultipleHeaders=ON -B build -S .
```

---

## Step 3: Viewport (canvas)

### Цель
Pan, zoom, координатные преобразования.

### TDD

```cpp
// tests/test_viewport.cpp

#include <gtest/gtest.h>
#include "editor/viewport.h"

TEST(ViewportTest, ScreenToWorld_Identity) {
    Viewport vp;
    Pt screen(0.0f, 0.0f);
    Pt result = vp.screen_to_world(screen, Pt(0.0f, 0.0f));
    EXPECT_EQ(result, Pt::zero());
}

TEST(ViewportTest, ZoomAt_Mouse) {
    Viewport vp;
    vp.zoom_at(0.1f, Pt(100.0f, 100.0f), Pt(0.0f, 0.0f));
    EXPECT_GT(vp.zoom, 1.0f);
}
```

### Реализация

```cpp
// editor/viewport.h
#pragma once

#include "editor/data/pt.h"

struct Viewport {
    Pt pan;           // world coord of screen origin
    float zoom;
    float grid_step;

    Viewport() : pan(), zoom(1.0f), grid_step(16.0f) {}

    // Screen → World
    Pt screen_to_world(Pt screen, Pt canvas_min) const;

    // World → Screen
    Pt world_to_screen(Pt world, Pt canvas_min) const;

    // Pan
    void pan_by(Pt screen_delta);

    // Zoom
    void zoom_at(float delta, Pt screen_pos, Pt canvas_min);
};
```

---

## Step 4: Interaction state

### Цель
Отслеживание выделения, drag, pan.

### TDD

```cpp
// tests/test_interaction.cpp

#include <gtest/gtest.h>
#include "editor/interaction.h"

TEST(InteractionTest, DefaultIsEmpty) {
    Interaction i;
    EXPECT_FALSE(i.selected_node.has_value());
    EXPECT_EQ(i.dragging, Dragging::None);
}
```

### Реализация

```cpp
// editor/interaction.h
#pragma once

#include <optional>

enum class Dragging {
    None,
    Node,
    RoutingPoint
};

struct Interaction {
    std::optional<size_t> selected_node;
    std::optional<size_t> selected_wire;
    Dragging dragging = Dragging::None;
    bool panning = false;
    Pt drag_anchor;  // для накопления дробных смещений
};
```

---

## Step 5: Rendering (базовый)

### Цель
Рисование сетки и простейших примитивов.

### TDD

```cpp
// tests/test_render.cpp

#include <gtest/gtest.h>
#include "editor/render.h"

// Тест проверяет что рендер не падает на пустых данных
TEST(RenderTest, EmptyCircuit_DoesNotCrash) {
    Circuit c;
    // Не должен крашнуться
    render_circuit(c, nullptr, Viewport());
}
```

### Реализация

```cpp
// editor/render.h
#pragma once

#include "editor/data/circuit.h"
#include "editor/viewport.h"

// Абстрактный DrawList (imgui compatible)
struct DrawList {
    virtual ~DrawList() = default;
    virtual void add_line(Pt a, Pt b, uint32_t color, float thickness = 1.0f) = 0;
    virtual void add_rect(Pt min, Pt max, uint32_t color) = 0;
    virtual void add_circle(Pt center, float radius, uint32_t color) = 0;
    virtual void add_text(Pt pos, const char* text, uint32_t color) = 0;
};

// Рендер схемы в DrawList
void render_circuit(const Circuit& c, DrawList* dl, const Viewport& vp);

// Рендер сетки
void render_grid(DrawList* dl, const Viewport& vp, Pt canvas_min, Pt canvas_max);
```

---

## Step 6: Hit testing

### Цель
Определение что под мышью (node, wire, port).

### TDD

```cpp
// tests/test_hittest.cpp

#include <gtest/gtest.h>
#include "editor/hittest.h"

TEST(HitTest, Node_Inside) {
    Circuit c;
    c.nodes.push_back(Node::new("n1", "Batt", "Battery").at(10.0f, 10.0f).size(Pt(100.0f, 50.0f));

    auto hit = hit_test(c, Pt(50.0f, 30.0f), Viewport());
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->type, HitType::Node);
}
```

### Реализация

```cpp
// editor/hittest.h
#pragma once

#include "editor/data/circuit.h"
#include "editor/viewport.h"
#include <optional>

enum class HitType { None, Node, Wire, Port };

struct HitResult {
    HitType type;
    size_t index;       // index in nodes/wires
    size_t port_index;  // for HitType::Port
};

std::optional<HitResult> hit_test(const Circuit& c, Pt world_pos, const Viewport& vp);
```

---

## Step 7: Event handling

### Цель
Обработка мыши и клавиатуры.

### TDD

```cpp
// tests/test_events.cpp

#include <gtest/gtest.h>
#include "editor/app.h"

TEST(EventsTest, MouseDown_SelectsNode) {
    EditorApp app;
    app.circuit.nodes.push_back(Node::new("n1", "Batt", "Battery").at(0.0f, 0.0f));

    // Mouse down на node
    app.on_mouse_down(Pt(50.0f, 25.0f), MouseButton::Left, Pt(0.0f, 0.0f));

    ASSERT_TRUE(app.interaction.selected_node.has_value());
    EXPECT_EQ(*app.interaction.selected_node, 0);
}
```

### Реализация

```cpp
// editor/app.h
#pragma once

#include "editor/data/circuit.h"
#include "editor/viewport.h"
#include "editor/interaction.h"

enum class MouseButton { Left, Middle, Right };

class EditorApp {
public:
    Circuit circuit;
    Viewport viewport;
    Interaction interaction;

    EditorApp();

    // Event handlers
    void on_mouse_down(Pt world_pos, MouseButton btn, Pt canvas_min);
    void on_mouse_up(MouseButton btn);
    void on_mouse_drag(Pt world_delta, Pt canvas_min);
    void on_scroll(float delta, Pt mouse_pos, Pt canvas_min);
    void on_key_down(int key);

    // Main update
    void update(void* imgui_context);  // imgui: ImGuiContext*

private:
    void update_selection(Pt world_pos);
    void start_drag(Pt world_pos);
    void update_drag(Pt world_delta);
};
```

---

## Step 8: Интеграция с imgui

### Цель
Запустить редактор в окне.

### Реализация

```cpp
// editor/main.cpp

#include "editor/app.h"
#include <imgui.h>
#include <examples/imgui_impl_sdl2.h>
#include <examples/imgui_impl_opengl3.h>
// SDL2 или GLFW setup...

int main(int argc, char** argv) {
    // SDL2 + OpenGL setup...

    EditorApp app;

    while (running) {
        // SDL2: Handle events...

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Меню
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New")) { app.new_circuit(); }
                if (ImGui::MenuItem("Open...")) { /* load dialog */ }
                if (ImGui::MenuItem("Save")) { /* save */ }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // Central panel с canvas
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Canvas", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

        auto canvas_min = ImGui::GetWindowContentRegionMin();
        auto canvas_max = ImGui::GetWindowContentRegionMax();
        Pt canvas_min_pt(canvas_min.x, canvas_min.y);
        Pt canvas_max_pt(canvas_max.x, canvas_max.y);

        // Рендер через ImDrawList
        auto* dl = ImGui::GetWindowDrawList();
        ImDrawListWrapper wrapper(dl);  // wrapper для нашего DrawList

        // Сетка
        render_grid(&wrapper, app.viewport, canvas_min_pt, canvas_max_pt);

        // Схема
        render_circuit(app.circuit, &wrapper, app.viewport);

        // Event handling
        if (ImGui::IsWindowFocused()) {
            handle_imgui_events(app, ImGui::GetIO());
        }

        ImGui::End();
        ImGui::PopStyleVar();

        // Рендер
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
}
```

---

## DEBUG макрос

Используем общий `src/debug.h` для всего проекта:

```cpp
// В любом файле:
#include "debug.h"

void some_function() {
    Pt mouse_pos = ...;

    DEBUG_LOG("Mouse at ({:.1f}, {:.1f})", mouse_pos.x, mouse_pos.y);

    if (node_selected) {
        DEBUG_INFO("Selected node: {}", node->name);
    }

    // Проверка - продолжает работу но логирует
    DEBUG_ASSERT(!circuit.nodes.empty(), "Circuit should not be empty");

    // Фатальная проверка - abort() если false
    DEBUG_ASSERT_FATAL(ptr != nullptr, "Pointer must not be null");
}
```

### CMake настройка

```cmake
# В CMakeLists.txt
add_executable(an24_editor ...)

target_compile_definitions(an24_editor PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

# spdlog только в Debug
target_link_libraries(an24_editor PRIVATE
    $<$<CONFIG:Debug>:spdlog::spdlog>
    imgui::imgui
    nlohmann_json::nlohmann_json
)
```

---

## Структура файлов (итоговая)

```
src/editor/
├── CMakeLists.txt
├── debug.h              # DEBUG макросы
├── data/
│   ├── CMakeLists.txt
│   ├── pt.h
│   ├── port.h
│   ├── node.h
│   ├── wire.h
│   ├── circuit.h
│   └── test_data.cpp    # TDD тесты
├── persist.h
├── viewport.h
├── interaction.h
├── render.h
├── hittest.h
├── app.h
├── main.cpp
└── imgui_wrapper.h      # ImDrawList wrapper

src/tests/
└── CMakeLists.txt
```

---

## Зависимости

```bash
# Конфигурация CMake
find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)
FetchContent_Declare(imgui URL https://github.com/ocornut/imgui.git)
FetchContent_Declare(spdlog URL https://github.com/gabime/spdlog.git)
FetchContent_Declare(nlohmann_json URL https://github.com/nlohmann/json.git)
FetchContent_Declare(googletest URL https://github.com/google/googletest.git)
```

### CMake настройка для DEBUG-only spdlog

```cmake
# В editor/CMakeLists.txt
add_executable(an24_editor ...)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(an24_editor PRIVATE DEBUG)
    target_link_libraries(an24_editor PRIVATE spdlog::spdlog)
else()
    # RELEASE - не линкуем spdlog, не определяем DEBUG
    target_compile_definitions(an24_editor PRIVATE NDEBUG)
endif()
```

Или через generator expression:

```cmake
target_compile_definitions(an24_editor PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

target_link_libraries(an24_editor PRIVATE
    $<$<CONFIG:Debug>:spdlog::spdlog>
    imgui::imgui
    nlohmann_json::nlohmann_json
)
```

---

## Notes

1. **TDD**: Каждый step начинаем с `test_*.cpp` который НЕ компилируется или FAILS
2. **Комментарии на русском** в .h/.cpp файлах
3. **DEBUG macro**: использовать EDITOR_DEBUG() для логирования
4. **Red-Green**: сначала красный тест → писать код → зеленый тест
5. **Модульность**: каждый компонент (data, viewport, interact) - отдельный .h + тест
