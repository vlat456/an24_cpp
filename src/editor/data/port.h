#pragma once

#include <string>
#include <optional>
#include "../../ui/core/interned_id.h"
#include "../json_parser/json_parser.h"  // For PortType enum

/// Сторона порта на узле
enum class PortSide {
    Input,   ///< Входной порт (слева)
    Output,  ///< Выходной порт (справа)
    InOut    ///< Двунаправленный порт (может принимать и отдавать)
};

/// Geometric side of a node where a port is rendered.
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
    if (s == "left")   return PortLayoutSide::Left;
    if (s == "right")  return PortLayoutSide::Right;
    if (s == "top")    return PortLayoutSide::Top;
    if (s == "bottom") return PortLayoutSide::Bottom;
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

/// Порт узла - точка подключения проводов
struct EditorPort {
    ui::InternedId name;   ///< Имя порта (interned, e.g., "in", "out", "v_in")
    PortSide side;         ///< Сторона: вход или выход
    PortType type; ///< Тип порта для визуализации и валидации (NO default — must be set explicitly)

    EditorPort() : name(), side(PortSide::Input), type(PortType::Any) {}
    EditorPort(ui::InternedId name_, PortSide side_, PortType type_) : name(name_), side(side_), type(type_) {}
};
