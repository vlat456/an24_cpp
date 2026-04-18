#pragma once

#include "blueprint_v2/interface/direction.h"
#include "ui/core/interned_id.h"
#include "core/domain_types.h"
#include <cstdint>
#include <optional>
#include <string>

namespace bp2 {

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

inline PortLayoutSide default_layout_side(Direction direction) {
    switch (direction) {
        case Direction::Input:  return PortLayoutSide::Left;
        case Direction::Output: return PortLayoutSide::Right;
        case Direction::InOut:  return PortLayoutSide::Left;
    }
    return PortLayoutSide::Left;
}

struct NodePort {
    ui::InternedId name;
    Direction direction;
    PortType type;

    NodePort() : name(), direction(Direction::Input), type(PortType::Any) {}
    NodePort(ui::InternedId name_, Direction direction_, PortType type_)
        : name(name_), direction(direction_), type(type_) {}

    bool operator==(const NodePort& o) const {
        return name == o.name && direction == o.direction && type == o.type;
    }
};

} // namespace bp2
