#pragma once

#include "../../ui/math/pt.h"
#include "../../ui/core/interned_id.h"
#include "port.h"
#include <string>
#include <vector>
#include <functional>

/// Конец провода - ссылка на порт узла
struct WireEnd {
    ui::InternedId node_id;     ///< ID узла (interned)
    ui::InternedId port_name;   ///< Имя порта (interned)
    PortSide side;              ///< Сторона (input/output) - для совместимости

    WireEnd() : node_id(), port_name(), side(PortSide::Input) {}
    WireEnd(ui::InternedId node_, ui::InternedId port_, PortSide side_)
        : node_id(node_), port_name(port_), side(side_) {}
};

/// Провод - соединение между двумя портами
struct Wire {
    ui::InternedId id;                    ///< Уникальный ID (interned)
    WireEnd start;                        ///< Начало провода
    WireEnd end;                          ///< Конец провода

    /// Опциональные точки изгиба провода (для ручного размещения)
    std::vector<ui::Pt> routing_points;

    Wire() : id(), start(), end(), routing_points() {}

    /// Конструктор
    static Wire make(ui::InternedId id_, WireEnd start_, WireEnd end_) {
        Wire w;
        w.id = id_;
        w.start = std::move(start_);
        w.end = std::move(end_);
        return w;
    }

    /// fluent: добавить точку изгиба
    Wire& add_routing_point(ui::Pt pt) {
        routing_points.push_back(pt);
        return *this;
    }
};

/// Удобные конструкторы (InternedId version)
inline WireEnd wire_output(ui::InternedId node, ui::InternedId port) {
    return WireEnd(node, port, PortSide::Output);
}

inline WireEnd wire_input(ui::InternedId node, ui::InternedId port) {
    return WireEnd(node, port, PortSide::Input);
}

/// [2.1] Dedup key for Wire — identifies a connection by its two endpoints.
/// Side is intentionally excluded: the connection identity is (node+port) × 2.
/// Now 16 bytes (4 × InternedId) instead of ~128 bytes (4 × std::string).
struct WireKey {
    ui::InternedId start_node;
    ui::InternedId start_port;
    ui::InternedId end_node;
    ui::InternedId end_port;

    explicit WireKey(const Wire& w)
        : start_node(w.start.node_id), start_port(w.start.port_name)
        , end_node(w.end.node_id), end_port(w.end.port_name) {}

    bool operator==(const WireKey& o) const {
        return start_node == o.start_node && start_port == o.start_port &&
               end_node == o.end_node && end_port == o.end_port;
    }
};

/// Hash for WireKey (combine 4 uint32 hashes — trivial integer work)
struct WireKeyHash {
    size_t operator()(const WireKey& k) const {
        std::hash<uint32_t> h;
        size_t seed = h(k.start_node.raw());
        seed ^= h(k.start_port.raw()) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= h(k.end_node.raw())   + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= h(k.end_port.raw())   + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

/// Key for port occupancy index: identifies a single port on a node.
/// Now 8 bytes (2 × InternedId) instead of ~64 bytes (2 × std::string).
struct PortKey {
    ui::InternedId node_id;
    ui::InternedId port_name;

    PortKey() = default;
    PortKey(ui::InternedId node, ui::InternedId port)
        : node_id(node), port_name(port) {}

    bool operator==(const PortKey& o) const {
        return node_id == o.node_id && port_name == o.port_name;
    }
};

/// Hash for PortKey (combine 2 uint32 hashes — trivial integer work)
struct PortKeyHash {
    size_t operator()(const PortKey& k) const {
        std::hash<uint32_t> h;
        size_t seed = h(k.node_id.raw());
        seed ^= h(k.port_name.raw()) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};
