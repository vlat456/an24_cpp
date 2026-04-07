#pragma once

#include <optional>

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

} // namespace bp2
