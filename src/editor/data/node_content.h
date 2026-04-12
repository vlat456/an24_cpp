#pragma once

#include "../../blueprint_v2/blueprint/node_content_type.h"
#include "../../blueprint_v2/blueprint/node_port.h"
#include "../../ui/math/pt.h"
#include "../../ui/core/interned_id.h"
#include <string>
#include <optional>
#include <cstdint>
#include <unordered_map>

/// Содержимое узла (пока placeholder)
struct NodeContent {
    bp2::NodeContentType type = bp2::NodeContentType::None;
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
    std::optional<bp2::PortLayoutSide> side;  ///< Override side (nullopt = use default)
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

namespace ui {
class StringInterner;
} // namespace ui

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

/// [Issue #132] Helper to resolve param value from instance params map or defaults
/// Lookup order: params_map[key] → fallback
inline float get_param_float_from_map(const std::unordered_map<ui::InternedId, float>& params,
                                      const std::unordered_map<std::string, std::string>& string_params,
                                      const std::string& key,
                                      ui::StringInterner& interner,
                                      float fallback = 0.0f) {
    // Try numeric params first
    const ui::InternedId key_iid = interner.intern(key);
    auto it = params.find(key_iid);
    if (it != params.end()) {
        return it->second;
    }
    
    // Try string params (stof may fail, fallback in that case)
    auto sit = string_params.find(key);
    if (sit != string_params.end()) {
        try {
            return std::stof(sit->second);
        } catch (...) {
            // stof failed, use fallback
        }
    }
    
    return fallback;
}

/// [Issue #132] Helper to resolve param bool value from instance params or defaults
inline bool get_param_bool_from_map(const std::unordered_map<ui::InternedId, float>& params,
                                    const std::unordered_map<std::string, std::string>& string_params,
                                    const std::string& key,
                                    ui::StringInterner& interner,
                                    bool fallback = false) {
    // Try numeric params (0 = false, != 0 = true)
    const ui::InternedId key_iid = interner.intern(key);
    auto it = params.find(key_iid);
    if (it != params.end()) {
        return it->second != 0.0f;
    }
    
    // Try string params
    auto sit = string_params.find(key);
    if (sit != string_params.end()) {
        return sit->second == "true" || sit->second == "1";
    }
    
    return fallback;
}

/// [Issue #132] Create NodeContent from TypeDefinition and instance params
/// Resolves param-driven content (min/max/positions/initial_position/closed)
/// using instance params first, then type definition defaults.
inline NodeContent create_node_content(const TypeDefinition* def,
                                       const std::unordered_map<ui::InternedId, float>& instance_params,
                                       const std::unordered_map<std::string, std::string>& instance_string_params,
                                       ui::StringInterner& interner) {
    NodeContent content;
    content.type = bp2::NodeContentType::None;
    if (!def) return content;

    const std::string& ct = def->content_type;
    if (ct == "Gauge") {
        content.type = bp2::NodeContentType::Gauge;
        content.label = "V";
        content.value = 0.0f;
        
        // Resolve min/max from instance params, then type definition
        auto min_it = def->params.find("min");
        float def_min = (min_it != def->params.end()) ? std::stof(min_it->second) : 0.0f;
        content.min = get_param_float_from_map(instance_params, instance_string_params, "min", interner, def_min);
        
        auto max_it = def->params.find("max");
        float def_max = (max_it != def->params.end()) ? std::stof(max_it->second) : 28.0f;
        content.max = get_param_float_from_map(instance_params, instance_string_params, "max", interner, def_max);
        
        content.unit = "V";
    } else if (ct == "Switch") {
        content.type = bp2::NodeContentType::Switch;
        content.label = "ON";
        auto it = def->params.find("closed");
        bool def_state = (it != def->params.end() && it->second == "true");
        content.state = get_param_bool_from_map(instance_params, instance_string_params, "closed", interner, def_state);
    } else if (ct == "VerticalToggle") {
        content.type = bp2::NodeContentType::VerticalToggle;
        content.label = "";
        auto it = def->params.find("closed");
        bool def_state = (it != def->params.end() && it->second == "true");
        content.state = get_param_bool_from_map(instance_params, instance_string_params, "closed", interner, def_state);
    } else if (ct == "HoldButton") {
        content.type = bp2::NodeContentType::Switch;
        content.label = "RELEASED";
        content.state = false;
    } else if (ct == "Text") {
        content.type = bp2::NodeContentType::Text;
        content.label = "OFF";
    } else if (ct == "Slider") {
        content.type = bp2::NodeContentType::Slider;
        content.value = 0.0f;
        
        // Resolve min/max from instance params, then type definition
        auto min_it = def->params.find("min");
        float def_min = (min_it != def->params.end()) ? std::stof(min_it->second) : 0.0f;
        content.min = get_param_float_from_map(instance_params, instance_string_params, "min", interner, def_min);
        
        auto max_it = def->params.find("max");
        float def_max = (max_it != def->params.end()) ? std::stof(max_it->second) : 1.0f;
        content.max = get_param_float_from_map(instance_params, instance_string_params, "max", interner, def_max);
    } else if (ct == "Indicator") {
        content.type = bp2::NodeContentType::Indicator;
        content.value = 0.0f;  // normalized brightness (0-1)
    } else if (ct == "Knob") {
        content.type = bp2::NodeContentType::Knob;
        content.value = 0.0f;  // current position (0-based)
        
        // Resolve positions from instance params, then type definition
        auto pos_it = def->params.find("positions");
        float def_positions = (pos_it != def->params.end()) ? std::stof(pos_it->second) : 2.0f;
        content.max = get_param_float_from_map(instance_params, instance_string_params, "positions", interner, def_positions);
        
        content.min = 0.0f;
        
        // Resolve initial_position from instance params, then type definition
        auto init_it = def->params.find("initial_position");
        float def_initial = (init_it != def->params.end()) ? std::stof(init_it->second) : 0.0f;
        content.value = get_param_float_from_map(instance_params, instance_string_params, "initial_position", interner, def_initial);
    }
    return content;
}
