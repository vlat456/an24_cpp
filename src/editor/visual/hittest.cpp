#include "visual/hittest.h"
#include "visual/spatial_grid.h"
#include "visual/trigonometry.h"
#include "visual/node/node.h"
#include "layout_constants.h"

HitResult hit_test(const Blueprint& bp, VisualNodeCache& cache, Pt world_pos,
                   const Viewport& vp, const std::string& group_id,
                   const editor_spatial::SpatialGrid& grid) {
    HitResult result;
    (void)vp;

    // --- Узлы ---
    {
        std::vector<size_t> candidates;
        candidates.reserve(8);
        grid.query_nodes(world_pos, 0.0f, candidates);
        for (size_t i : candidates) {
            if (i >= bp.nodes.size()) continue;
            const auto& n = bp.nodes[i];
            if (n.group_id != group_id) continue;
            auto* visual = cache.getOrCreate(n, bp.wires);
            if (visual->containsPoint(world_pos)) {
                result.type = HitType::Node;
                result.node_index = i;
                return result;
            }
        }
    }

    // --- Routing points и сегменты проводов ---
    {
        std::vector<size_t> candidates;
        candidates.reserve(8);
        float margin = std::max(editor_constants::ROUTING_POINT_HIT_RADIUS,
                                editor_constants::WIRE_SEGMENT_HIT_TOLERANCE);
        grid.query_wires(world_pos, margin, candidates);

        // Routing points — приоритет выше сегментов
        for (size_t wire_idx : candidates) {
            if (wire_idx >= bp.wires.size()) continue;
            const auto& w = bp.wires[wire_idx];
            const Node* sn = bp.find_node(w.start.node_id.c_str());
            const Node* en = bp.find_node(w.end.node_id.c_str());
            if (!sn || !en || sn->group_id != group_id || en->group_id != group_id) continue;
            for (size_t rp_idx = 0; rp_idx < w.routing_points.size(); rp_idx++) {
                if (editor_math::distance(world_pos, w.routing_points[rp_idx])
                        <= editor_constants::ROUTING_POINT_HIT_RADIUS) {
                    result.type = HitType::RoutingPoint;
                    result.wire_index = wire_idx;
                    result.routing_point_index = rp_idx;
                    return result;
                }
            }
        }

        // Сегменты
        for (size_t wire_idx : candidates) {
            if (wire_idx >= bp.wires.size()) continue;
            const auto& w = bp.wires[wire_idx];
            const Node* sn = bp.find_node(w.start.node_id.c_str());
            const Node* en = bp.find_node(w.end.node_id.c_str());
            if (!sn || !en || sn->group_id != group_id || en->group_id != group_id) continue;

            Pt start_pos = editor_math::get_port_position(
                *sn, w.start.port_name.c_str(), bp.wires, w.id.c_str(), cache);
            Pt end_pos = editor_math::get_port_position(
                *en, w.end.port_name.c_str(), bp.wires, w.id.c_str(), cache);

            Pt prev = start_pos;
            bool hit_found = false;
            for (const auto& rp : w.routing_points) {
                if (editor_math::distance_to_segment(world_pos, prev, rp)
                        < editor_constants::WIRE_SEGMENT_HIT_TOLERANCE) {
                    hit_found = true; break;
                }
                prev = rp;
            }
            if (!hit_found && editor_math::distance_to_segment(world_pos, prev, end_pos)
                    < editor_constants::WIRE_SEGMENT_HIT_TOLERANCE)
                hit_found = true;

            if (hit_found) {
                result.type = HitType::Wire;
                result.wire_index = wire_idx;
                return result;
            }
        }
    }

    return result;
}



// ============================================================================
// Port Hit Testing
// ============================================================================

namespace {
    constexpr float PORT_HIT_RADIUS = editor_constants::PORT_HIT_RADIUS;  // Радиус зоны клика порта
}

HitResult hit_test_ports(const Blueprint& bp, VisualNodeCache& cache, Pt world_pos,
                         const std::string& group_id,
                         const editor_spatial::SpatialGrid& grid) {
    HitResult result;
    const float PORT_HIT_RADIUS = editor_constants::PORT_HIT_RADIUS;

    std::vector<size_t> candidates;
    candidates.reserve(8);
    grid.query_nodes(world_pos, PORT_HIT_RADIUS, candidates);

    for (size_t node_idx : candidates) {
        if (node_idx >= bp.nodes.size()) continue;
        const auto& node = bp.nodes[node_idx];
        if (node.group_id != group_id) continue;

        auto* visual = cache.getOrCreate(node, bp.wires);
        if (!visual) continue;

        for (size_t port_idx = 0; port_idx < visual->getPortCount(); port_idx++) {
            const auto* port = visual->getPort(port_idx);
            if (!port) break;
            Pt port_pos = port->worldPosition();
            if (editor_math::distance(world_pos, port_pos) <= PORT_HIT_RADIUS) {
                result.type = HitType::Port;
                result.node_index = node_idx;
                result.port_index = port_idx;
                result.port_node_id = node.id;
                // Use logicalName() to get the target port ("v" for aliases, name for normal ports)
                result.port_name = port->logicalName();
                result.port_position = port_pos;

                // [g1h2i3j4] For Bus alias ports, store the wire ID so the caller
                // can identify which specific wire is connected to this port.
                if (port->isAlias()) {
                    result.port_wire_id = port->name();  // alias name IS the wire ID
                }

                // Determine port side from VisualPort (already computed correctly)
                result.port_side = port->side();

                return result;
            }
        }
    }
    return result;
}
