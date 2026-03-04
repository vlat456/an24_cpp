#include "hittest.h"
#include <cmath>

namespace {

// Приблизительная позиция порта (упрощено)
Pt get_port_position_approx(const Node& node, const char* port_name) {
    // Ищем в inputs
    for (size_t i = 0; i < node.inputs.size(); i++) {
        if (node.inputs[i].name == port_name) {
            float y_offset = node.size.y * (float)(i + 1) / (float)(node.inputs.size() + 1);
            return Pt(node.pos.x, node.pos.y + y_offset);
        }
    }

    // Ищем в outputs
    for (size_t i = 0; i < node.outputs.size(); i++) {
        if (node.outputs[i].name == port_name) {
            float y_offset = node.size.y * (float)(i + 1) / (float)(node.outputs.size() + 1);
            return Pt(node.pos.x + node.size.x, node.pos.y + y_offset);
        }
    }

    return Pt(node.pos.x + node.size.x / 2, node.pos.y + node.size.y / 2);
}

// Расстояние от точки до отрезка
float distance_to_segment(Pt p, Pt a, Pt b) {
    // Вектор от a к b
    float ab_x = b.x - a.x;
    float ab_y = b.y - a.y;

    // Длина отрезка в квадрате
    float ab_len_sq = ab_x * ab_x + ab_y * ab_y;

    if (ab_len_sq < 1e-6f) {
        // Отрезок вырожден в точку
        float dx = p.x - a.x;
        float dy = p.y - a.y;
        return (float)std::sqrt(dx * dx + dy * dy);
    }

    // Проекция p на линию ab
    float t = ((p.x - a.x) * ab_x + (p.y - a.y) * ab_y) / ab_len_sq;

    // Ограничиваем t [0, 1]
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    // Ближайшая точка на отрезке
    Pt closest(a.x + t * ab_x, a.y + t * ab_y);

    // Расстояние
    float dx = p.x - closest.x;
    float dy = p.y - closest.y;
    return (float)std::sqrt(dx * dx + dy * dy);
}

} // namespace

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

        Pt start_pos = get_port_position_approx(*start_node, w.start.port_name.c_str());
        Pt end_pos = get_port_position_approx(*end_node, w.end.port_name.c_str());

        // Вычисляем расстояние до отрезка
        float dist = distance_to_segment(world_pos, start_pos, end_pos);
        if (dist < 10.0f) { // 10 пикселей толерантность
            result.type = HitType::Wire;
            result.wire_index = i;
            return result;
        }
    }

    return result;
}
