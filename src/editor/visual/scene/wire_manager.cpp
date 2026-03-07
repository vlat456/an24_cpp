#include "visual/scene/wire_manager.h"
#include "visual/scene/scene.h"
#include "visual/port/port.h"
#include "visual/trigonometry.h"
#include "router/router.h"
#include "data/node.h"
#include <limits>

// ---- Queries ----

Pt WireManager::wireEndPosition(const Wire& wire, bool start) const {
    const auto& end = start ? wire.start : wire.end;
    const Node* node = scene_.findNode(end.node_id.c_str());
    if (!node) return Pt::zero();
    return scene_.portPosition(*node, end.port_name.c_str(), wire.id.c_str());
}

std::vector<Pt> WireManager::wirePolyline(const Wire& wire) const {
    std::vector<Pt> poly;
    poly.push_back(wireEndPosition(wire, true));
    poly.insert(poly.end(), wire.routing_points.begin(), wire.routing_points.end());
    poly.push_back(wireEndPosition(wire, false));
    return poly;
}

std::optional<WirePortMatch> WireManager::findWireOnPort(const HitResult& port_hit) const {
    // Determine if the port belongs to a Bus node (for special matching).
    NodeKind port_node_kind = NodeKind::Node;
    const Node* hit_node = scene_.findNode(port_hit.port_node_id.c_str());
    if (hit_node) port_node_kind = hit_node->kind;

    const auto& wires = scene_.wires();
    for (size_t wi = 0; wi < wires.size(); wi++) {
        const auto& w = wires[wi];

        // Bus alias port → match by wire ID; Bus main "v" port → skip entirely.
        bool match_start = false;
        bool match_end = false;
        if (!port_hit.port_wire_id.empty()) {
            match_start = (w.id == port_hit.port_wire_id &&
                           w.start.node_id == port_hit.port_node_id);
            match_end   = (w.id == port_hit.port_wire_id &&
                           w.end.node_id == port_hit.port_node_id);
        } else if (port_node_kind != NodeKind::Bus) {
            match_start = (w.start.node_id == port_hit.port_node_id &&
                           w.start.port_name == port_hit.port_name);
            match_end   = (w.end.node_id == port_hit.port_node_id &&
                           w.end.port_name == port_hit.port_name);
        }

        if (match_start || match_end) {
            bool detach_start = match_start;

            // Anchor = nearest routing point to the detached end, or the far port.
            Pt anchor_pos;
            PortSide fixed_side;
            if (detach_start) {
                fixed_side = w.end.side;
                anchor_pos = !w.routing_points.empty()
                    ? w.routing_points.front()
                    : wireEndPosition(w, false);
            } else {
                fixed_side = w.start.side;
                anchor_pos = !w.routing_points.empty()
                    ? w.routing_points.back()
                    : wireEndPosition(w, true);
            }

            return WirePortMatch{wi, detach_start, anchor_pos, fixed_side};
        }
    }
    return std::nullopt;
}

bool WireManager::canConnect(PortSide a, PortSide b) {
    return VisualPort::areSidesCompatible(a, b);
}

bool WireManager::isSamePort(const std::string& node_a, const std::string& port_a,
                             const std::string& node_b, const std::string& port_b) {
    return node_a == node_b && port_a == port_b;
}

// ---- Mutations ----

bool WireManager::routeWire(size_t wire_idx) {
    auto& wires = scene_.wires();
    if (wire_idx >= wires.size()) return false;
    Wire& wire = wires[wire_idx];

    const Node* start_node = scene_.findNode(wire.start.node_id.c_str());
    const Node* end_node   = scene_.findNode(wire.end.node_id.c_str());
    if (!start_node || !end_node) return false;

    Pt start_pos = scene_.portPosition(*start_node, wire.start.port_name.c_str(),
                                       wire.id.c_str());
    Pt end_pos   = scene_.portPosition(*end_node, wire.end.port_name.c_str(),
                                       wire.id.c_str());

    auto existing_paths = buildExistingPaths(wire_idx);

    auto path = route_around_nodes(
        start_pos, end_pos,
        *start_node, wire.start.port_name.c_str(),
        *end_node, wire.end.port_name.c_str(),
        scene_.nodes(), scene_.gridStep(),
        existing_paths);

    if (path.empty()) return false;

    // Strip first and last points (port positions, not routing points).
    if (path.size() > 2) {
        wire.routing_points.assign(path.begin() + 1, path.end() - 1);
    } else {
        wire.routing_points.clear();
    }
    return true;
}

void WireManager::addRoutingPoint(size_t wire_idx, Pt world_pos) {
    auto& wires = scene_.wires();
    if (wire_idx >= wires.size()) return;
    auto& wire = wires[wire_idx];

    Pt start_pos = wireEndPosition(wire, true);
    Pt end_pos   = wireEndPosition(wire, false);

    // Find nearest segment along the polyline.
    size_t insert_idx = 0;
    float min_dist = std::numeric_limits<float>::max();

    Pt prev = start_pos;
    for (size_t i = 0; i <= wire.routing_points.size(); i++) {
        Pt next = (i < wire.routing_points.size()) ? wire.routing_points[i] : end_pos;
        float dist = editor_math::distance_to_segment(world_pos, prev, next);
        if (dist < min_dist) {
            min_dist = dist;
            insert_idx = i;
        }
        prev = next;
    }

    Pt snapped = editor_math::snap_to_grid(world_pos, scene_.gridStep());
    wire.routing_points.insert(wire.routing_points.begin() + static_cast<long>(insert_idx), snapped);
}

void WireManager::removeRoutingPoint(size_t wire_idx, size_t rp_idx) {
    auto& wires = scene_.wires();
    if (wire_idx >= wires.size()) return;
    auto& wire = wires[wire_idx];
    if (rp_idx >= wire.routing_points.size()) return;
    wire.routing_points.erase(wire.routing_points.begin() + static_cast<long>(rp_idx));
}

// ---- Private ----

std::vector<std::vector<Pt>> WireManager::buildExistingPaths(size_t exclude_wire_idx) const {
    std::vector<std::vector<Pt>> paths;
    const auto& wires = scene_.wires();
    for (size_t i = 0; i < wires.size(); i++) {
        if (i == exclude_wire_idx) continue;
        paths.push_back(wirePolyline(wires[i]));
    }
    return paths;
}
