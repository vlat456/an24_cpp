#pragma once

#include "data/blueprint.h"
#include "viewport/viewport.h"
#include "input/input_types.h"
#include "input/canvas_input.h"
#include "visual/scene.h"
#include "visual/scene_mutations.h"
#include "window/window_manager.h"
#include "window/properties_window.h"
#include "visual/inspector/inspector.h"
#include "../jit_solver/simulator.h"
#include "json_parser/json_parser.h"
#include <spdlog/spdlog.h>
#include <optional>
#include <stdexcept>
#include <unordered_set>

/// Главное приложение редактора
struct EditorApp {
    /// Shared blueprint data (single source of truth)
    Blueprint blueprint;

    /// Window manager: owns all editor windows (root + sub-blueprint windows)
    WindowManager window_manager{blueprint};

    /// Convenience accessors for the root window
    visual::Scene& scene = window_manager.root().scene;
    CanvasInput& input = window_manager.root().input;

    /// JIT Simulation (manages component lifecycle)
    Simulator<JIT_Solver> simulation;
    bool simulation_running = false;

    /// Type registry (loaded from library/*.blueprint)
    TypeRegistry type_registry;

    /// Context menu state (tracks which window triggered it)
    bool show_context_menu = false;
    Pt context_menu_pos;
    std::string context_menu_group_id;

    /// Node context menu state (right-click on node)
    bool show_node_context_menu = false;
    std::string context_menu_node_id;
    std::string node_context_menu_group_id;  ///< Which window the right-click came from

    /// Color picker state
    bool show_color_picker = false;
    std::string color_picker_node_id;
    std::string color_picker_group_id;  ///< Which window the color picker targets
    float color_picker_rgba[4] = {0.5f, 0.5f, 0.5f, 1.0f};

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
        : inspector(&blueprint)  // Inspector needs blueprint pointer
    {
        // Load type registry at startup
        type_registry = load_type_registry();
    }

    /// Создать новую схему
    void new_circuit() {
        window_manager.closeAll();
        blueprint = Blueprint();
        scene.clear();
        simulation = Simulator<JIT_Solver>();
        simulation_running = false;
    }

    /// Перестроить симуляцию (при изменении схемы)
    void rebuild_simulation() {
        if (simulation_running) {
            // If running, restart to rebuild components
            simulation.stop();
            try {
                simulation.start(blueprint);
            } catch (const std::runtime_error& e) {
                spdlog::error("[sim] Failed to rebuild simulation: {}", e.what());
                simulation_running = false;
            }
        }
        // If not running, components will be built on next start()
    }

    /// Запустить симуляцию
    void start_simulation() {
        if (!simulation_running) {
            try {
                simulation.start(blueprint);  // Creates components!
                simulation_running = true;
            } catch (const std::runtime_error& e) {
                spdlog::error("[sim] Failed to start simulation: {}", e.what());
                simulation.stop();
            }
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
            context_menu_node_id = r.context_menu_node_id;
            node_context_menu_group_id = group_id;
        }
        if (!r.open_sub_window.empty()) open_sub_window(r.open_sub_window);
        if (!r.toggle_switch_node_id.empty()) trigger_switch(r.toggle_switch_node_id);
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

    /// Open a sub-window for a sub-blueprint instance.
    void open_sub_window(const std::string& sub_blueprint_id);

    /// Open properties window for a specific node
    void open_properties_for_node(const std::string& node_id);

    /// Open color picker for a specific node
    void open_color_picker_for_node(const std::string& node_id);

private:
};
