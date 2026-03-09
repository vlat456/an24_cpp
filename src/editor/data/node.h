#pragma once

#include "pt.h"
#include "port.h"
#include <string>
#include <vector>
#include <unordered_map>

/// Тип содержимого узла (пока простой enum)
enum class NodeContentType {
    None,
    Gauge,     ///< Измерительный прибор
    Switch,    ///< Переключатель
    Value,     ///< Отображаемое значение
    Text       ///< Текст
};

/// Содержимое узла (пока placeholder)
struct NodeContent {
    NodeContentType type = NodeContentType::None;
    std::string label;
    float value = 0.0f;
    float min = 0.0f;
    float max = 1.0f;
    std::string unit;
    bool state = false;
};

/// Узел в схеме - компонент (батарея, насос, и т.д.)
struct Node {
    std::string id;          ///< Уникальный ID
    std::string name;        ///< Отображаемое имя
    std::string type_name;   ///< Тип (Battery, Pump, Bus, etc.)
    std::string render_hint; ///< Visual hint for rendering ("bus", "ref", or empty)
    bool expandable = false; ///< True if node has sub-graph (double-clickable)

    // Phase 5.1: Hierarchical blueprint support
    bool collapsed = true;   ///< Show as single node (true) or expanded (false)
    std::string blueprint_path;  ///< Classname of nested blueprint type (e.g., "simple_battery")

    /// Group membership: which collapsed group this node belongs to.
    /// Empty string = root (top-level). "lamp1" = inside collapsed group "lamp1".
    std::string group_id;

    Pt pos;                  ///< Позиция (верхний левый угол)
    Pt size;                 ///< Размеры (ширина × высота)

    std::vector<Port> inputs;    ///< Входные порты
    std::vector<Port> outputs;   ///< Выходные порты

    /// Parameters (optional, for overriding component defaults)
    std::unordered_map<std::string, std::string> params;

    NodeContent node_content;  ///< Содержимое для отображения

    Node()
        : id()
        , name()
        , type_name()
        , render_hint()
        , expandable(false)
        , collapsed(true)
        , blueprint_path()
        , group_id()
        , pos(Pt::zero())
        , size(120.0f, 80.0f)
        , inputs()
        , outputs()
        , node_content()
    {}

    /// fluent: задать позицию
    Node& at(float x, float y) {
        pos = Pt(x, y);
        return *this;
    }

    /// fluent: задать размеры
    Node& size_wh(float w, float h) {
        size = Pt(w, h);
        return *this;
    }

    /// fluent: добавить входной порт
    Node& input(const char* name_, an24::PortType type = an24::PortType::V) {
        inputs.emplace_back(name_, PortSide::Input, type);
        return *this;
    }

    /// fluent: добавить выходной порт
    Node& output(const char* name_, an24::PortType type = an24::PortType::V) {
        outputs.emplace_back(name_, PortSide::Output, type);
        return *this;
    }

    /// fluent: задать содержимое
    Node& with_content(NodeContent c) {
        node_content = std::move(c);
        return *this;
    }
};

// =============================================================================
// Node size utility (single source of truth)
// =============================================================================

namespace an24 {
struct TypeDefinition;
struct TypeRegistry;
}  // namespace an24

/// Get default node size from type definition (single source of truth)
/// @param type_name Component classname (e.g., "Battery", "Splitter", "Bus", "RefNode")
/// @param registry Type registry to look up size from JSON definitions
/// @return Default size in pixels
inline Pt get_default_node_size(const std::string& type_name, const an24::TypeRegistry* registry) {
    constexpr float GRID_UNIT = 20.0f;  // 1 grid unit = 20 pixels

    // Try to get size from type definition
    if (registry) {
        const auto* def = registry->get(type_name);
        if (def && def->size.has_value()) {
            return Pt(def->size->first * GRID_UNIT,
                     def->size->second * GRID_UNIT);
        }
    }

    // Default fallback for regular nodes (types without size in JSON)
    return Pt(120, 80);
}

// [DRY-i9j0] Shared factory — was duplicated in app.cpp and persist.cpp
/// Create default NodeContent from a TypeDefinition (single source of truth)
inline NodeContent create_node_content_from_def(const an24::TypeDefinition* def) {
    NodeContent content;
    content.type = NodeContentType::None;
    if (!def) return content;

    const std::string& ct = def->content_type;
    if (ct == "Gauge") {
        content.type = NodeContentType::Gauge;
        content.label = "V";
        content.value = 0.0f;
        content.min = 0.0f;
        content.max = 30.0f;
        content.unit = "V";
    } else if (ct == "Switch") {
        content.type = NodeContentType::Switch;
        content.label = "ON";
        auto it = def->params.find("closed");
        content.state = (it != def->params.end() && it->second == "true");
    } else if (ct == "HoldButton") {
        content.type = NodeContentType::Switch;
        content.label = "RELEASED";
        content.state = false;
    } else if (ct == "Text") {
        content.type = NodeContentType::Text;
        content.label = "OFF";
    }
    return content;
}

