#pragma once

#include "ui/core/interned_id.h"
#include "json_parser/json_parser.h"
#include <cstdint>
#include <optional>
#include <string>

namespace bp2 {

enum class PortSide {
    Input,
    Output,
    InOut
};

enum class PortLayoutSide : uint8_t {
    Left,
    Right,
    Top,
    Bottom
};

inline const char* port_layout_side_to_string(PortLayoutSide s) {
    switch (s) {
        case PortLayoutSide::Left:   return "left";
        case PortLayoutSide::Right:  return "right";
        case PortLayoutSide::Top:    return "top";
        case PortLayoutSide::Bottom: return "bottom";
    }
    return "left";
}

inline std::optional<PortLayoutSide> parse_port_layout_side(const std::string& s) {
    if (s == "left") {
        return PortLayoutSide::Left;
    }
    if (s == "right") {
        return PortLayoutSide::Right;
    }
    if (s == "top") {
        return PortLayoutSide::Top;
    }
    if (s == "bottom") {
        return PortLayoutSide::Bottom;
    }
    return std::nullopt;
}

inline PortLayoutSide default_layout_side(PortSide side) {
    switch (side) {
        case PortSide::Input:  return PortLayoutSide::Left;
        case PortSide::Output: return PortLayoutSide::Right;
        case PortSide::InOut:  return PortLayoutSide::Left;
    }
    return PortLayoutSide::Left;
}

struct NodePort {
    ui::InternedId name;
    PortSide side;
    PortType type;

    NodePort() : name(), side(PortSide::Input), type(PortType::Any) {}
    NodePort(ui::InternedId name_, PortSide side_, PortType type_)
        : name(name_), side(side_), type(type_) {}

    bool operator==(const NodePort& o) const {
        return name == o.name && side == o.side && type == o.type;
    }
};

} // namespace bp2
