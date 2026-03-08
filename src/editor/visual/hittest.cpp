#include "visual/hittest.h"
#include "visual/trigonometry.h"
#include "visual/node/node.h"
#include "layout_constants.h"

// [h1a2b3c4] Primary overload: use VisualNodeCache for consistent hit testing
HitResult hit_test(const Blueprint& bp, VisualNodeCache& cache, Pt world_pos, const Viewport& vp,
                   const std::string& group_id) {
    HitResult result;
    (void)vp;

    // Check nodes belonging to this group
    for (size_t i = 0; i < bp.nodes.size(); i++) {
        const auto& n = bp.nodes[i];
        if (n.group_id != group_id) continue;
        auto* visual = cache.getOrCreate(n, bp.wires);
        if (visual->containsPoint(world_pos)) {
            result.type = HitType::Node;
            result.node_index = i;
            return result;
        }
    }

    // Check routing points on wires belonging to this group
    for (size_t wire_idx = 0; wire_idx < bp.wires.size(); wire_idx++) {
        const auto& w = bp.wires[wire_idx];
        // Skip wires whose endpoints are not in this group
        const Node* sn = bp.find_node(w.start.node_id.c_str());
        const Node* en = bp.find_node(w.end.node_id.c_str());
        if (!sn || !en || sn->group_id != group_id || en->group_id != group_id) continue;

        for (size_t rp_idx = 0; rp_idx < w.routing_points.size(); rp_idx++) {
            const Pt& rp = w.routing_points[rp_idx];
            float dist = editor_math::distance(world_pos, rp);
            if (dist <= editor_constants::ROUTING_POINT_HIT_RADIUS) {
                result.type = HitType::RoutingPoint;
                result.wire_index = wire_idx;
                result.routing_point_index = rp_idx;
                return result;
            }
        }
    }

    // [i2d4e6f8] Wire segment hit tolerance
    for (size_t i = 0; i < bp.wires.size(); i++) {
        const auto& w = bp.wires[i];

        const Node* start_node = bp.find_node(w.start.node_id.c_str());
        const Node* end_node = bp.find_node(w.end.node_id.c_str());
        if (!start_node || !end_node) continue;
        if (start_node->group_id != group_id || end_node->group_id != group_id) continue;

        // [p1q2r3s4] Pass wire ID so Bus alias ports resolve to correct positions
        // (matching render.cpp which also passes w.id). Without wire_id, Bus nodes
        // return the main "v" port position for all wires → hit-test misses.
        Pt start_pos = editor_math::get_port_position(*start_node, w.start.port_name.c_str(), bp.wires,
                                                       w.id.c_str(), cache);
        Pt end_pos = editor_math::get_port_position(*end_node, w.end.port_name.c_str(), bp.wires,
                                                     w.id.c_str(), cache);

        Pt prev = start_pos;
        for (const auto& rp : w.routing_points) {
            float dist = editor_math::distance_to_segment(world_pos, prev, rp);
            if (dist < editor_constants::WIRE_SEGMENT_HIT_TOLERANCE) {
                result.type = HitType::Wire;
                result.wire_index = i;
                return result;
            }
            prev = rp;
        }
        float dist = editor_math::distance_to_segment(world_pos, prev, end_pos);
        if (dist < editor_constants::WIRE_SEGMENT_HIT_TOLERANCE) {
            result.type = HitType::Wire;
            result.wire_index = i;
            return result;
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
                         const std::string& group_id) {
    HitResult result;

    // Check ports of nodes belonging to this group
    for (size_t node_idx = 0; node_idx < bp.nodes.size(); node_idx++) {
        const auto& node = bp.nodes[node_idx];
        if (node.group_id != group_id) continue;

        // Always call getOrCreate with wires to ensure BusVisualNode has dynamic ports
        auto* visual = cache.getOrCreate(node, bp.wires);
        if (!visual) continue;

        // Проверяем все порты
        for (size_t port_idx = 0; port_idx < visual->getPortCount(); port_idx++) {
            const auto* port = visual->getPort(port_idx);
            if (!port) break;

            Pt port_pos = port->worldPosition();
            float dist = editor_math::distance(world_pos, port_pos);
            if (dist <= PORT_HIT_RADIUS) {
                result.type = HitType::Port;
                result.node_index = node_idx;
                result.port_index = port_idx;
                // BUGFIX [c5a1d8] Removed dead is_input field
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
