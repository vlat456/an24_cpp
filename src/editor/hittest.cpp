#include "hittest.h"
#include "trigonometry.h"

HitResult hit_test(const Blueprint& bp, Pt world_pos, const Viewport& vp) {
    HitResult result;
    (void)vp;

    // Сначала проверяем узлы (они рисуются поверх проводов)
    for (size_t i = 0; i < bp.nodes.size(); i++) {
        const auto& n = bp.nodes[i];

        // Проверяем попадание в rect узла
        if (world_pos.x >= n.pos.x && world_pos.x <= n.pos.x + n.size.x &&
            world_pos.y >= n.pos.y && world_pos.y <= n.pos.y + n.size.y) {
            result.type = HitType::Node;
            result.node_index = i;
            return result;
        }
    }

    // Потом проверяем routing points (чтобы можно было их выбирать и dragить)
    for (size_t wire_idx = 0; wire_idx < bp.wires.size(); wire_idx++) {
        const auto& w = bp.wires[wire_idx];
        for (size_t rp_idx = 0; rp_idx < w.routing_points.size(); rp_idx++) {
            const Pt& rp = w.routing_points[rp_idx];
            float dist = editor_math::distance(world_pos, rp);
            if (dist <= 25.0f) { // radius для routing point
                result.type = HitType::RoutingPoint;
                result.wire_index = wire_idx;
                result.routing_point_index = rp_idx;
                return result;
            }
        }
    }

    // Потом проверяем провода (упрощенно - только концевые точки)
    for (size_t i = 0; i < bp.wires.size(); i++) {
        const auto& w = bp.wires[i];

        // Находим позиции портов
        const Node* start_node = nullptr;
        const Node* end_node = nullptr;

        for (const auto& n : bp.nodes) {
            if (n.id == w.start.node_id) start_node = &n;
            if (n.id == w.end.node_id) end_node = &n;
        }

        if (!start_node || !end_node) continue;

        Pt start_pos = editor_math::get_port_position(*start_node, w.start.port_name.c_str());
        Pt end_pos = editor_math::get_port_position(*end_node, w.end.port_name.c_str());

        // Проверяем сегменты: start -> rp[0] -> ... -> rp[n] -> end
        Pt prev = start_pos;
        for (const auto& rp : w.routing_points) {
            float dist = editor_math::distance_to_segment(world_pos, prev, rp);
            if (dist < 20.0f) { // 20 пикселей толерантность
                result.type = HitType::Wire;
                result.wire_index = i;
                return result;
            }
            prev = rp;
        }
        // Последний сегмент
        float dist = editor_math::distance_to_segment(world_pos, prev, end_pos);
        if (dist < 20.0f) {
            result.type = HitType::Wire;
            result.wire_index = i;
            return result;
        }
    }

    return result;
}
