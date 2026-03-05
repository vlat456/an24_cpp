#pragma once

#include "data/blueprint.h"
#include "viewport/viewport.h"
#include "interact/interaction.h"
#include "hittest.h"
#include "visual_node.h"
#include "simulation.h"

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
    Space,   // Space = toggle simulation
};

/// Главное приложение редактора
struct EditorApp {
    /// Данные схемы
    Blueprint blueprint;

    /// Viewport (pan, zoom)
    Viewport viewport;

    /// Interaction state
    Interaction interaction;

    /// Visual node cache for rendering content (ImGui widgets)
    VisualNodeCache visual_cache;

    /// JIT Simulation
    SimulationController simulation;
    bool simulation_running = false;

    EditorApp() = default;

    /// Создать новую схему
    void new_circuit() {
        blueprint = Blueprint();
        viewport = Viewport();
        interaction = Interaction();
        simulation = SimulationController();
        simulation_running = false;
    }

    /// Запустить симуляцию
    void start_simulation() {
        if (!simulation_running) {
            simulation.build(blueprint);
            simulation.start();
            simulation_running = true;
        }
    }

    /// Остановить симуляцию
    void stop_simulation() {
        simulation.stop();
        simulation_running = false;
    }

    /// Обновить симуляцию на один шаг
    void update_simulation() {
        if (simulation_running) {
            simulation.step(simulation.dt);
        }
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
