#include "app.h"
#include "hittest.h"
#include "wires/hittest.h"
#include "trigonometry.h"
#include "router/router.h"
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

        // Заканчиваем panning или drag - применяем snap к сетке
        if (interaction.dragging == Dragging::Node) {
            // Snap всех выделенных узлов к сетке
            for (size_t idx : interaction.selected_nodes) {
                if (idx < blueprint.nodes.size()) {
                    blueprint.nodes[idx].pos = editor_math::snap_to_grid(
                        blueprint.nodes[idx].pos, blueprint.grid_step);
                }
            }
        } else if (interaction.dragging == Dragging::RoutingPoint) {
            // Snap routing point к сетке
            size_t wire_idx = interaction.routing_point_wire;
            size_t rp_idx = interaction.routing_point_index;
            if (wire_idx < blueprint.wires.size()) {
                auto& wire = blueprint.wires[wire_idx];
                if (rp_idx < wire.routing_points.size()) {
                    wire.routing_points[rp_idx] = editor_math::snap_to_grid(
                        wire.routing_points[rp_idx], blueprint.grid_step);
                }
            }
        }

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
        // Перетаскивание узлов - просто добавляем delta (без snap)
        for (size_t idx : interaction.selected_nodes) {
            if (idx < blueprint.nodes.size()) {
                blueprint.nodes[idx].pos.x += world_delta.x;
                blueprint.nodes[idx].pos.y += world_delta.y;
            }
        }
    } else if (interaction.dragging == Dragging::RoutingPoint) {
        // Перетаскивание routing point
        size_t wire_idx = interaction.routing_point_wire;
        size_t rp_idx = interaction.routing_point_index;
        if (wire_idx < blueprint.wires.size()) {
            auto& wire = blueprint.wires[wire_idx];
            if (rp_idx < wire.routing_points.size()) {
                wire.routing_points[rp_idx].x += world_delta.x;
                wire.routing_points[rp_idx].y += world_delta.y;
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

        // Collect IDs of nodes to delete
        std::vector<std::string> deleted_ids;
        for (size_t idx : interaction.selected_nodes) {
            if (idx < blueprint.nodes.size()) {
                deleted_ids.push_back(blueprint.nodes[idx].id);
                blueprint.nodes.erase(blueprint.nodes.begin() + idx);
            }
        }

        // Remove wires that reference deleted nodes
        blueprint.wires.erase(
            std::remove_if(blueprint.wires.begin(), blueprint.wires.end(),
                [&deleted_ids](const Wire& w) {
                    for (const auto& id : deleted_ids) {
                        if (w.start.node_id == id || w.end.node_id == id) return true;
                    }
                    return false;
                }),
            blueprint.wires.end());

        interaction.clear_selection();
    } else if (key == Key::R) {
        // R - роутинг выделенного провода
        if (interaction.selected_wire.has_value()) {
            size_t wire_idx = *interaction.selected_wire;
            if (wire_idx < blueprint.wires.size()) {
                Wire& wire = blueprint.wires[wire_idx];

                // Находим start и end node
                const Node* start_node = nullptr;
                const Node* end_node = nullptr;
                for (const auto& n : blueprint.nodes) {
                    if (n.id == wire.start.node_id) start_node = &n;
                    if (n.id == wire.end.node_id) end_node = &n;
                }

                if (start_node && end_node) {
                    Pt start_pos = editor_math::get_port_position(*start_node, wire.start.port_name.c_str());
                    Pt end_pos = editor_math::get_port_position(*end_node, wire.end.port_name.c_str());

                    // Роутинг вокруг nodes
                    auto path = route_around_nodes(start_pos, end_pos, blueprint.nodes, blueprint.grid_step);

                    if (!path.empty()) {
                        wire.routing_points = path;
                        DEBUG_LOG("Rerouted wire {} with {} points", wire.id, wire.routing_points.size());
                    } else {
                        // Fallback: L-shape
                        wire.routing_points = route_l_shape(start_pos, end_pos, blueprint.grid_step);
                        DEBUG_LOG("Could not find A* route, using L-shape for wire {}", wire.id);
                    }
                }
            }
        }
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

            // Вставляем с привязкой к сетке
            Pt snapped_pos = editor_math::snap_to_grid(world_pos, blueprint.grid_step);
            wire.routing_points.insert(wire.routing_points.begin() + insert_idx, snapped_pos);
        }
    }
}
