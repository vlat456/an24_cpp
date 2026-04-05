#pragma once

#include "../../ui/math/pt.h"
#include "port.h"
#include <string>
#include <optional>
#include <cstdint>

/// Тип содержимого узла (пока простой enum)
enum class NodeContentType {
    None,
    Gauge,           ///< Измерительный прибор
    Switch,          ///< Кнопка-переключатель
    VerticalToggle,  ///< Вертикальный тумблер (слайдер)
    Value,           ///< Отображаемое значение
    Text,            ///< Текст
    Slider,          ///< Интерактивный слайдер с min/max
    Indicator,       ///< Индикатор (лампочка) - круг с яркостью
    Knob             ///< Поворотный переключатель (2-5 позиций)
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
    bool tripped = false;  ///< AZS thermal trip indicator (red button tint)
};

/// Optional per-node custom color (RGBA, 0.0–1.0)
struct NodeColor {
    float r = 0.5f, g = 0.5f, b = 0.5f, a = 1.0f;

    /// Convert to ImGui uint32 ABGR format (0xAABBGGRR)
    uint32_t to_uint32() const {
        auto clamp01 = [](float v) -> float {
            return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        };
        uint8_t ri = static_cast<uint8_t>(clamp01(r) * 255.0f + 0.5f);
        uint8_t gi = static_cast<uint8_t>(clamp01(g) * 255.0f + 0.5f);
        uint8_t bi = static_cast<uint8_t>(clamp01(b) * 255.0f + 0.5f);
        uint8_t ai = static_cast<uint8_t>(clamp01(a) * 255.0f + 0.5f);
        return (uint32_t(ai) << 24) | (uint32_t(bi) << 16) | (uint32_t(gi) << 8) | uint32_t(ri);
    }
};

/// Per-port layout override at the blueprint (instance) level.
struct PortLayoutOverride {
    std::string port_name;                    ///< Match by name (survives reordering)
    std::optional<PortLayoutSide> side;       ///< Override side (nullopt = use default)
    std::optional<uint8_t> position;          ///< Position hint within side (nullopt = auto-append)
    
    bool operator==(const PortLayoutOverride& other) const {
        return port_name == other.port_name &&
               side == other.side &&
               position == other.position;
    }
};

// =============================================================================
// Node size utility (single source of truth)
// =============================================================================

struct TypeDefinition;
struct TypeRegistry;

/// Get default node size from type definition (single source of truth)
/// @param type_name Component classname (e.g., "Battery", "Splitter", "Bus", "RefNode")
/// @param registry Type registry to look up size from JSON definitions
/// @return Default size in pixels
inline ui::Pt get_default_node_size(const std::string& type_name, const TypeRegistry* registry) {
    constexpr float GRID_UNIT = 20.0f;  // 1 grid unit = 20 pixels

    // Try to get size from type definition
    if (registry) {
        const auto* def = registry->get(type_name);
        if (def && def->size.has_value()) {
            return ui::Pt(def->size->first * GRID_UNIT,
                     def->size->second * GRID_UNIT);
        }
    }

    // Default fallback for regular nodes (types without size in JSON)
    return ui::Pt(120, 80);
}

// [DRY-i9j0] Shared factory — was duplicated in app.cpp and persist.cpp
/// Create default NodeContent from a TypeDefinition (single source of truth)
inline NodeContent create_node_content_from_def(const TypeDefinition* def) {
    NodeContent content;
    content.type = NodeContentType::None;
    if (!def) return content;

    const std::string& ct = def->content_type;
    if (ct == "Gauge") {
        content.type = NodeContentType::Gauge;
        content.label = "V";
        content.value = 0.0f;
        auto min_it = def->params.find("min");
        content.min = (min_it != def->params.end()) ? std::stof(min_it->second) : 0.0f;
        auto max_it = def->params.find("max");
        content.max = (max_it != def->params.end()) ? std::stof(max_it->second) : 28.0f;
        content.unit = "V";
    } else if (ct == "Switch") {
        content.type = NodeContentType::Switch;
        content.label = "ON";
        auto it = def->params.find("closed");
        content.state = (it != def->params.end() && it->second == "true");
    } else if (ct == "VerticalToggle") {
        content.type = NodeContentType::VerticalToggle;
        content.label = "";
        auto it = def->params.find("closed");
        content.state = (it != def->params.end() && it->second == "true");
    } else if (ct == "HoldButton") {
        content.type = NodeContentType::Switch;
        content.label = "RELEASED";
        content.state = false;
    } else if (ct == "Text") {
        content.type = NodeContentType::Text;
        content.label = "OFF";
    } else if (ct == "Slider") {
        content.type = NodeContentType::Slider;
        content.value = 0.0f;
        auto min_it = def->params.find("min");
        content.min = (min_it != def->params.end()) ? std::stof(min_it->second) : 0.0f;
        auto max_it = def->params.find("max");
        content.max = (max_it != def->params.end()) ? std::stof(max_it->second) : 1.0f;
    } else if (ct == "Indicator") {
        content.type = NodeContentType::Indicator;
        content.value = 0.0f;  // normalized brightness (0-1)
    } else if (ct == "Knob") {
        content.type = NodeContentType::Knob;
        content.value = 0.0f;  // current position (0-based)
        auto pos_it = def->params.find("positions");
        content.max = (pos_it != def->params.end()) ? std::stof(pos_it->second) : 2.0f;
        content.min = 0.0f;
        auto init_it = def->params.find("initial_position");
        if (init_it != def->params.end()) {
            content.value = std::stof(init_it->second);
        }
    }
    return content;
}
