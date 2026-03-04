#pragma once

#include "data/pt.h"
#include "data/node.h"
#include <cmath>
#include <algorithm>

namespace editor_math {

// Точная позиция порта (как в render.cpp)
inline Pt get_port_position(const Node& node, const char* port_name) {
    static constexpr float HEADER_HEIGHT = 20.0f;

    // Bus - все порты в центре
    if (node.kind == NodeKind::Bus) {
        return Pt(node.pos.x + node.size.x / 2, node.pos.y + node.size.y / 2);
    }

    // Ref - порт сверху
    if (node.kind == NodeKind::Ref) {
        return Pt(node.pos.x + node.size.x / 2, node.pos.y);
    }

    // Обычный Node - порты по сторонам
    int num_inputs = (int)node.inputs.size();
    int num_outputs = (int)node.outputs.size();
    int max_ports = std::max(num_inputs, num_outputs);
    if (max_ports == 0) max_ports = 1;

    float port_area_height = node.size.y - HEADER_HEIGHT;
    if (port_area_height < 1.0f) port_area_height = 1.0f;

    for (size_t i = 0; i < node.inputs.size(); i++) {
        if (node.inputs[i].name == port_name) {
            float t = (float)(i + 1) / (float)(max_ports + 1);
            return Pt(node.pos.x, node.pos.y + HEADER_HEIGHT + port_area_height * t);
        }
    }

    for (size_t i = 0; i < node.outputs.size(); i++) {
        if (node.outputs[i].name == port_name) {
            float t = (float)(i + 1) / (float)(max_ports + 1);
            return Pt(node.pos.x + node.size.x, node.pos.y + HEADER_HEIGHT + port_area_height * t);
        }
    }

    return Pt(node.pos.x + node.size.x / 2, node.pos.y + node.size.y / 2);
}

// Расстояние между точками
inline float distance(Pt a, Pt b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

// Расстояние от точки до отрезка
inline float distance_to_segment(Pt p, Pt a, Pt b) {
    float ab_x = b.x - a.x;
    float ab_y = b.y - a.y;
    float ab_len_sq = ab_x * ab_x + ab_y * ab_y;

    if (ab_len_sq < 1e-6f) {
        return distance(p, a);
    }

    float t = ((p.x - a.x) * ab_x + (p.y - a.y) * ab_y) / ab_len_sq;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    Pt closest(a.x + t * ab_x, a.y + t * ab_y);
    return distance(p, closest);
}

/// Привязка точки к сетке (snap to grid)
inline Pt snap_to_grid(Pt pos, float grid_step) {
    return Pt(
        std::round(pos.x / grid_step) * grid_step,
        std::round(pos.y / grid_step) * grid_step
    );
}

} // namespace editor_math
