#pragma once

#include "data/blueprint.h"
#include "viewport/viewport.h"
#include "interact/interaction.h"
#include "hittest.h"

/// Кнопки мыши
enum class MouseButton {
    Left,
    Middle,
    Right
};

/// Клавиши клавиатуры
enum class Key {
    Escape,
    Delete,
    S,       // Ctrl+S = save
    Z,       // Ctrl+Z = undo
    R,       // R = reroute selected wire
};

/// Главное приложение редактора
struct EditorApp {
    /// Данные схемы
    Blueprint blueprint;

    /// Viewport (pan, zoom)
    Viewport viewport;

    /// Interaction state
    Interaction interaction;

    EditorApp() = default;

    /// Создать новую схему
    void new_circuit() {
        blueprint = Blueprint();
        viewport = Viewport();
        interaction = Interaction();
    }

    /// Обработка mouse down
    void on_mouse_down(Pt world_pos, MouseButton btn, Pt canvas_min, bool add_to_selection = false);

    /// Обработка mouse up
    void on_mouse_up(MouseButton btn);

    /// Обработка mouse drag
    void on_mouse_drag(Pt world_delta, Pt canvas_min);

    /// Обработка scroll (zoom)
    void on_scroll(float delta, Pt mouse_pos, Pt canvas_min);

    /// Обработка key down
    void on_key_down(Key key);

    /// Обработка double click - добавить/удалить routing point
    void on_double_click(Pt world_pos);
};
