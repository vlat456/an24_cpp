#pragma once

#include <string>

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

    Port() : name(), side(PortSide::Input) {}
    Port(const char* name_, PortSide side_) : name(name_), side(side_) {}
};
