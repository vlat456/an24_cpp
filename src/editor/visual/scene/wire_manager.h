#pragma once

#include "data/pt.h"
#include "data/wire.h"
#include "data/port.h"
#include "visual/hittest.h"
#include <optional>
#include <string>
#include <vector>

class VisualScene;

/// Result of finding which wire connects to a given port.
struct WirePortMatch {
    size_t wire_index;
    bool detach_start;   // true = wire's start end connects to the hit port
    Pt anchor_pos;       // position of the nearest routing point (or far port)
    PortSide fixed_side; // side of the OTHER (non-detached) end
};

/// Manages wire queries and mutations. Non-owning reference to VisualScene.
/// Single source of truth for wire topology, routing, and compatibility.
class WireManager {
public:
    explicit WireManager(VisualScene& scene) : scene_(scene) {}

    // ---- Queries ----

    /// World position of a wire endpoint (start or end port).
    Pt wireEndPosition(const Wire& wire, bool start) const;

    /// Full polyline: [start_pos, ...routing_points..., end_pos].
    std::vector<Pt> wirePolyline(const Wire& wire) const;

    /// Find wire connected to a port hit result. Bus-aware.
    /// Returns nullopt if no wire connects to the hit port.
    std::optional<WirePortMatch> findWireOnPort(const HitResult& port_hit) const;

    /// Can two port sides be connected? Delegates to VisualPort::areSidesCompatible.
    static bool canConnect(PortSide a, PortSide b);

    /// Are two port identities the same port?
    static bool isSamePort(const std::string& node_a, const std::string& port_a,
                           const std::string& node_b, const std::string& port_b);

    // ---- Mutations ----

    /// Run A* routing for the given wire, updating its routing_points.
    /// Returns true if a path was found.
    bool routeWire(size_t wire_idx);

    /// Insert a routing point on the nearest segment of the wire.
    void addRoutingPoint(size_t wire_idx, Pt world_pos);

    /// Remove a routing point by index.
    void removeRoutingPoint(size_t wire_idx, size_t rp_idx);

private:
    /// Build polylines for all wires except the given index (for A* avoidance).
    std::vector<std::vector<Pt>> buildExistingPaths(size_t exclude_wire_idx) const;

    VisualScene& scene_;
};
