#pragma once

#include <string>
#include "../../ui/core/interned_id.h"
#include "../json_parser/json_parser.h"  // For PortType enum

/// Сторона порта на узле
enum class PortSide {
    Input,   ///< Входной порт (слева)
    Output,  ///< Выходной порт (справа)
    InOut    ///< Двунаправленный порт (может принимать и отдавать)
};

/// Порт узла - точка подключения проводов
struct EditorPort {
    ui::InternedId name;   ///< Имя порта (interned, e.g., "in", "out", "v_in")
    PortSide side;         ///< Сторона: вход или выход
    PortType type; ///< Тип порта для визуализации и валидации (NO default — must be set explicitly)

    EditorPort() : name(), side(PortSide::Input), type(PortType::Any) {}
    EditorPort(ui::InternedId name_, PortSide side_, PortType type_) : name(name_), side(side_), type(type_) {}
};
