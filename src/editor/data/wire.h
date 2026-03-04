#pragma once

#include "pt.h"
#include "port.h"
#include <string>
#include <vector>

/// Конец провода - ссылка на порт узла
struct WireEnd {
    std::string node_id;     ///< ID узла
    std::string port_name;   ///< Имя порта
    PortSide side;           ///< Сторона (input/output) - для совместимости

    WireEnd() : node_id(), port_name(), side(PortSide::Input) {}
    WireEnd(const char* node_, const char* port_, PortSide side_)
        : node_id(node_), port_name(port_), side(side_) {}
};

/// Провод - соединение между двумя портами
struct Wire {
    std::string id;                       ///< Уникальный ID
    WireEnd start;                        ///< Начало провода
    WireEnd end;                          ///< Конец провода

    /// Опциональные точки изгиба провода (для ручного размещения)
    std::vector<Pt> routing_points;

    Wire() : id(), start(), end(), routing_points() {}

    /// Конструктор
    static Wire make(const char* id_, WireEnd start_, WireEnd end_) {
        Wire w;
        w.id = id_;
        w.start = std::move(start_);
        w.end = std::move(end_);
        return w;
    }

    /// fluent: добавить точку изгиба
    Wire& add_routing_point(Pt pt) {
        routing_points.push_back(pt);
        return *this;
    }
};

/// Удобные конструкторы
inline WireEnd wire_output(const char* node, const char* port) {
    return WireEnd(node, port, PortSide::Output);
}

inline WireEnd wire_input(const char* node, const char* port) {
    return WireEnd(node, port, PortSide::Input);
}
