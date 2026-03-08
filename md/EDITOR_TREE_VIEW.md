# Editor Tree View - ImGui Implementation Analysis

## Overview

Да, средствами ImGui можно сделать отличный tree view схемы. ImGui имеет встроенные `TreeNode`/`TreePop` виджеты с native collapsing, search, и selection.

---

## Возможные Иерархии

### 1. **По Типу Компонентов** (Самый простой)

```
Electrical
├── Power Sources
│   ├── Battery
│   ├── Generator
│   └── GS24 (Starter-Generator)
├── Control Devices
│   ├── Switch
│   ├── Relay
│   └── HoldButton
├── Control Systems
│   ├── PID
│   ├── PI
│   ├── PD
│   └── P
├── Loads
│   ├── Load
│   ├── HighPowerLoad
│   └── IndicatorLight
└── Logic
    ├── Comparator
    ├── Splitter
    └── Merger

Hydraulic
├── ElectricPump
└── SolenoidValve

Mechanical
├── RU19A (APU)
└── InertiaNode

Sensors
├── Voltmeter
├── TempSensor
└── Gyroscope
```

### 2. **По Blueprint Hierarchy** (Сложнее, но мощнее)

```
Root Blueprint (an24_plane)
├── Electrical System (collapsed_group: "electrical")
│   ├── Battery (main_battery)
│   ├── RUG82 (voltage_reg)
│   ├── GS24 (starter_gen)
│   └── [nested blueprint: lamp_circuit]
│       └── (dbl-click to expand)
│           ├── BlueprintInput (power_in)
│           ├── IndicatorLight (lamp)
│           └── BlueprintOutput (lamp_out)
├── Hydraulic System (collapsed_group: "hydraulic")
│   └── ElectricPump (pump1)
└── APU System (collapsed_group: "apu")
    └── RU19A (apu_1)
```

### 3. **По Доменам** (Multi-domain)

```
Electrical Domain (60Hz)
├── Battery: main_battery [28.0V]
├── Bus: main_bus
└── Load: avionics_load

Mechanical Domain (20Hz)
└── RU19A: apu [RUNNING]

Hydraulic Domain (5Hz)
└── ElectricPump: pump1

Thermal Domain (1Hz)
└── Radiator: rad1
```

### 4. **По Подключениям** (Netlist-style)

```
Signals (Electrical)
├── 28V_main
│   ├── Source: Battery.main_battery
│   ├── Sink: RUG82.voltage_reg.input
│   └── Sink: GS24.starter_gen.field
├── 5V_avionics
│   └── [connected nodes...]
└── ground
    └── [all ground connections]

Wires
├── wire_0: Battery.main_battery → Bus.main_bus
├── wire_1: Bus.main_bus → RUG82.voltage_reg.input
└── ... (all wires)
```

---

## ImGui Implementation Pattern

### Basic Tree (по типам компонентов)

```cpp
// examples/an24_editor.cpp - добавить в главное окно

if (ImGui::Begin("Component Tree")) {
    RenderComponentTree(app);
}
ImGui::End();

void RenderComponentTree(EditorApp& app) {
    auto& blueprint = app.blueprint;

    // Group nodes by type
    std::unordered_map<std::string, std::vector<const Node*>> by_type;
    for (const auto& node : blueprint.nodes) {
        by_type[node.type_name].push_back(&node);
    }

    // Create category hierarchy
    if (ImGui::TreeNode("Electrical")) {
        if (ImGui::TreeNode("Power Sources")) {
            RenderTypeNode("Battery", by_type);
            RenderTypeNode("Generator", by_type);
            RenderTypeNode("GS24", by_type);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Control")) {
            RenderTypeNode("Switch", by_type);
            RenderTypeNode("Relay", by_type);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Control Systems")) {
            RenderTypeNode("PID", by_type);
            RenderTypeNode("PI", by_type);
            RenderTypeNode("PD", by_type);
            RenderTypeNode("P", by_type);
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Hydraulic")) {
        RenderTypeNode("ElectricPump", by_type);
        RenderTypeNode("SolenoidValve", by_type);
        ImGui::TreePop();
    }

    // ... etc
}

void RenderTypeNode(const std::string& type_name,
                    const std::unordered_map<std::string, std::vector<const Node*>>& by_type) {
    auto it = by_type.find(type_name);
    if (it == by_type.end()) return;

    const auto& nodes = it->second;
    std::string label = fmt::format("{} ({})", type_name, nodes.size());

    if (ImGui::TreeNode(label.c_str())) {
        for (const auto* node : nodes) {
            // Click to select node on canvas
            if (ImGui::Selectable(node->name.c_str(), IsNodeSelected(*node))) {
                SelectNodeOnCanvas(*node);
            }
            // Show params inline
            if (ImGui::IsItemHovered()) {
                ShowNodeTooltip(*node);
            }
        }
        ImGui::TreePop();
    }
}
```

### Blueprint Hierarchy Tree (сложнее)

```cpp
void RenderBlueprintTree(EditorApp& app) {
    auto& blueprint = app.blueprint;

    // Top-level nodes (group_id == "")
    std::vector<const Node*> top_level;
    std::unordered_map<std::string, std::vector<const Node*>> by_group;

    for (const auto& node : blueprint.nodes) {
        if (node.group_id.empty()) {
            top_level.push_back(&node);
        } else {
            by_group[node.group_id].push_back(&node);
        }
    }

    // Render top level
    for (const auto* node : top_level) {
        RenderTreeNode(*node, by_group, blueprint);
    }
}

void RenderTreeNode(const Node& node,
                    const std::unordered_map<std::string, std::vector<const Node*>>& by_group,
                    const Blueprint& bp) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
    if (IsNodeSelected(node)) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    std::string label = fmt::format("{} [{}]", node.name, node.type_name);

    if (node.kind == NodeKind::Blueprint || !node.group_id.empty()) {
        // Collapsed blueprint or group member
        if (ImGui::TreeNodeEx(label.c_str(), flags)) {
            // Show params
            for (const auto& [key, value] : node.params) {
                ImGui::Text("  %s: %s", key.c_str(), value.c_str());
            }

            // If this is a collapsed blueprint, show internal nodes
            if (node.kind == NodeKind::Blueprint) {
                auto* group = FindGroupById(bp, node.group_id);
                if (group) {
                    for (const auto& internal_id : group->internal_node_ids) {
                        const auto* internal_node = bp.find_node(internal_id.c_str());
                        if (internal_node) {
                            RenderTreeNode(*internal_node, by_group, bp);
                        }
                    }
                }
            }

            ImGui::TreePop();
        }
    } else {
        // Leaf node (regular component)
        if (ImGui::Selectable(label.c_str(), IsNodeSelected(node))) {
            SelectNodeOnCanvas(node);
        }
    }
}
```

### Domain-Based Tree (multi-frequency)

```cpp
void RenderDomainTree(EditorApp& app) {
    auto& blueprint = app.blueprint;

    // Group by domain (from ComponentDefinition)
    if (ImGui::TreeNode("Electrical (60Hz)")) {
        for (const auto& node : blueprint.nodes) {
            const auto* def = app.component_registry.get(node.type_name);
            if (def && def->has_domain("Electrical")) {
                RenderNodeLeaf(node);
            }
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Mechanical (20Hz)")) {
        // same pattern
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Hydraulic (5Hz)")) {
        // same pattern
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Thermal (1Hz)")) {
        // same pattern
        ImGui::TreePop();
    }
}
```

---

## Интеграция с Существующим OOP

### EditorApp Extension

```cpp
// app.h
struct EditorApp {
    // ... existing ...

    /// Tree view state
    bool show_component_tree = false;
    bool show_blueprint_tree = false;
    bool show_domain_tree = false;

    /// Tree view selection sync
    std::optional<std::string> tree_selected_node_id;

    void RenderComponentTree();
    void RenderBlueprintTree();
    void RenderDomainTree();
};
```

### Selection Synchronization

```cpp
// app.cpp
void EditorApp::RenderComponentTree() {
    if (!show_component_tree) return;

    if (ImGui::Begin("Component Tree", &show_component_tree)) {
        // ... render tree ...

        // Sync selection with canvas
        if (tree_selected_node_id.has_value()) {
            input.clear_selection();
            size_t idx = blueprint.find_node_index(tree_selected_node_id->c_str());
            if (idx != -1) {
                input.select_node(idx);
            }
        }
    }
    ImGui::End();
}
```

### Double-Click → Focus on Canvas

```cpp
// В tree node rendering
if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
    FocusNodeOnCanvas(node);
}

void EditorApp::FocusNodeOnCanvas(const Node& node) {
    // Center viewport on node
    Pt node_center = node.pos + node.size * 0.5f;
    scene.viewport().pan = -node_center + scene.canvas_size() * 0.5f;
}
```

---

## Поиск и Фильтрация

### Native ImGui Search

```cpp
void RenderComponentTreeWithSearch(EditorApp& app) {
    static char search_buf[256] = "";

    if (ImGui::Begin("Component Tree")) {
        // Search box
        ImGui::InputText("Search", search_buf, sizeof(search_buf));
        std::string search = search_buf;

        // Category filter
        static int category_filter = 0; // 0=All, 1=Electrical, 2=Hydraulic, etc.
        ImGui::RadioButton("All", &category_filter, 0); ImGui::SameLine();
        ImGui::RadioButton("Electrical", &category_filter, 1); ImGui::SameLine();
        ImGui::RadioButton("Hydraulic", &category_filter, 2);

        // Render filtered tree
        for (const auto& node : app.blueprint.nodes) {
            if (!search.empty()) {
                std::string node_str = fmt::format("{} {}", node.name, node.type_name);
                if (node_str.find(search) == std::string::npos) {
                    continue;  // Skip non-matching
                }
            }

            if (category_filter != 0) {
                const auto* def = app.component_registry.get(node.type_name);
                if (!def) continue;
                if (category_filter == 1 && !def->has_domain("Electrical")) continue;
                if (category_filter == 2 && !def->has_domain("Hydraulic")) continue;
            }

            RenderNodeLeaf(node);
        }
    }
    ImGui::End();
}
```

---

## Ключевые Особенности ImGui Tree API

| Функция | Описание |
|----------|----------|
| `ImGui::TreeNode("label")` | Создаёт collapsible header |
| `ImGui::TreeNodeEx("label", flags)` | С флагами (Selected, OpenOnArrow, etc.) |
| `ImGui::SetNextItemOpen(true)` | Программно раскрыть/свернуть |
| `ImGui::CollapsingHeader("label")` | Compact tree node |
| `ImGui::Selectable("label")` | Leaf node (может быть selected) |
| `ImGui::TreePop()` | Закрывает TreeNode |

### Полезные Флаги

```cpp
ImGuiTreeNodeFlags_OpenOnArrow          // Раскрытие только по стрелочке
ImGuiTreeNodeFlags_Selected            // Подсветить как выбранный
ImGuiTreeNodeFlags_OpenOnDoubleClick    // Раскрытие по двойному клику
ImGuiTreeNodeFlags_Leaf                 // Лист (нет детей)
ImGuiTreeNodeFlags_DefaultOpen          // Раскрыт по умолчанию
ImGuiTreeNodeFlags_Framed               // С рамкой
```

---

## Преимущества ImGui Tree View

1. **Native UX** - Collapsing, keyboard navigation уже есть
2. **Zero Allocation** - ImGui Immediate Mode не хранит состояние
3. **Fast** - Tree traversal только когда рендерим
4. **Flexible** - Любая иерархия, простое реорганизация
5. **Integrated** - Можно комбинировать с Canvas (selection sync)

---

## Рекомендация

**Начни с простого:** Tree View по типам компонентов + поиск.

```
Component Tree Window
├── Search Input
├── Filter: [All] [Electrical] [Hydraulic] [Sensors]
└── Tree
    ├── Electrical
    │   ├── Power Sources
    │   │   ├── Battery (3 instances)
    │   │   └── Generator (1 instance)
    │   └── Control
    │       └── Switch (5 instances)
    └── Sensors
        └── Voltmeter (2 instances)
```

**Потом добавь:**
1. Blueprint Hierarchy (nested blueprints)
2. Domain-based view (multi-frequency scheduling)
3. Wire/Signal connectivity view

**Интеграция с Properties Window:**
- Right-click on tree node → "Properties"
- Double-click on tree node → Focus on canvas
- Selection synced between tree and canvas

---

## Файлы для Изменения

| Файл | Изменение |
|------|-----------|
| `examples/an24_editor.cpp` | Добавить ImGui::Begin("Component Tree") |
| `src/editor/app.h` | Добавить `show_component_tree`, tree render methods |
| `src/editor/app.cpp` | Реализовать `RenderComponentTree()` |
| `src/editor/input/input_types.h` | Добавить `focus_node` в InputResult |

---

## Заключение

**Ответ: ДА**, ImGui полностью подходит для Tree View схемы!

- ✅ Native tree widgets (`TreeNode`/`TreePop`)
- ✅ Collapsing, search, selection из коробки
- ✅ Zero-allocation immediate mode
- ✅ Простая интеграция с существующим OOP
- ✅ Можно комбинировать с Canvas (selection sync)

Рекомендую начать с **Tree View по типам компонентов** - это наиболее полезно для навигации по большой схеме.
