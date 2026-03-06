#include "hittest.h"
#include "trigonometry.h"
#include "visual_node.h"

// [h1a2b3c4] Primary overload: use VisualNodeCache for consistent hit testing
HitResult hit_test(const Blueprint& bp, VisualNodeCache& cache, Pt world_pos, const Viewport& vp) {
    HitResult result;
    (void)vp;

    // Сначала проверяем узлы через cache
    for (size_t i = 0; i < bp.nodes.size(); i++) {
        const auto& n = bp.nodes[i];
        auto* visual = cache.getOrCreate(n, bp.wires);
        if (visual->containsPoint(world_pos)) {
            result.type = HitType::Node;
            result.node_index = i;
            return result;
        }
    }

    // Потом проверяем routing points (чтобы можно было их выбирать и dragить)
    constexpr float ROUTING_POINT_HIT_RADIUS = 10.0f;
    for (size_t wire_idx = 0; wire_idx < bp.wires.size(); wire_idx++) {
        const auto& w = bp.wires[wire_idx];
        for (size_t rp_idx = 0; rp_idx < w.routing_points.size(); rp_idx++) {
            const Pt& rp = w.routing_points[rp_idx];
            float dist = editor_math::distance(world_pos, rp);
            if (dist <= ROUTING_POINT_HIT_RADIUS) {
                result.type = HitType::RoutingPoint;
                result.wire_index = wire_idx;
                result.routing_point_index = rp_idx;
                return result;
            }
        }
    }

    // [i2d4e6f8] Wire segment hit tolerance unified to 5.0f (was 20.0f for routing, 5.0f for final).
    constexpr float WIRE_HIT_TOLERANCE = 5.0f;

    // Потом проверяем провода
    for (size_t i = 0; i < bp.wires.size(); i++) {
        const auto& w = bp.wires[i];

        const Node* start_node = nullptr;
        const Node* end_node = nullptr;

        for (const auto& n : bp.nodes) {
            if (n.id == w.start.node_id) start_node = &n;
            if (n.id == w.end.node_id) end_node = &n;
        }

        if (!start_node || !end_node) continue;

        // [p1q2r3s4] Pass wire ID so Bus alias ports resolve to correct positions
        // (matching render.cpp which also passes w.id). Without wire_id, Bus nodes
        // return the main "v" port position for all wires → hit-test misses.
        Pt start_pos = editor_math::get_port_position(*start_node, w.start.port_name.c_str(), bp.wires,
                                                       w.id.c_str(), &cache);
        Pt end_pos = editor_math::get_port_position(*end_node, w.end.port_name.c_str(), bp.wires,
                                                     w.id.c_str(), &cache);

        Pt prev = start_pos;
        for (const auto& rp : w.routing_points) {
            float dist = editor_math::distance_to_segment(world_pos, prev, rp);
            if (dist < WIRE_HIT_TOLERANCE) {
                result.type = HitType::Wire;
                result.wire_index = i;
                return result;
            }
            prev = rp;
        }
        float dist = editor_math::distance_to_segment(world_pos, prev, end_pos);
        if (dist < WIRE_HIT_TOLERANCE) {
            result.type = HitType::Wire;
            result.wire_index = i;
            return result;
        }
    }

    return result;
}

// Legacy overload without cache — creates fresh visuals every call
HitResult hit_test(const Blueprint& bp, Pt world_pos, const Viewport& vp) {
    HitResult result;
    (void)vp;

    for (size_t i = 0; i < bp.nodes.size(); i++) {
        const auto& n = bp.nodes[i];
        auto visual = VisualNodeFactory::create(n, bp.wires);
        if (visual->containsPoint(world_pos)) {
            result.type = HitType::Node;
            result.node_index = i;
            return result;
        }
    }

    constexpr float ROUTING_POINT_HIT_RADIUS = 10.0f;
    for (size_t wire_idx = 0; wire_idx < bp.wires.size(); wire_idx++) {
        const auto& w = bp.wires[wire_idx];
        for (size_t rp_idx = 0; rp_idx < w.routing_points.size(); rp_idx++) {
            const Pt& rp = w.routing_points[rp_idx];
            float dist = editor_math::distance(world_pos, rp);
            if (dist <= ROUTING_POINT_HIT_RADIUS) {
                result.type = HitType::RoutingPoint;
                result.wire_index = wire_idx;
                result.routing_point_index = rp_idx;
                return result;
            }
        }
    }

    constexpr float WIRE_HIT_TOLERANCE = 5.0f;
    for (size_t i = 0; i < bp.wires.size(); i++) {
        const auto& w = bp.wires[i];
        const Node* start_node = nullptr;
        const Node* end_node = nullptr;
        for (const auto& n : bp.nodes) {
            if (n.id == w.start.node_id) start_node = &n;
            if (n.id == w.end.node_id) end_node = &n;
        }
        if (!start_node || !end_node) continue;

        // [p1q2r3s4] Pass wire ID for correct Bus alias port resolution
        Pt start_pos = editor_math::get_port_position(*start_node, w.start.port_name.c_str(), bp.wires, w.id.c_str());
        Pt end_pos = editor_math::get_port_position(*end_node, w.end.port_name.c_str(), bp.wires, w.id.c_str());

        Pt prev = start_pos;
        for (const auto& rp : w.routing_points) {
            float dist = editor_math::distance_to_segment(world_pos, prev, rp);
            if (dist < WIRE_HIT_TOLERANCE) {
                result.type = HitType::Wire;
                result.wire_index = i;
                return result;
            }
            prev = rp;
        }
        float dist = editor_math::distance_to_segment(world_pos, prev, end_pos);
        if (dist < WIRE_HIT_TOLERANCE) {
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
    constexpr float PORT_HIT_RADIUS = 10.0f;  // Радиус зоны клика порта
}

HitResult hit_test_ports(const Blueprint& bp, VisualNodeCache& cache, Pt world_pos) {
    HitResult result;

    // Проверяем порты всех узлов
    for (size_t node_idx = 0; node_idx < bp.nodes.size(); node_idx++) {
        const auto& node = bp.nodes[node_idx];
        // Always call getOrCreate with wires to ensure BusVisualNode has dynamic ports
        // This is important because Bus nodes create visual ports based on wire connections
        auto* visual = cache.getOrCreate(node, bp.wires);
        if (!visual) continue;

        // Проверяем все порты
        for (size_t port_idx = 0; port_idx < visual->getPortCount(); port_idx++) {
            const auto* port = visual->getPort(port_idx);
            if (!port) break;

            Pt port_pos = visual->getPortPosition(port->name);
            float dist = editor_math::distance(world_pos, port_pos);
            if (dist <= PORT_HIT_RADIUS) {
                result.type = HitType::Port;
                result.node_index = node_idx;
                result.port_index = port_idx;
                result.is_input = false;  // [l5h7i9j1] Placeholder; actual side determined below via port_side
                result.port_node_id = node.id;
                // Use target_port if set (for PortAlias), otherwise use port name
                result.port_name = port->target_port.empty() ? port->name : port->target_port;
                result.port_position = port_pos;

                // [g1h2i3j4] For Bus alias ports, store the wire ID so the caller
                // can identify which specific wire is connected to this port.
                if (!port->target_port.empty()) {
                    result.port_wire_id = port->name;  // alias name IS the wire ID
                }

                // Determine port side by checking if it's in inputs or outputs
                // For aliased ports, check the target port instead
                std::string port_to_check = port->target_port.empty() ? port->name : port->target_port;

                bool is_input = false;
                bool is_output = false;
                for (const auto& p : node.inputs) {
                    if (p.name == port_to_check) {
                        is_input = true;
                        break;
                    }
                }
                for (const auto& p : node.outputs) {
                    if (p.name == port_to_check) {
                        is_output = true;
                        break;
                    }
                }

                // Set port side based on membership
                if (is_input && is_output) {
                    result.port_side = PortSide::InOut;
                } else if (is_input) {
                    result.port_side = PortSide::Input;
                } else {
                    result.port_side = PortSide::Output;
                }

                return result;
            }
        }
    }

    return result;
}
