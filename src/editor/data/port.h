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
    an24::PortType type = an24::PortType::Any;  ///< Тип порта для визуализации и валидации

    Port() : name(), side(PortSide::Input) {}
    Port(const char* name_, PortSide side_) : name(name_), side(side_) {}
    Port(const char* name_, PortSide side_, an24::PortType type_) : name(name_), side(side_), type(type_) {}
};
