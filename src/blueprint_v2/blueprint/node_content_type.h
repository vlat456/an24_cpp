#pragma once

#include <optional>
#include <string_view>

namespace bp2 {

/// Node content type for simulation readout / interactive widgets.
enum class NodeContentType {
    None,
    Gauge,
    Switch,
    VerticalToggle,
    Value,
    Text,
    Slider,
    Indicator,
    Knob,
    Count
};

inline constexpr bool is_valid_node_content_type(int value) {
    return value >= 0 && value < static_cast<int>(NodeContentType::Count);
}

inline std::optional<NodeContentType> node_content_type_from_int(int value) {
    switch (value) {
        case 0: return NodeContentType::None;
        case 1: return NodeContentType::Gauge;
        case 2: return NodeContentType::Switch;
        case 3: return NodeContentType::VerticalToggle;
        case 4: return NodeContentType::Value;
        case 5: return NodeContentType::Text;
        case 6: return NodeContentType::Slider;
        case 7: return NodeContentType::Indicator;
        case 8: return NodeContentType::Knob;
        default: return std::nullopt;
    }
}

/// Parse a content_type string (from JSON type definition) to enum.
/// Returns None for unknown values — the serialization boundary is the
/// only place strings should appear for content type identity.
inline constexpr NodeContentType parse_node_content_type(std::string_view s) {
    if (s == "Gauge")          return NodeContentType::Gauge;
    if (s == "Switch")         return NodeContentType::Switch;
    if (s == "VerticalToggle") return NodeContentType::VerticalToggle;
    if (s == "Value")          return NodeContentType::Value;
    if (s == "Text")           return NodeContentType::Text;
    if (s == "Slider")         return NodeContentType::Slider;
    if (s == "Indicator")      return NodeContentType::Indicator;
    if (s == "Knob")           return NodeContentType::Knob;
    if (s == "Azs")            return NodeContentType::Switch;   // AZS renders as Switch
    if (s == "HoldButton")     return NodeContentType::Switch;   // HoldButton renders as Switch
    return NodeContentType::None;
}

/// Inverse: enum → string (for serialization).
inline constexpr const char* node_content_type_to_string(NodeContentType t) {
    switch (t) {
        case NodeContentType::None:           return "None";
        case NodeContentType::Gauge:          return "Gauge";
        case NodeContentType::Switch:         return "Switch";
        case NodeContentType::VerticalToggle: return "VerticalToggle";
        case NodeContentType::Value:          return "Value";
        case NodeContentType::Text:           return "Text";
        case NodeContentType::Slider:         return "Slider";
        case NodeContentType::Indicator:      return "Indicator";
        case NodeContentType::Knob:           return "Knob";
        case NodeContentType::Azs:            return "Azs";
        case NodeContentType::Count:          return "None";
    }
    return "None";
}

} // namespace bp2
