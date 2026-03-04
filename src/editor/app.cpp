#include "app.h"
#include "hittest.h"
#include <algorithm>

void EditorApp::on_mouse_down(Pt world_pos, MouseButton btn, Pt canvas_min, bool add_to_selection) {
    (void)canvas_min;

    if (btn == MouseButton::Left) {
        // Hit test - что под курсром?
        HitResult hit = hit_test(blueprint, world_pos, viewport);

        if (hit.type == HitType::Node) {
            // Если не add_to_selection - очищаем предыдущее выделение
            if (!add_to_selection) {
                interaction.clear_selection();
            }
            // Выделяем узел и начинаем drag
            interaction.add_node_selection(hit.node_index);
            interaction.start_drag_node(world_pos);
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
