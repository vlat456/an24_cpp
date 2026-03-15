# PROMPT: Node Color Dialog (Test-First)

## Goal

Add a per-node custom color feature: right-click node → context menu shows **"Color..."** item (above "Properties") → opens an ImGui color picker dialog. The chosen color is stored in `Node.color`, serialized to/from JSON, and applied to the VisualNode header and body fill at render time.

---

## Architecture Overview

This project is a C++ circuit editor (ImGui + SDL2 + OpenGL). Key structures:

- **`Node`** (`src/editor/data/node.h`) — data model for a node. Has `id`, `name`, `type_name`, `pos`, `size`, `params`, `render_hint`, `node_content`, etc.
- **`VisualNode`** (`src/editor/visual/node/node.h`) — rendering class. Constructed from `Node`. Renders header via `HeaderWidget(name, fill_color)` and body via `render_theme::COLOR_BODY_FILL`.
- **`Blueprint`** (`src/editor/data/blueprint.h`) — owns `vector<Node>` and `vector<Wire>`.
- **Context menu** — right-click on node sets `show_node_context_menu = true` + `context_menu_node_index` in `InputResult`. The main loop (`examples/an24_editor.cpp`) opens `ImGui::OpenPopup("NodeContextMenu")` and currently shows only "Properties".
- **Persist** (`src/editor/visual/scene/persist.cpp`) — `blueprint_to_editor_json()` / `load_editor_format()` serialize nodes to/from JSON.
- **Theme** (`src/editor/visual/renderer/render_theme.h`) — `get_node_colors(type_name)` returns `NodeColors{fill, border}` from a static lookup table.

---

## Test-First Implementation Plan

### Phase 1: Tests (write ALL tests first, they will fail)

Create `tests/test_node_color.cpp` with these test cases:

#### 1.1 Data model tests

```cpp
#include <gtest/gtest.h>
#include "editor/data/node.h"

// Node color should default to "no custom color" (nullopt)
TEST(NodeColor, DefaultColor_IsNullopt) {
    Node n;
    EXPECT_FALSE(n.color.has_value());
}

// Node color can be set
TEST(NodeColor, SetColor) {
    Node n;
    n.color = NodeColor{0.5f, 0.3f, 0.8f, 1.0f};
    ASSERT_TRUE(n.color.has_value());
    EXPECT_FLOAT_EQ(n.color->r, 0.5f);
    EXPECT_FLOAT_EQ(n.color->g, 0.3f);
    EXPECT_FLOAT_EQ(n.color->b, 0.8f);
    EXPECT_FLOAT_EQ(n.color->a, 1.0f);
}

// to_uint32 converts to ImGui ABGR format (0xAABBGGRR)
TEST(NodeColor, ToUint32_ABGR) {
    NodeColor c{1.0f, 0.0f, 0.0f, 1.0f}; // pure red
    EXPECT_EQ(c.to_uint32(), 0xFF0000FFu); // ABGR: alpha=FF, blue=00, green=00, red=FF
}

TEST(NodeColor, ToUint32_Green) {
    NodeColor c{0.0f, 1.0f, 0.0f, 1.0f};
    EXPECT_EQ(c.to_uint32(), 0xFF00FF00u);
}

TEST(NodeColor, ToUint32_HalfAlpha) {
    NodeColor c{1.0f, 1.0f, 1.0f, 0.5f};
    uint32_t result = c.to_uint32();
    uint8_t alpha = (result >> 24) & 0xFF;
    EXPECT_NEAR(alpha, 127, 1); // 0.5 * 255 ≈ 127
}
```

#### 1.2 JSON persistence roundtrip tests

```cpp
#include "editor/visual/scene/persist.h"

// Node with no color → JSON has no "color" key
TEST(NodeColorPersist, NoColor_NotInJson) {
    Blueprint bp;
    Node n;
    n.id = "bat1"; n.type_name = "Battery"; n.at(0, 0);
    n.input("v_in"); n.output("v_out");
    bp.add_node(std::move(n));

    std::string json = blueprint_to_editor_json(bp);
    EXPECT_EQ(json.find("\"color\""), std::string::npos)
        << "Node without custom color should not emit color key";
}

// Node with color → roundtrips through JSON
TEST(NodeColorPersist, Color_Roundtrip) {
    Blueprint bp;
    Node n;
    n.id = "bat1"; n.type_name = "Battery"; n.at(100, 200);
    n.input("v_in"); n.output("v_out");
    n.color = NodeColor{0.5f, 0.3f, 0.8f, 1.0f};
    bp.add_node(std::move(n));

    std::string json = blueprint_to_editor_json(bp);
    auto bp2 = blueprint_from_json(json);

    ASSERT_TRUE(bp2.has_value());
    ASSERT_EQ(bp2->nodes.size(), 1u);
    ASSERT_TRUE(bp2->nodes[0].color.has_value());
    EXPECT_NEAR(bp2->nodes[0].color->r, 0.5f, 0.01f);
    EXPECT_NEAR(bp2->nodes[0].color->g, 0.3f, 0.01f);
    EXPECT_NEAR(bp2->nodes[0].color->b, 0.8f, 0.01f);
    EXPECT_NEAR(bp2->nodes[0].color->a, 1.0f, 0.01f);
}

// Node color loads from JSON that has "color" field
TEST(NodeColorPersist, LoadFromJson_WithColor) {
    std::string json = R"({
        "devices": [{
            "name": "n1",
            "classname": "Battery",
            "ports": {"v_in": {"direction": "In", "type": "V"}, "v_out": {"direction": "Out", "type": "V"}},
            "pos": {"x": 0, "y": 0},
            "size": {"x": 120, "y": 80},
            "color": {"r": 0.2, "g": 0.4, "b": 0.6, "a": 1.0}
        }],
        "wires": []
    })";
    auto bp = blueprint_from_json(json);
    ASSERT_TRUE(bp.has_value());
    ASSERT_TRUE(bp->nodes[0].color.has_value());
    EXPECT_NEAR(bp->nodes[0].color->r, 0.2f, 0.001f);
}

// JSON without color field → no custom color
TEST(NodeColorPersist, LoadFromJson_WithoutColor) {
    std::string json = R"({
        "devices": [{
            "name": "n1",
            "classname": "Battery",
            "ports": {"v_in": {"direction": "In", "type": "V"}, "v_out": {"direction": "Out", "type": "V"}},
            "pos": {"x": 0, "y": 0},
            "size": {"x": 120, "y": 80}
        }],
        "wires": []
    })";
    auto bp = blueprint_from_json(json);
    ASSERT_TRUE(bp.has_value());
    EXPECT_FALSE(bp->nodes[0].color.has_value());
}

// Multiple nodes: only colored one gets color
TEST(NodeColorPersist, MultipleNodes_OnlyColoredOneHasColor) {
    Blueprint bp;
    Node n1; n1.id = "a"; n1.type_name = "Battery"; n1.at(0,0);
    n1.input("v_in"); n1.output("v_out");
    n1.color = NodeColor{1.0f, 0.0f, 0.0f, 1.0f};

    Node n2; n2.id = "b"; n2.type_name = "Resistor"; n2.at(200,0);
    n2.input("v_in");
    // n2 has NO color

    bp.add_node(std::move(n1));
    bp.add_node(std::move(n2));

    std::string json = blueprint_to_editor_json(bp);
    auto bp2 = blueprint_from_json(json);
    ASSERT_TRUE(bp2.has_value());
    EXPECT_TRUE(bp2->nodes[0].color.has_value());
    EXPECT_FALSE(bp2->nodes[1].color.has_value());
}
```

#### 1.3 VisualNode color application tests

```cpp
#include "editor/visual/node/node.h"
#include "editor/visual/renderer/render_theme.h"

// VisualNode should store custom color if Node has one
TEST(NodeColorVisual, CustomColor_StoredInVisualNode) {
    Node n;
    n.id = "bat"; n.name = "Bat"; n.type_name = "Battery";
    n.at(0, 0).size_wh(120, 80);
    n.input("v_in"); n.output("v_out");
    n.color = NodeColor{0.2f, 0.4f, 0.6f, 1.0f};

    VisualNode vn(n);
    // VisualNode should expose the custom color
    ASSERT_TRUE(vn.customColor().has_value());
    EXPECT_FLOAT_EQ(vn.customColor()->r, 0.2f);
}

// VisualNode without custom color should return nullopt
TEST(NodeColorVisual, NoCustomColor_Nullopt) {
    Node n;
    n.id = "bat"; n.name = "Bat"; n.type_name = "Battery";
    n.at(0, 0).size_wh(120, 80);
    n.input("v_in"); n.output("v_out");

    VisualNode vn(n);
    EXPECT_FALSE(vn.customColor().has_value());
}
```

#### 1.4 CMake target

Add to `tests/CMakeLists.txt`:

```cmake
# Node color tests
add_executable(node_color_tests
    test_node_color.cpp
)
target_include_directories(node_color_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_compile_definitions(node_color_tests PRIVATE EDITOR_TESTING)
target_link_libraries(node_color_tests PRIVATE
    editor_lib
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(node_color_tests)
```

Note: Most editor tests use `EDITOR_TESTING` define to stub out ImGui calls. Follow the same pattern as `test_persist.cpp` and `test_context_menu.cpp` for includes and link libraries.

---

### Phase 2: Implementation (make tests pass)

#### Touchpoint 1: `NodeColor` struct → `src/editor/data/node.h`

Add before the `Node` struct:

```cpp
/// Optional per-node custom color (RGBA, 0.0–1.0)
struct NodeColor {
    float r = 0.5f, g = 0.5f, b = 0.5f, a = 1.0f;

    /// Convert to ImGui uint32 ABGR format (0xAABBGGRR)
    uint32_t to_uint32() const {
        uint8_t ri = static_cast<uint8_t>(r * 255.0f + 0.5f);
        uint8_t gi = static_cast<uint8_t>(g * 255.0f + 0.5f);
        uint8_t bi = static_cast<uint8_t>(b * 255.0f + 0.5f);
        uint8_t ai = static_cast<uint8_t>(a * 255.0f + 0.5f);
        return (uint32_t(ai) << 24) | (uint32_t(bi) << 16) | (uint32_t(gi) << 8) | uint32_t(ri);
    }
};
```

Add to `Node` struct, after `node_content`:

```cpp
    std::optional<NodeColor> color;   ///< Per-node custom color (nullopt = use theme default)
```

Add `#include <optional>` to the top of the file if not already present.

Update the `Node()` constructor initializer list — add `, color(std::nullopt)` (or leave it — `optional` defaults to `nullopt`).

---

#### Touchpoint 2: JSON save — `src/editor/visual/scene/persist.cpp` → `blueprint_to_editor_json()`

Inside the `for (const auto& n : bp.nodes)` loop, after the `device["content"] = content;` block, before `devices.push_back(device);`:

```cpp
        // Per-node custom color (optional)
        if (n.color.has_value()) {
            device["color"] = {
                {"r", n.color->r},
                {"g", n.color->g},
                {"b", n.color->b},
                {"a", n.color->a}
            };
        }
```

---

#### Touchpoint 3: JSON load — `src/editor/visual/scene/persist.cpp` → `load_editor_format()`

Inside the per-device loop, after the content loading block, before `bp.nodes.push_back`:

```cpp
        // Per-node custom color (optional)
        if (d.contains("color") && d["color"].is_object()) {
            NodeColor c;
            c.r = d["color"].value("r", 0.5f);
            c.g = d["color"].value("g", 0.5f);
            c.b = d["color"].value("b", 0.5f);
            c.a = d["color"].value("a", 1.0f);
            n.color = c;
        }
```

---

#### Touchpoint 4: VisualNode stores + exposes custom color — `src/editor/visual/node/node.h`

Add to `VisualNode` public section:

```cpp
    /// Per-node custom color (nullopt = use theme default)
    const std::optional<NodeColor>& customColor() const { return custom_color_; }
    void setCustomColor(std::optional<NodeColor> c) { custom_color_ = std::move(c); }
```

Add to `VisualNode` protected section:

```cpp
    std::optional<NodeColor> custom_color_;
```

In the constructor (`src/editor/visual/node/node.cpp`), after `node_content_(node.node_content)`, add:

```cpp
    , custom_color_(node.color)
```

---

#### Touchpoint 5: VisualNode uses custom color in `render()` — `src/editor/visual/node/node.cpp`

In `VisualNode::render()`, the body fill currently uses the global constant:

```cpp
dl->add_rect_filled(Pt(screen_min.x, screen_min.y + header_h), screen_max, render_theme::COLOR_BODY_FILL);
```

Replace with:

```cpp
    uint32_t body_fill = custom_color_.has_value()
        ? custom_color_->to_uint32()
        : render_theme::COLOR_BODY_FILL;
    dl->add_rect_filled(Pt(screen_min.x, screen_min.y + header_h), screen_max, body_fill);
```

Also in `buildLayout()`, the header color currently uses a constant:

```cpp
layout_.addWidget(std::make_unique<HeaderWidget>(name_, render_theme::COLOR_BUS_FILL));
```

To also tint the header, you have two options:

- **Option A (simple):** Keep header default, only tint body. This is easier.
- **Option B (full tint):** Pass custom color to HeaderWidget too. This requires storing the color in VisualNode before calling `buildLayout()`. The header would use a slightly lighter/darker version of the custom color.

**Recommended: Option A** — tint body only. The header already has the type name so visual identification is preserved.

---

#### Touchpoint 6: Context menu — `examples/an24_editor.cpp`

Current context menu (around line 633):

```cpp
if (ImGui::BeginPopup("NodeContextMenu")) {
    if (ImGui::MenuItem("Properties")) {
        app.open_properties_for_node(app.context_menu_node_index);
    }
    ImGui::EndPopup();
}
```

Replace with:

```cpp
if (ImGui::BeginPopup("NodeContextMenu")) {
    if (ImGui::MenuItem("Color...")) {
        app.open_color_picker_for_node(app.context_menu_node_index);
    }
    if (ImGui::MenuItem("Properties")) {
        app.open_properties_for_node(app.context_menu_node_index);
    }
    ImGui::EndPopup();
}
```

---

#### Touchpoint 7: Color picker dialog — `src/editor/app.h` + `src/editor/app.cpp`

Add to `EditorApp` (app.h):

```cpp
    /// Color picker state
    bool show_color_picker = false;
    size_t color_picker_node_index = 0;
    float color_picker_rgba[4] = {0.5f, 0.5f, 0.5f, 1.0f};

    /// Open color picker for a specific node
    void open_color_picker_for_node(size_t node_index);
```

Add to `app.cpp`:

```cpp
void EditorApp::open_color_picker_for_node(size_t node_index) {
    if (node_index >= blueprint.nodes.size()) return;
    color_picker_node_index = node_index;
    show_color_picker = true;

    // Pre-fill with existing custom color or theme default
    const Node& node = blueprint.nodes[node_index];
    if (node.color.has_value()) {
        color_picker_rgba[0] = node.color->r;
        color_picker_rgba[1] = node.color->g;
        color_picker_rgba[2] = node.color->b;
        color_picker_rgba[3] = node.color->a;
    } else {
        // Use body fill default as starting color
        color_picker_rgba[0] = 0.19f;  // COLOR_BODY_FILL = 0xFF303040 → r≈0x40/255
        color_picker_rgba[1] = 0.19f;
        color_picker_rgba[2] = 0.25f;
        color_picker_rgba[3] = 1.0f;
    }
}
```

---

#### Touchpoint 8: Color picker window render — `examples/an24_editor.cpp`

Add after the node context menu block, before `app.properties_window.render();`:

```cpp
        // Color picker dialog
        if (app.show_color_picker) {
            ImGui::OpenPopup("Node Color");
        }
        if (ImGui::BeginPopupModal("Node Color", &app.show_color_picker, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::ColorPicker4("##picker", app.color_picker_rgba,
                ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayRGB);

            if (ImGui::Button("Apply")) {
                Node& node = app.blueprint.nodes[app.color_picker_node_index];
                node.color = NodeColor{
                    app.color_picker_rgba[0],
                    app.color_picker_rgba[1],
                    app.color_picker_rgba[2],
                    app.color_picker_rgba[3]
                };
                // Update VisualNode too
                auto* vn = app.scene.getVisualNode(app.color_picker_node_index);
                if (vn) vn->setCustomColor(node.color);
                app.show_color_picker = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset")) {
                Node& node = app.blueprint.nodes[app.color_picker_node_index];
                node.color = std::nullopt;
                auto* vn = app.scene.getVisualNode(app.color_picker_node_index);
                if (vn) vn->setCustomColor(std::nullopt);
                app.show_color_picker = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                app.show_color_picker = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
```

**Note:** You may need to add a `getVisualNode(size_t index)` method to `VisualScene` if it doesn't exist. Check `scene.h` — the cache has a `nodes_` vector. Expose it:

```cpp
VisualNode* getVisualNode(size_t index) {
    return (index < cache_.nodes_.size()) ? cache_.nodes_[index].get() : nullptr;
}
```

---

## File Summary

| File                                  | Change                                                                                                      |
| ------------------------------------- | ----------------------------------------------------------------------------------------------------------- |
| `src/editor/data/node.h`              | Add `NodeColor` struct + `std::optional<NodeColor> color` to `Node`                                         |
| `src/editor/visual/scene/persist.cpp` | Save `"color"` in `blueprint_to_editor_json()`, load in `load_editor_format()`                              |
| `src/editor/visual/node/node.h`       | Add `custom_color_` field + `customColor()` / `setCustomColor()` to `VisualNode`                            |
| `src/editor/visual/node/node.cpp`     | Store `node.color` in constructor. Use `custom_color_` in `render()` for body fill.                         |
| `src/editor/app.h`                    | Add `show_color_picker`, `color_picker_node_index`, `color_picker_rgba[4]`, `open_color_picker_for_node()`  |
| `src/editor/app.cpp`                  | Implement `open_color_picker_for_node()`                                                                    |
| `examples/an24_editor.cpp`            | Add "Color..." menu item to NodeContextMenu. Add `ImGui::ColorPicker4` popup modal with Apply/Reset/Cancel. |
| `src/editor/visual/scene/scene.h`     | Possibly add `getVisualNode(size_t)` accessor                                                               |
| `tests/test_node_color.cpp`           | **NEW** — all tests above                                                                                   |
| `tests/CMakeLists.txt`                | Add `node_color_tests` target                                                                               |

## Verification

After implementation:

```bash
cd build && cmake .. && make -j8 && ctest --output-on-failure
```

All existing 781 tests must still pass, plus the new `node_color_tests`.

## Important Notes

1. **ImGui color format:** ImGui uses ABGR byte order in uint32: `0xAABBGGRR`. Red = `0xFF0000FF`. This is why `to_uint32()` packs bytes as `(A << 24) | (B << 16) | (G << 8) | R`.

2. **`EDITOR_TESTING` define:** Tests that include editor headers compile with `-DEDITOR_TESTING` to stub out `#include <imgui.h>`. The NodeColor struct and persistence code don't use ImGui directly, so tests should compile fine.

3. **Backward compatibility:** Old JSON files without `"color"` field will load with `color = nullopt` (the `d.contains("color")` check ensures this). No migration needed.

4. **The `show_color_picker` bool must be set to `false` one frame AFTER `ImGui::OpenPopup()`** — this is the standard ImGui popup lifecycle. The pattern `if (show) { OpenPopup(); show = false; }` followed by `if (BeginPopupModal(...))` handles this correctly. But note `BeginPopupModal` takes a `bool* p_open` parameter — passing `&app.show_color_picker` lets ImGui close it with the X button too.

5. **Body fill only vs. header+body:** The recommended approach is body fill only. If you want to also tint the header, you need to rebuild the layout or change `HeaderWidget::fill_color_` after construction (add a setter to `HeaderWidget`).
