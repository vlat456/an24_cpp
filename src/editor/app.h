#pragma once

#include "data/blueprint.h"
#include "viewport/viewport.h"
#include "input/input_types.h"
#include "input/canvas_input.h"
#include "visual/hittest.h"
#include "visual/scene/scene.h"
#include "visual/scene/wire_manager.h"
#include "window/window_manager.h"
#include "window/properties_window.h"
#include "visual/inspector/inspector.h"
#include "../jit_solver/simulator.h"
#include "json_parser/json_parser.h"
#include <optional>
#include <unordered_set>

/// Главное приложение редактора
struct EditorApp {
    /// Shared blueprint data (single source of truth)
    Blueprint blueprint;

    /// Window manager: owns all editor windows (root + sub-blueprint windows)
    WindowManager window_manager{blueprint};

    /// Convenience accessors for the root window
    VisualScene& scene = window_manager.root().scene;
    CanvasInput& input = window_manager.root().input;
    WireManager& wire_manager = window_manager.root().wire_manager;

    /// JIT Simulation (manages component lifecycle)
    an24::Simulator<an24::JIT_Solver> simulation;
    bool simulation_running = false;

    /// Type registry (loaded from library/*.json)
    an24::TypeRegistry type_registry;

    /// Context menu state (tracks which window triggered it)
    bool show_context_menu = false;
    Pt context_menu_pos;
    std::string context_menu_group_id;

    /// Node context menu state (right-click on node)
    bool show_node_context_menu = false;
    size_t context_menu_node_index = 0;

    /// Properties window
    PropertiesWindow properties_window;

    /// Inspector window (component tree with port connections)
    Inspector inspector;
    bool show_inspector = true;  // Show/hide inspector window

    /// Manual signal overrides (for button clicks, etc.)
    /// Maps "node_id.port_name" -> voltage value (temporary, cleared after use)
    std::unordered_map<std::string, float> signal_overrides;

    /// HoldButtons currently being held (mouse is down on them)
    std::unordered_set<std::string> held_buttons;

    EditorApp()
        : inspector(window_manager.root().scene)  // Inspector needs scene reference
    {
        // Load type registry at startup
        type_registry = an24::load_type_registry();
    }

    /// Создать новую схему
    void new_circuit() {
        window_manager.closeAll();
        scene.reset();
        simulation = an24::Simulator<an24::JIT_Solver>();
        simulation_running = false;
    }

    /// Перестроить симуляцию (при изменении схемы)
    void rebuild_simulation() {
        if (simulation_running) {
            // If running, restart to rebuild components
            simulation.stop();
            simulation.start(scene.blueprint());
        }
        // If not running, components will be built on next start()
    }

    /// Запустить симуляцию
    void start_simulation() {
        if (!simulation_running) {
            simulation.start(scene.blueprint());  // Creates components!
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
    void update_simulation(float dt) {
        if (simulation_running) {
            simulation.step(dt);
        }
    }

    /// Обновить node_content на основе значений симуляции
    void update_node_content_from_simulation();

    /// Сбросить node_content в дефолтные значения (при stop)
    void reset_node_content();

    /// Обновить симуляцию (apply overrides, step, clear overrides)
    void update_simulation_step(float dt);

    /// Handle InputResult from any window's CanvasInput
    void apply_input_result(const InputResult& r, const std::string& group_id = "") {
        if (r.rebuild_simulation) {
            rebuild_simulation();
            window_manager.removeOrphanedWindows();
        }
        if (r.show_context_menu) {
            show_context_menu = true;
            context_menu_pos = r.context_menu_pos;
            context_menu_group_id = group_id;
        }
        if (r.show_node_context_menu) {
            show_node_context_menu = true;
            context_menu_node_index = r.context_menu_node_index;
        }
        if (!r.open_sub_window.empty()) open_sub_window(r.open_sub_window);
    }

    /// Добавить компонент на схему (group_id = which sub-blueprint to add to)
    void add_component(const std::string& classname, Pt world_pos, const std::string& group_id = "");

    /// Добавить вложенный блюпринт на схему (collapsed node)
    void add_blueprint(const std::string& blueprint_name, Pt world_pos, const std::string& group_id = "");

    /// Переключить Switch (toggle button)
    void trigger_switch(const std::string& node_id);

    /// HoldButton: нажать кнопку (control = 1.0V)
    void hold_button_press(const std::string& node_id);

    /// HoldButton: отпустить кнопку (control = 2.0V, затем 0.0V)
    void hold_button_release(const std::string& node_id);

    /// Open a sub-window for a collapsed group (replaces drill_into).
    void open_sub_window(const std::string& collapsed_group_id);

    /// Open properties window for a specific node
    void open_properties_for_node(size_t node_index);

private:
};
