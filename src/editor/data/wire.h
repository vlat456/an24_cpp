#pragma once

#include "pt.h"
#include "port.h"
#include <string>
#include <vector>
#include <functional>

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

/// [2.1] Dedup key for Wire — identifies a connection by its two endpoints.
/// Side is intentionally excluded: the connection identity is (node+port) × 2.
struct WireKey {
    std::string start_node;
    std::string start_port;
    std::string end_node;
    std::string end_port;

    explicit WireKey(const Wire& w)
        : start_node(w.start.node_id), start_port(w.start.port_name)
        , end_node(w.end.node_id), end_port(w.end.port_name) {}

    bool operator==(const WireKey& o) const {
        return start_node == o.start_node && start_port == o.start_port &&
               end_node == o.end_node && end_port == o.end_port;
    }
};

/// Hash for WireKey (combine 4 string hashes)
struct WireKeyHash {
    size_t operator()(const WireKey& k) const {
        std::hash<std::string> h;
        size_t seed = h(k.start_node);
        seed ^= h(k.start_port) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= h(k.end_node)   + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= h(k.end_port)   + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};
