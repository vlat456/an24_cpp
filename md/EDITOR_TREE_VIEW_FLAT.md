# Editor Tree View - Flat Flow Diagram

## Overview

Плоский список всех компонентов, каждый раскрывается в список портов с показом подключений (что куда подключено).

---

## Структура

```
Battery (main_battery)
├── v_in  → [не подключён]
└── v_out → Switch.v_in

Switch (sw1)
├── v_in  ← Battery.v_out
└── v_out → Lamp.v_in

Lamp (lamp1)
├── v_in  ← Switch.v_out
└── unused → [нет таких портов]
```

---

## Реализация

### Data Preparation (найти подключения для порта)

```cpp
// src/editor/app.h
struct EditorApp {
    // ... existing ...

    /// Для каждого port храним ссылку на подключённый port
    struct PortConnection {
        std::string node_id;    // "main_battery"
        std::string port_name;  // "v_out"
        std::string display;    // "Battery.v_out"
    };

    /// Найти подключение к порту
    std::optional<PortConnection> find_connection(
        const std::string& node_id,
        const std::string& port_name,
        PortSide side) const;
};
```

```cpp
// src/editor/app.cpp
std::optional<EditorApp::PortConnection> EditorApp::find_connection(
    const std::string& node_id,
    const std::string& port_name,
    PortSide side) const
{
    // Ищем wire, подключённый к этому порту
    for (const auto& wire : blueprint.wires) {
        if (side == PortSide::Input) {
            if (wire.end.node_id == node_id && wire.end.port == port_name) {
                // Нашли! wire.end → наш порт, значит wire.start → источник
                const Node* src_node = blueprint.find_node(wire.start.node_id.c_str());
                if (src_node) {
                    return PortConnection{
                        wire.start.node_id,
                        wire.start.port,
                        fmt::format("{}.{}", src_node->name, wire.start.port)
                    };
                }
            }
        } else { // Output
            if (wire.start.node_id == node_id && wire.start.port == port_name) {
                const Node* dst_node = blueprint.find_node(wire.end.node_id.c_str());
                if (dst_node) {
                    return PortConnection{
                        wire.end.node_id,
                        wire.end.port,
                        fmt::format("{}.{}", dst_node->name, wire.end.port)
                    };
                }
            }
        }
    }
    return std::nullopt;  // Порт не подключён
}
```

---

## Tree Rendering (ImGui)

```cpp
// src/editor/app.cpp
void EditorApp::RenderComponentTree() {
    if (!show_component_tree) return;

    ImGui::Begin("Component Tree", &show_component_tree);

    // Search/filter
    static char search_buf[128] = "";
    ImGui::InputText("Search", search_buf, sizeof(search_buf));
    std::string search = search_buf;

    for (const auto& node : blueprint.nodes) {
        // Filter by search
        if (!search.empty()) {
            std::string search_str = fmt::format("{} {}", node.name, node.type_name);
            if (search_str.find(search) == std::string::npos) {
                continue;
            }
        }

        // TreeNode для каждого компонента
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
        if (IsNodeSelected(node)) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        std::string label = fmt::format("{} [{}]", node.name, node.type_name);

        if (ImGui::TreeNodeEx(label.c_str(), flags)) {
            // Inputs
            if (!node.inputs.empty()) {
                if (ImGui::TreeNode("Inputs")) {
                    for (const auto& port : node.inputs) {
                        RenderPortRow(node, port, PortSide::Input);
                    }
                    ImGui::TreePop();
                }
            }

            // Outputs
            if (!node.outputs.empty()) {
                if (ImGui::TreeNode("Outputs")) {
                    for (const auto& port : node.outputs) {
                        RenderPortRow(node, port, PortSide::Output);
                    }
                    ImGui::TreePop();
                }
            }

            ImGui::TreePop();
        }

        // Click on TreeNode → select node on canvas
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
            SelectNodeOnCanvas(node);
        }
    }

    ImGui::End();
}

void EditorApp::RenderPortRow(const Node& node, const Port& port, PortSide side) {
    // Найти подключение
    auto conn = find_connection(node.id, port.name, side);

    // Иконка направления
    const char* arrow = (side == PortSide::Input) ? "←" : "→";

    // Label: "v_in → Battery.v_out"
    std::string port_label = fmt::format("{} {}", port.name, arrow);

    ImGui::Bullet();

    if (conn.has_value()) {
        // Показать подключение
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "%s %s",
                          port_label.c_str(), conn->display.c_str());

        // Double-click on connection → focus connected node
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            FocusNodeOnCanvas(conn->node_id);
        }
    } else {
        // Порт не подключён
        ImGui::TextDisabled("%s [not connected]", port_label.c_str());
    }

    // Right-click on port → show port info (type, domain)
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1)) {
        show_port_context_menu = true;
        context_menu_port = port;
    }
}
```

---

## Визуальные улучшения

### Цветовая кодировка типов портов

```cpp
ImVec4 GetPortTypeColor(PortType type) {
    switch (type) {
        case PortType::V:  return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);  // Red (Voltage)
        case PortType::I:  return ImVec4(0.3f, 1.0f, 0.3f, 1.0f);  // Green (Current)
        case PortType::P:  return ImVec4(0.3f, 0.3f, 1.0f, 1.0f);  // Blue (Pressure)
        case PortType::Q:  return ImVec4(1.0f, 1.0f, 0.3f, 1.0f);  // Yellow (Flow)
        case PortType::T:  return ImVec4(1.0f, 0.3f, 1.0f, 1.0f);  // Magenta (Temperature)
        case PortType::W:  return ImVec4(0.3f, 1.0f, 1.0f, 1.0f);  // Cyan (Angular velocity)
        case PortType::Any: return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);  // Gray
        default: return ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
    }
}

void RenderPortRow(const Node& node, const Port& port, PortSide side) {
    ImVec4 color = GetPortTypeColor(port.type);

    auto conn = find_connection(node.id, port.name, side);
    const char* arrow = (side == PortSide::Input) ? "←" : "→";
    std::string port_label = fmt::format("{} {}", port.name, arrow);

    if (conn.has_value()) {
        ImGui::TextColored(color, "%s %s", port_label.c_str(), conn->display.c_str());
    } else {
        ImGui::TextColored(color * ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                          "%s [disconnected]", port_label.c_str());
    }
}
```

### Сортировка узлов

```cpp
void EditorApp::RenderComponentTree() {
    // ... search ...

    // Сортировка по имени или типу
    std::vector<const Node*> sorted_nodes;
    for (const auto& node : blueprint.nodes) {
        sorted_nodes.push_back(&node);
    }

    static int sort_mode = 0; // 0=by name, 1=by type, 2=by connections
    ImGui::RadioButton("Name", &sort_mode, 0); ImGui::SameLine();
    ImGui::RadioButton("Type", &sort_mode, 1); ImGui::SameLine();
    ImGui::RadioButton("Connections", &sort_mode, 2);

    std::sort(sorted_nodes.begin(), sorted_nodes.end(),
        [sort_mode, this](const Node* a, const Node* b) {
            if (sort_mode == 0) return a->name < b->name;
            if (sort_mode == 1) return a->type_name < b->type_name;
            // sort_mode == 2: by connection count
            return CountConnections(*a) > CountConnections(*b);
        });

    for (const auto* node : sorted_nodes) {
        // ... render node ...
    }
}

size_t EditorApp::CountConnections(const Node& node) const {
    size_t count = 0;
    for (const auto& wire : blueprint.wires) {
        if (wire.start.node_id == node.id || wire.end.node_id == node.id) {
            count++;
        }
    }
    return count;
}
```

---

## Полный пример готового дерева

```
┌─────────────────────────────────────────────┐
│ Component Tree                    [_][□][×] │
├─────────────────────────────────────────────┤
│ Search: [_______________]                   │
│ Sort: (•) Name  ( ) Type  ( ) Connections  │
├─────────────────────────────────────────────┤
│ ▼ Battery (main_battery)                   │
│   ▼ Inputs                                  │
│     → [not connected]                       │
│   ▼ Outputs                                 │
│     → Switch.sw1.v_in                       │
│                                             │
│ ▼ Switch (sw1)                             │
│   ▼ Inputs                                  │
│     ← Battery.main_battery.v_out            │
│   ▼ Outputs                                 │
│     → Lamp.lamp1.v_in                       │
│                                             │
│ ▼ Lamp (lamp1)                             │
│   ▼ Inputs                                  │
│     ← Switch.sw1.v_out                      │
│   ▼ Outputs                                 │
│     → [not connected]                       │
└─────────────────────────────────────────────┘
```

---

## Интерактивность

| Действие | Результат |
|----------|----------|
| Клик на Node | Выделить на canvas |
| Двойной клик на Node | Центрировать viewport |
| Двойной клик на подключение | Фокус на подключённый node |
| Right-click на Port | Показать порт инфо (type, domain) |
| Hover на подключение | Подсветить wire на canvas |

---

## Интеграция в main loop

```cpp
// examples/an24_editor.cpp

// В меню View добавить:
if (ImGui::BeginMenu("View")) {
    if (ImGui::MenuItem("Component Tree", "Ctrl+T", app.show_component_tree)) {
        app.show_component_tree = !app.show_component_tree;
    }
    // ... existing ...
    ImGui::EndMenu();
}

// В main loop (после canvas):
app.RenderComponentTree();
```

---

## File Changes Summary

| File | Changes |
|------|---------|
| `src/editor/app.h` | Add `show_component_tree`, `find_connection()`, `RenderComponentTree()` |
| `src/editor/app.cpp` | Implement tree rendering, port connection lookup |
| `examples/an24_editor.cpp` | Add menu item + call `RenderComponentTree()` |

---

## Success Criteria

- [ ] Плоский список всех компонентов
- [ ] Каждый Node раскрывается в Inputs/Outputs
- [ ] Показать подключения (Node.v_in → Wire)
- [ ] Цветовая кодировка типов портов (V/I/P/Q/T/W/Any)
- [ ] Сортировка (Name/Type/Connections)
- [ ] Поиск по имени/типу
- [ ] Двойной клик → фокус на canvas
- [ ] Right-click → свойства порта

---

## Заключение

**Это самый полезный Tree View** для отладки схемы:
- Видно весь поток сигналов (Battery → Switch → Lamp)
- Нашёл разрывы ([not connected])
- Понятна топология схемы

**Implementation: ~2-3 часа**

1. `find_connection()` - 30 мин
2. `RenderComponentTree()` - 1 час
3. Цветовая кодировка + сортировка - 30 мин
4. Интерактивность (click, double-click) - 30 мин
5. Тесты - 30 мин
