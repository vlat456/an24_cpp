#pragma once

#include "window/window_manager.h"
#include "visual/scene.h"
#include "input/canvas_input.h"
#include "jit_solver/simulator.h"
#include "json_parser/json_parser.h"
#include "visual/render_context.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "blueprint_v2/path/path.h"
#include "ui/core/interned_id.h"
#include <string>
#include <string_view>
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
        return filepath_.empty()
            && model_.current().nodes().empty()
            && model_.current().wires().empty();
    }

    // ── File I/O ──

    bool save(const std::string& path);
    bool load(const std::string& path);

    // ── Blueprint & window access ──

    bp2::Blueprint const& blueprint() const { return model_.current(); }

    bp2::EditorModel& model() { return model_; }
    const bp2::EditorModel& model() const { return model_; }

    ui::StringInterner& interner() { return interner_; }
    bp2::PathArena& arena() { return arena_; }

    bool canUndo() const { return model_.can_undo(); }
    bool canRedo() const { return model_.can_redo(); }
    bool performUndo();
    bool performRedo();

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

    /// Cancel all in-flight gestures, rebuild every window's scene from the
    /// blueprint, then rebuild the simulation.  Use this after any operation
    /// that mutates the blueprint outside of the normal undo/redo path
    /// (e.g. bake-in, addComponent, property edits).
    void rebuildAllWindows();

    void updateSimulationStep(float dt);

    /// Update node_content (gauges, switches, etc.) from simulation values.
    void updateNodeContentFromSimulation();
    void resetNodeContent(const TypeRegistry& registry);

    /// Build a set of wire IDs that are energized (have non-zero voltage).
    void buildEnergizedWireSet(
        std::unordered_set<std::string_view, visual::StringViewHash>& out,
        const std::string& group_id) const;

    // ── Signal overrides (switch/button clicks) ──

    std::unordered_map<std::string, float>& signalOverrides() { return signal_overrides_; }
    std::unordered_set<std::string>& heldButtons() { return held_buttons_; }

    void triggerSwitch(const std::string& node_id);
    void setSliderValue(const std::string& node_id, float value);
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
    // ── Private helpers ──

    /// Build a minimal legacy ::Blueprint from the current bp2::Blueprint,
    /// sufficient for Blueprint::to_simulator_json() (simulation use only).
    Blueprint build_legacy_for_simulation() const;

    /// Extract (node_id, port_name) InternedId pair from a bp2::Path
    /// (expects PathKind::Port with Node parent). Returns empty pair on error.
    /// arena_ is mutable because PathArena::parent() may lazily build cache.
    std::pair<ui::InternedId, ui::InternedId>
    bp2_path_to_node_port(const bp2::Path& path) const;

    // ── Private data ──

    std::string id_;
    std::string filepath_;
    std::string display_name_ = "Untitled";

    ui::StringInterner interner_;
    bp2::PathArena arena_{interner_};
    bp2::EditorModel model_;
    WindowManager window_manager_{model_, interner_, arena_};
    Simulator<JIT_Solver> simulation_;
    bool simulation_running_ = false;

    std::unordered_map<std::string, float> signal_overrides_;
    std::unordered_set<std::string> held_buttons_;

    static int next_id_;
};
