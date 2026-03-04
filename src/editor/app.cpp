#include "app.h"
#include "hittest.h"
#include "wires/hittest.h"
#include "trigonometry.h"
#include "debug.h"
#include <algorithm>

void EditorApp::on_mouse_down(Pt world_pos, MouseButton btn, Pt canvas_min, bool add_to_selection) {
    (void)canvas_min;

    if (btn == MouseButton::Left) {
        // Hit test - что под курсром?
        HitResult hit = hit_test(blueprint, world_pos, viewport);
        DEBUG_LOG("Hit test at ({:.1f}, {:.1f}): type={}", world_pos.x, world_pos.y, (int)hit.type);

        if (hit.type == HitType::Node) {
            // Если не add_to_selection - очищаем предыдущее выделение
            if (!add_to_selection) {
                interaction.clear_selection();
            }
            // Выделяем узел и начинаем drag
            interaction.add_node_selection(hit.node_index);
            interaction.start_drag_node(world_pos);
        } else if (hit.type == HitType::RoutingPoint) {
            // Выделяем routing point и начинаем drag
            interaction.start_drag_routing_point(hit.wire_index, hit.routing_point_index);
        } else if (hit.type == HitType::Wire) {
            // Выделяем провод
            interaction.selected_wire = hit.wire_index;
            interaction.clear_drag();
        } else {
            // Клик в пустоту - panning (как в Rust)
            interaction.clear_selection();
            interaction.set_panning(true);
        }
    }
}

void EditorApp::on_mouse_up(MouseButton btn) {
    if (btn == MouseButton::Left) {
        // Если был marquee selection - применить его
        if (interaction.marquee_selecting) {
            // Marquee selection - выделить все узлы в прямоугольнике
            float min_x = std::min(interaction.marquee_start.x, interaction.marquee_end.x);
            float max_x = std::max(interaction.marquee_start.x, interaction.marquee_end.x);
            float min_y = std::min(interaction.marquee_start.y, interaction.marquee_end.y);
            float max_y = std::max(interaction.marquee_start.y, interaction.marquee_end.y);

            for (size_t i = 0; i < blueprint.nodes.size(); i++) {
                const auto& n = blueprint.nodes[i];
                // Check if node center is in marquee
                float cx = n.pos.x + n.size.x / 2;
                float cy = n.pos.y + n.size.y / 2;
                if (cx >= min_x && cx <= max_x && cy >= min_y && cy <= max_y) {
                    interaction.add_node_selection(i);
                }
            }
            interaction.marquee_selecting = false;
        }

        // Заканчиваем panning или drag
        interaction.set_panning(false);
        interaction.end_drag();
    }
}

void EditorApp::on_mouse_drag(Pt world_delta, Pt canvas_min) {
    (void)canvas_min;

    if (interaction.panning) {
        // Панорамирование - обратный delta
        viewport.pan.x -= world_delta.x;
        viewport.pan.y -= world_delta.y;
    } else if (interaction.dragging == Dragging::Node) {
        // Перетаскивание всех выделенных узлов
        for (size_t idx : interaction.selected_nodes) {
            if (idx < blueprint.nodes.size()) {
                blueprint.nodes[idx].pos.x += world_delta.x;
                blueprint.nodes[idx].pos.y += world_delta.y;
            }
        }
    } else if (interaction.dragging == Dragging::RoutingPoint) {
        // Перетаскивание routing point с привязкой к сетке
        size_t wire_idx = interaction.routing_point_wire;
        size_t rp_idx = interaction.routing_point_index;
        if (wire_idx < blueprint.wires.size()) {
            auto& wire = blueprint.wires[wire_idx];
            if (rp_idx < wire.routing_points.size()) {
                Pt new_pos = wire.routing_points[rp_idx] + world_delta;
                wire.routing_points[rp_idx] = new_pos;
            }
        }
    } else if (interaction.marquee_selecting) {
        // Обновляем конец marquee
        interaction.marquee_end = interaction.marquee_end + world_delta;
    }
}

void EditorApp::on_scroll(float delta, Pt mouse_pos, Pt canvas_min) {
    viewport.zoom_at(delta, mouse_pos, canvas_min);
}

void EditorApp::on_key_down(Key key) {
    if (key == Key::Escape) {
        // Сброс выделения
        interaction.clear_selection();
    } else if (key == Key::Delete) {
        // Удаление всех выделенных узлов (с конца чтобы не сбить индексы)
        std::sort(interaction.selected_nodes.begin(), interaction.selected_nodes.end(), std::greater<size_t>());
        for (size_t idx : interaction.selected_nodes) {
            if (idx < blueprint.nodes.size()) {
                blueprint.nodes.erase(blueprint.nodes.begin() + idx);
            }
        }
        interaction.clear_selection();
    }
}

void EditorApp::on_double_click(Pt world_pos) {
    DEBUG_LOG("Double click at ({:.1f}, {:.1f})", world_pos.x, world_pos.y);

    // 1. Сначала проверяем hit на routing point - удалить
    auto rp_hit = hit_test_routing_point(blueprint, world_pos);
    if (rp_hit) {
        DEBUG_LOG("Double click: delete routing point wire={} rp={}", rp_hit->wire_index, rp_hit->routing_point_index);
        if (rp_hit->wire_index < blueprint.wires.size()) {
            auto& wire = blueprint.wires[rp_hit->wire_index];
            if (rp_hit->routing_point_index < wire.routing_points.size()) {
                wire.routing_points.erase(wire.routing_points.begin() + rp_hit->routing_point_index);
            }
        }
        return;
    }

    // 2. Потом проверяем hit на wire - добавить новую точку
    HitResult hit = hit_test(blueprint, world_pos, viewport);
    DEBUG_LOG("Double click: hit type={}", (int)hit.type);
    if (hit.type == HitType::Wire) {
        DEBUG_LOG("Double click: add routing point to wire {}", hit.wire_index);
        if (hit.wire_index < blueprint.wires.size()) {
            auto& wire = blueprint.wires[hit.wire_index];

            // Находим ближайший сегмент и вставляем после него
            const Node* start_node = nullptr;
            const Node* end_node = nullptr;
            for (const auto& n : blueprint.nodes) {
                if (n.id == wire.start.node_id) start_node = &n;
                if (n.id == wire.end.node_id) end_node = &n;
            }
            if (!start_node || !end_node) return;

            Pt start_pos = editor_math::get_port_position(*start_node, wire.start.port_name.c_str());
            Pt end_pos = editor_math::get_port_position(*end_node, wire.end.port_name.c_str());

            // Ищем ближайший сегмент для вставки
            size_t insert_idx = 0;
            float min_dist = editor_math::distance_to_segment(world_pos, start_pos, end_pos);

            // Проверяем сегменты: start -> rp[0] -> ... -> rp[n] -> end
            Pt prev = start_pos;
            for (size_t i = 0; i <= wire.routing_points.size(); i++) {
                Pt next = (i < wire.routing_points.size()) ? wire.routing_points[i] : end_pos;
                float dist = editor_math::distance_to_segment(world_pos, prev, next);
                if (dist < min_dist) {
                    min_dist = dist;
                    insert_idx = i; // вставить перед next
                }
                prev = next;
            }

            // Вставляем точно в позицию double click
            wire.routing_points.insert(wire.routing_points.begin() + insert_idx, world_pos);
        }
    }
}
