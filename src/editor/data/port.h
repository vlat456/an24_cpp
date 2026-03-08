#pragma once

#include <string>
#include "../json_parser/json_parser.h"  // For PortType enum

/// Сторона порта на узле
enum class PortSide {
    Input,   ///< Входной порт (слева)
    Output,  ///< Выходной порт (справа)
    InOut    ///< Двунаправленный порт (может принимать и отдавать)
};

/// Порт узла - точка подключения проводов
struct Port {
    std::string name;   ///< Имя порта (e.g., "in", "out", "v_in")
    PortSide side;      ///< Сторона: вход или выход
    an24::PortType type; ///< Тип порта для визуализации и валидации (NO default — must be set explicitly)

    Port() : name(), side(PortSide::Input), type(an24::PortType::Any) {}
    Port(const char* name_, PortSide side_, an24::PortType type_) : name(name_), side(side_), type(type_) {}
};
