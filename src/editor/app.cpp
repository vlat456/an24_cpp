#include "app.h"
#include "hittest.h"

void EditorApp::on_mouse_down(Pt world_pos, MouseButton btn, Pt canvas_min) {
    if (btn == MouseButton::Middle) {
        // Middle click - начало панорамирования
        interaction.set_panning(true);
        return;
    }

    if (btn == MouseButton::Left) {
        // Hit test - что под курсором?
        HitResult hit = hit_test(blueprint, world_pos, viewport);

        if (hit.type == HitType::Node) {
            // Выделяем узел
            interaction.selected_node = hit.node_index;
            // Начинаем drag
            interaction.start_drag_node(world_pos);
        } else if (hit.type == HitType::Wire) {
            // Выделяем провод
            interaction.selected_wire = hit.wire_index;
        } else {
            // Клик в пустоту - снимаем выделение
            interaction.clear_selection();
        }
    }
}

void EditorApp::on_mouse_up(MouseButton btn) {
    if (btn == MouseButton::Middle) {
        interaction.set_panning(false);
    }

    if (btn == MouseButton::Left) {
        // Заканчиваем drag
        interaction.end_drag();
    }
}

void EditorApp::on_mouse_drag(Pt world_delta, Pt canvas_min) {
    (void)canvas_min;

    if (interaction.panning) {
        // Панорамирование - обратный delta
        viewport.pan.x -= world_delta.x / viewport.zoom;
        viewport.pan.y -= world_delta.y / viewport.zoom;
    } else if (interaction.dragging == Dragging::Node) {
        // Перетаскивание узла
        if (interaction.selected_node.has_value()) {
            size_t idx = *interaction.selected_node;
            if (idx < blueprint.nodes.size()) {
                blueprint.nodes[idx].pos.x += world_delta.x;
                blueprint.nodes[idx].pos.y += world_delta.y;
            }
        }
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
        // Удаление выделенного
        if (interaction.selected_node.has_value()) {
            size_t idx = *interaction.selected_node;
            if (idx < blueprint.nodes.size()) {
                blueprint.nodes.erase(blueprint.nodes.begin() + idx);
                interaction.clear_selection();
            }
        }
    }
}
