#pragma once

#include "data/blueprint.h"
#include "window/window_manager.h"
#include "visual/scene.h"
#include "input/canvas_input.h"
#include "jit_solver/simulator.h"
#include "json_parser/json_parser.h"
#include <string>
#include <unordered_map>
#include <unordered_set>

/// A single open document: owns a Blueprint + Simulator + WindowManager.
/// Multiple Document instances can coexist for MDI.
class Document {
public:
    /// Create new untitled document
    Document();

    /// Non-copyable, non-movable (owns WindowManager which holds references)
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    Document(Document&&) = delete;
    Document& operator=(Document&&) = delete;

    // ── Identity ──

    const std::string& id() const { return id_; }
    const std::string& filepath() const { return filepath_; }
    const std::string& displayName() const { return display_name_; }

    /// Title for ImGui tab: "filename.blueprint*" if modified
    std::string title() const;

    /// Returns true if this is an untitled, empty document (never saved, no content)
    bool isPristine() const {
        return filepath_.empty() && blueprint_.nodes.empty() && blueprint_.wires.empty();
    }

    // ── File I/O ──

    bool save(const std::string& path);
    bool load(const std::string& path);

    // ── Blueprint & window access ──

    Blueprint& blueprint() { return blueprint_; }
    const Blueprint& blueprint() const { return blueprint_; }

    WindowManager& windowManager() { return window_manager_; }
    const WindowManager& windowManager() const { return window_manager_; }

    /// Root window convenience accessors
    BlueprintWindow& root() { return window_manager_.root(); }
    visual::Scene& scene() { return root().scene; }
    Viewport& viewport() { return root().viewport; }
    CanvasInput& input() { return root().input; }

    // ── Simulation ──

    Simulator<JIT_Solver>& simulation() { return simulation_; }
    const Simulator<JIT_Solver>& simulation() const { return simulation_; }
    bool isSimulationRunning() const { return simulation_running_; }

    void startSimulation();
    void stopSimulation();
    void rebuildSimulation();
    void updateSimulationStep(float dt);

    /// Update node_content (gauges, switches, etc.) from simulation values.
    /// Needs TypeRegistry for reset_node_content logic.
    void updateNodeContentFromSimulation();
    void resetNodeContent(const TypeRegistry& registry);

    /// Build a set of wire IDs that are energized (have non-zero voltage).
    /// Used by the renderer to visually highlight powered wires.
    void buildEnergizedWireSet(std::unordered_set<std::string>& out,
                               const std::string& group_id) const;

    // ── Signal overrides (switch/button clicks) ──

    std::unordered_map<std::string, float>& signalOverrides() { return signal_overrides_; }
    std::unordered_set<std::string>& heldButtons() { return held_buttons_; }

    void triggerSwitch(const std::string& node_id);
    void holdButtonPress(const std::string& node_id);
    void holdButtonRelease(const std::string& node_id);

    // ── Component/blueprint addition ──

    void addComponent(const std::string& classname, Pt world_pos,
                      const std::string& group_id,
                      TypeRegistry& registry);
    void addBlueprint(const std::string& blueprint_name, Pt world_pos,
                      const std::string& group_id,
                      TypeRegistry& registry);

    // ── Sub-windows ──

    void openSubWindow(const std::string& sub_blueprint_id);

    // ── Input result dispatch ──

    /// Apply input result from any window's CanvasInput.
    /// Updates context menu state, rebuilds simulation, etc.
    /// Returns true if context menu / node menu should be shown (caller handles ImGui popup).
    struct InputResultAction {
        bool show_context_menu = false;
        Pt context_menu_pos;
        std::string context_menu_group_id;

        bool show_node_context_menu = false;
        std::string context_menu_node_id;
        std::string node_context_menu_group_id;
    };
    InputResultAction applyInputResult(const InputResult& r, const std::string& group_id = "");

private:
    std::string id_;
    std::string filepath_;
    std::string display_name_ = "Untitled";

    Blueprint blueprint_;
    WindowManager window_manager_{blueprint_};
    Simulator<JIT_Solver> simulation_;
    bool simulation_running_ = false;

    std::unordered_map<std::string, float> signal_overrides_;
    std::unordered_set<std::string> held_buttons_;

    static int next_id_;
};
