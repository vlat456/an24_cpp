#pragma once

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
        case NodeContentType::Count:          return "None";
    }
    return "None";
}

} // namespace bp2
