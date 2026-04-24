#pragma once

#include "../../blueprint_v2/blueprint/node_content_type.h"
#include "../../blueprint_v2/blueprint/blueprint.h"
#include "../../blueprint_v2/blueprint/node_port.h"
#include "core/model/component_registry.h"
#include "core/model/presentation_spec.h"
#include "editor/data/node_state.h"
#include "../../ui/math/pt.h"
#include "../../ui/core/interned_id.h"
#include <string>
#include <optional>
#include <cstdint>
#include <unordered_map>

/// Per-node display content payload consumed by widget rendering.
/// label and unit are string_view — must point to stable storage
/// (string literals, interner strings, or blueprint param strings).
struct NodeContent {
    bp2::NodeContentType type = bp2::NodeContentType::None;
    std::string_view label;
    float value = 0.0f;
    float min = 0.0f;
    float max = 1.0f;
    std::string_view unit;
    bool state = false;
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

namespace ui {
class StringInterner;
} // namespace ui

/// Get default node size from type definition (single source of truth)
/// @param type_name Component classname (e.g., "Battery", "Splitter", "Bus", "RefNode")
/// @param registry Type registry to look up size from JSON definitions
/// @return Default size in pixels
inline ui::Pt get_default_node_size(const std::string& type_name, const ComponentRegistry* registry) {
    constexpr float GRID_UNIT = 20.0f;  // 1 grid unit = 20 pixels

    // Try to get size from presentation spec
    if (registry) {
        const auto* pres = registry->get_presentation(type_name);
        if (pres && pres->default_size.has_value()) {
            return ui::Pt(pres->default_size->first * GRID_UNIT,
                     pres->default_size->second * GRID_UNIT);
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

/// Create NodeContent from ComponentSpec and instance params.
/// Dispatches on typed NodeContentType enum — zero string comparison.
/// Resolves param-driven content (min/max/positions/initial_position/closed)
/// using instance params first, then type definition defaults.
inline NodeContent create_node_content(const ComponentSpec& def,
                                       const TypePresentation* pres,
                                       const std::unordered_map<ui::InternedId, float>& instance_params,
                                       const std::unordered_map<std::string, std::string>& instance_string_params,
                                       ui::StringInterner& interner) {
    NodeContent content;
    const bp2::NodeContentType ct = pres ? pres->content_type : bp2::NodeContentType::None;
    content.type = ct;
    const auto& params = spec_params(def);

    switch (ct) {
        case bp2::NodeContentType::Gauge: {
            content.label = "V";
            content.value = 0.0f;

            auto min_it = params.find("min");
            float def_min = (min_it != params.end()) ? std::stof(min_it->second.default_value) : 0.0f;
            content.min = get_param_float_from_map(instance_params, instance_string_params, "min", interner, def_min);

            auto max_it = params.find("max");
            float def_max = (max_it != params.end()) ? std::stof(max_it->second.default_value) : 28.0f;
            content.max = get_param_float_from_map(instance_params, instance_string_params, "max", interner, def_max);

            content.unit = "V";
            break;
        }
        case bp2::NodeContentType::Switch: {
            content.label = "ON";
            auto it = params.find("closed");
            bool def_state = (it != params.end() && it->second.default_value == "true");
            content.state = get_param_bool_from_map(instance_params, instance_string_params, "closed", interner, def_state);
            break;
        }
        case bp2::NodeContentType::VerticalToggle: {
            content.label = "";
            auto it = params.find("closed");
            bool def_state = (it != params.end() && it->second.default_value == "true");
            content.state = get_param_bool_from_map(instance_params, instance_string_params, "closed", interner, def_state);
            break;
        }
        case bp2::NodeContentType::Text:
            content.label = "OFF";
            break;

        case bp2::NodeContentType::Slider: {
            content.value = 0.0f;

            auto min_it = params.find("min");
            float def_min = (min_it != params.end()) ? std::stof(min_it->second.default_value) : 0.0f;
            content.min = get_param_float_from_map(instance_params, instance_string_params, "min", interner, def_min);

            auto max_it = params.find("max");
            float def_max = (max_it != params.end()) ? std::stof(max_it->second.default_value) : 1.0f;
            content.max = get_param_float_from_map(instance_params, instance_string_params, "max", interner, def_max);
            break;
        }
        case bp2::NodeContentType::Indicator:
            content.value = 0.0f;
            break;

        case bp2::NodeContentType::Knob: {
            content.value = 0.0f;

            auto pos_it = params.find("positions");
            float def_positions = (pos_it != params.end()) ? std::stof(pos_it->second.default_value) : 2.0f;
            content.max = get_param_float_from_map(instance_params, instance_string_params, "positions", interner, def_positions);

            content.min = 0.0f;

            auto init_it = params.find("initial_position");
            float def_initial = (init_it != params.end()) ? std::stof(init_it->second.default_value) : 0.0f;
            content.value = get_param_float_from_map(instance_params, instance_string_params, "initial_position", interner, def_initial);
            break;
        }

        case bp2::NodeContentType::Value:
        case bp2::NodeContentType::None:
        case bp2::NodeContentType::Count:
            break;
    }

    return content;
}

/// Build the full runtime node content payload from canonical static semantics
/// plus the node's current dynamic runtime state.
inline NodeContent create_runtime_node_content(const bp2::Blueprint::Node& node,
                                               const ComponentSpec& def,
                                               const TypePresentation* pres,
                                               ui::StringInterner& interner,
                                               const editor::RuntimeNodeState* runtime_state = nullptr) {
    NodeContent content = create_node_content(def, pres, node.semantic.params, node.semantic.string_params, interner);

    if (runtime_state == nullptr) {
        return content;
    }

    std::visit([
        &content
    ](const auto& state) {
        using State = std::decay_t<decltype(state)>;
        if constexpr (std::is_same_v<State, editor::ScalarNodeRuntimeState>) {
            content.value = state.value;
        } else if constexpr (std::is_same_v<State, editor::BoolNodeRuntimeState>) {
            content.state = state.state;
        } else if constexpr (std::is_same_v<State, editor::DiscreteNodeRuntimeState>) {
            content.value = static_cast<float>(state.position);
        }
    }, *runtime_state);

    return content;
}
