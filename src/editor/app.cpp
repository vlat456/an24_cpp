#include "app.h"
#include "hittest.h"
#include "wires/hittest.h"
#include "trigonometry.h"
#include "router/router.h"
#include "visual_node.h"
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
            // drag_anchor = позиция первого выделенного узла (unsnapped accumulator)
            // Use IDraggable interface to get position
            auto primary_visual = VisualNodeFactory::create(blueprint.nodes[hit.node_index], blueprint.wires);
            Pt primary_pos = primary_visual->getPosition();
            interaction.start_drag_node(primary_pos);
            // Сохраняем смещения относительно anchor для каждого выделенного узла
            interaction.drag_node_offsets.clear();
            for (size_t idx : interaction.selected_nodes) {
                if (idx < blueprint.nodes.size()) {
                    auto visual = VisualNodeFactory::create(blueprint.nodes[idx], blueprint.wires);
                    interaction.drag_node_offsets.push_back(visual->getPosition() - primary_pos);
                }
            }
        } else if (hit.type == HitType::RoutingPoint) {
            // Выделяем routing point и начинаем drag
            Pt rp_pos = blueprint.wires[hit.wire_index].routing_points[hit.routing_point_index];
            interaction.start_drag_routing_point(hit.wire_index, hit.routing_point_index, rp_pos);
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
                auto visual = VisualNodeFactory::create(blueprint.nodes[i], blueprint.wires);
                Pt pos = visual->getPosition();
                Pt size = visual->getSize();
                // Check if node center is in marquee
                float cx = pos.x + size.x / 2;
                float cy = pos.y + size.y / 2;
                if (cx >= min_x && cx <= max_x && cy >= min_y && cy <= max_y) {
                    interaction.add_node_selection(i);
                }
            }
            interaction.marquee_selecting = false;
        }

        // Snap уже применяется в on_mouse_drag, не нужен на release
        interaction.drag_node_offsets.clear();
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
        // Accumulate unsnapped delta, then snap
        interaction.update_drag_anchor(world_delta);
        Pt snapped = editor_math::snap_to_grid(interaction.drag_anchor, blueprint.grid_step);
        for (size_t i = 0; i < interaction.selected_nodes.size(); i++) {
            size_t idx = interaction.selected_nodes[i];
            if (idx < blueprint.nodes.size()) {
                Pt offset = (i < interaction.drag_node_offsets.size())
                    ? interaction.drag_node_offsets[i] : Pt(0.0f, 0.0f);
                Pt new_pos = snapped + offset;
                // Use IDraggable interface to set position
                auto visual = VisualNodeFactory::create(blueprint.nodes[idx], blueprint.wires);
                visual->setPosition(new_pos);
                // Also update raw Node for persistence
                blueprint.nodes[idx].pos = new_pos;
            }
        }
    } else if (interaction.dragging == Dragging::RoutingPoint) {
        // Accumulate unsnapped delta, then snap
        interaction.update_drag_anchor(world_delta);
        Pt snapped = editor_math::snap_to_grid(interaction.drag_anchor, blueprint.grid_step);
        size_t wire_idx = interaction.routing_point_wire;
        size_t rp_idx = interaction.routing_point_index;
        if (wire_idx < blueprint.wires.size()) {
            auto& wire = blueprint.wires[wire_idx];
            if (rp_idx < wire.routing_points.size()) {
                wire.routing_points[rp_idx] = snapped;
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
                    Pt start_pos = editor_math::get_port_position(*start_node, wire.start.port_name.c_str(), blueprint.wires);
                    Pt end_pos = editor_math::get_port_position(*end_node, wire.end.port_name.c_str(), blueprint.wires);

                    // Build existing wire polylines (all wires except current)
                    std::vector<std::vector<Pt>> existing_paths;
                    for (size_t i = 0; i < blueprint.wires.size(); i++) {
                        if (i == wire_idx) continue;
                        const auto& ow = blueprint.wires[i];
                        const Node* sn = nullptr;
                        const Node* en = nullptr;
                        for (const auto& n : blueprint.nodes) {
                            if (n.id == ow.start.node_id) sn = &n;
                            if (n.id == ow.end.node_id) en = &n;
                        }
                        if (!sn || !en) continue;
                        std::vector<Pt> poly;
                        poly.push_back(editor_math::get_port_position(*sn, ow.start.port_name.c_str(), blueprint.wires));
                        poly.insert(poly.end(), ow.routing_points.begin(), ow.routing_points.end());
                        poly.push_back(editor_math::get_port_position(*en, ow.end.port_name.c_str(), blueprint.wires));
                        existing_paths.push_back(std::move(poly));
                    }

                    // Роутинг с port departure/arrival + existing wire avoidance
                    auto path = route_around_nodes(
                        start_pos, end_pos,
                        *start_node, wire.start.port_name.c_str(),
                        *end_node, wire.end.port_name.c_str(),
                        blueprint.nodes, blueprint.grid_step,
                        existing_paths);

                    if (!path.empty()) {
                        // Strip first and last points (they are port positions, not routing points)
                        // routing_points = intermediate waypoints only
                        if (path.size() > 2) {
                            wire.routing_points.assign(path.begin() + 1, path.end() - 1);
                        } else {
                            wire.routing_points.clear();
                        }
                        DEBUG_LOG("Rerouted wire {} with {} points", wire.id, wire.routing_points.size());
                    } else {
                        DEBUG_LOG("Could not find A* route for wire {}", wire.id);
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

            Pt start_pos = editor_math::get_port_position(*start_node, wire.start.port_name.c_str(), blueprint.wires);
            Pt end_pos = editor_math::get_port_position(*end_node, wire.end.port_name.c_str(), blueprint.wires);

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
