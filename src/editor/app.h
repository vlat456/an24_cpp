#pragma once

#include "data/blueprint.h"
#include "viewport/viewport.h"
#include "interact/interaction.h"
#include "hittest.h"
#include "visual_node.h"
#include "../jit_solver/simulator.h"
#include "json_parser/json_parser.h"
#include <optional>
#include <unordered_set>

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

    /// JIT Simulation (manages component lifecycle)
    an24::Simulator<an24::JIT_Solver> simulation;
    bool simulation_running = false;

    /// Component registry (loaded from components/*.json)
    an24::ComponentRegistry component_registry;

    /// Context menu state
    bool show_context_menu = false;
    Pt context_menu_pos;

    /// Manual signal overrides (for button clicks, etc.)
    /// Maps "node_id.port_name" -> voltage value (temporary, cleared after use)
    std::unordered_map<std::string, float> signal_overrides;

    /// HoldButtons currently being held (mouse is down on them)
    std::unordered_set<std::string> held_buttons;

    /// Last mouse position (for wire creation, etc.)
    Pt last_mouse_pos;

    EditorApp() {
        // Load component registry at startup
        component_registry = an24::load_component_registry();
    }

    /// Создать новую схему
    void new_circuit() {
        blueprint = Blueprint();
        viewport = Viewport();
        interaction = Interaction();
        simulation = an24::Simulator<an24::JIT_Solver>();  // Reset simulator
        simulation_running = false;
        visual_cache.clear();
    }

    /// Перестроить симуляцию (при изменении схемы)
    void rebuild_simulation() {
        if (simulation_running) {
            // If running, restart to rebuild components
            simulation.stop();
            simulation.start(blueprint);
        }
        // If not running, components will be built on next start()
    }

    /// Запустить симуляцию
    void start_simulation() {
        if (!simulation_running) {
            simulation.start(blueprint);  // Creates components!
            simulation_running = true;
        }
    }

    /// Остановить симуляцию
    void stop_simulation() {
        simulation.stop();  // Destroys components!
        reset_node_content();  // Reset visual content to defaults
        simulation_running = false;
    }

    /// Обновить симуляцию на один шаг
    void update_simulation() {
        if (simulation_running) {
            constexpr float dt = 0.016f;  // 60 Hz
            simulation.step(dt);
        }
    }

    /// Обновить node_content на основе значений симуляции
    void update_node_content_from_simulation();

    /// Сбросить node_content в дефолтные значения (при stop)
    void reset_node_content();

    /// Обновить симуляцию (apply overrides, step, clear overrides)
    void update_simulation_step();

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

    /// Добавить компонент на схему
    void add_component(const std::string& classname, Pt world_pos);

    /// Переключить Switch (toggle button)
    void trigger_switch(const std::string& node_id);

    /// HoldButton: нажать кнопку (control = 1.0V)
    void hold_button_press(const std::string& node_id);

    /// HoldButton: отпустить кнопку (control = 2.0V, затем 0.0V)
    void hold_button_release(const std::string& node_id);
};
