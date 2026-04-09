#pragma once

#include "window/window_manager.h"
#include "window/window_scope_id.h"
#include "visual/scene.h"
#include "input/canvas_input.h"
#include "core/solvers/jit/simulator.h"
#include "json_parser/json_parser.h"
#include "visual/render_context.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "blueprint_v2/path/path.h"
#include "ui/core/interned_id.h"
#include "identity.h"
#include <string>
#include <string_view>
#include <vector>
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

    /// Re-seed next wire id counter from current blueprint wires.
    void sync_next_wire_id();

    bool canUndo() const { return model_.can_undo(); }
    bool canRedo() const { return model_.can_redo(); }
    bool performUndo();
    bool performRedo();

    WindowManager& windowManager() { return window_manager_; }
    const WindowManager& windowManager() const { return window_manager_; }

    /// Root window convenience accessors
    BlueprintWindow& root() { return window_manager_.root(); }
    const BlueprintWindow& root() const { return window_manager_.root(); }
    visual::Scene& scene() { return root().scene; }
    Viewport& viewport() { return root().viewport; }
    CanvasInput& input() { return root().input; }

    // ── Simulation ──

    Simulator<JIT_Solver>& simulation() { return simulation_; }
    const Simulator<JIT_Solver>& simulation() const { return simulation_; }
    bool isSimulationRunning() const { return simulation_running_; }

    /// Set the type registry used to filter visual-only params from simulation JSON.
    /// Must be called before startSimulation(). Pointer must outlive the Document.
    void setTypeRegistry(const TypeRegistry* reg) {
        type_registry_ = reg;
        window_manager_.set_parser_registry(reg);
    }

    void startSimulation();
    void stopSimulation();
    void rebuildSimulation();

    /// Cancel all in-flight gestures, rebuild every window's scene from the
    /// blueprint, then rebuild the simulation.  Use this after any operation
    /// that mutates the blueprint outside of the normal undo/redo path
    /// (e.g. bake-in, addComponent, property edits).
    void rebuildAllWindows();

    void updateSimulationStep(double dt);

    /// Update node_content (gauges, switches, etc.) from simulation values.
    void updateNodeContentFromSimulation();
    void resetNodeContent(const TypeRegistry& registry);

    /// Build a set of wire IDs that are energized (have non-zero voltage).
    void buildEnergizedWireSet(
        std::unordered_set<std::string_view, visual::StringViewHash>& out,
        const std::string& scope_id) const;

    /// Build energized wire set for an external-reference window.
    /// Iterates external blueprint wires and maps signal keys through parent_instance_id.
    void buildEnergizedWireSetExternal(
        std::unordered_set<std::string_view, visual::StringViewHash>& out,
        const bp2::Blueprint& external_bp,
        ui::StringInterner& external_interner,
        bp2::PathArena& external_arena,
        const std::string& parent_instance_id) const;

    // ── Signal overrides (switch/button clicks) ──

    std::unordered_map<std::string, float>& signalOverrides() { return signal_overrides_; }
    std::unordered_set<std::string>& heldButtons() { return held_buttons_; }

    void triggerSwitch(const editor::NodeId& node_id, const std::string& scope_id = "");
    void setSliderValue(const editor::NodeId& node_id, float value, const std::string& scope_id = "");
    void setKnobPosition(const editor::NodeId& node_id, int position, const std::string& scope_id = "");
    void holdButtonPress(const editor::NodeId& node_id, const std::string& scope_id = "");
    void holdButtonRelease(const editor::NodeId& node_id, const std::string& scope_id = "");

    // ── Component/blueprint addition ──

    void addComponent(const std::string& classname, Pt world_pos,
                      const std::string& scope_id,
                      TypeRegistry& registry);
    void addBlueprint(const std::string& blueprint_name, Pt world_pos,
                      const std::string& scope_id,
                      TypeRegistry& registry);

    bool extractToBlueprint(const std::vector<ui::InternedId>& selected_node_ids,
                           const std::string& blueprint_name,
                           const WindowScopeId& scope_id,
                           std::string* error_out = nullptr,
                           bool allow_nonembedded_descendant_refs = false);

    // ── Sub-windows ──

    void openSubWindow(const std::string& sub_blueprint_id);

    /// Open a parent-bound external reference window for a composite node.
    /// Loads the external blueprint and creates a read-only sub-window with
    /// signal keys mapped through the parent instance id.
    void openExternalRefWindow(const std::string& instance_id,
                                const std::string& blueprint_file_path);

    // ── Input result dispatch ──

    struct InputResultAction {
        bool show_context_menu = false;
        Pt context_menu_pos;
        std::string context_menu_scope_id;

        bool show_node_context_menu = false;
        editor::NodeId context_menu_node_id;
        std::string node_context_menu_scope_id;

        std::string toggle_probe_wire_id;
        WindowScopeId toggle_probe_scope_id = WindowScopeId::root();
        bool has_toggle_probe_world_pos = false;
        Pt toggle_probe_world_pos;

        bool open_inline_value_editor = false;
        editor::NodeId inline_value_editor_node_id;
        bool has_inline_value_editor_screen_pos = false;
        Pt inline_value_editor_screen_pos;
    };
    InputResultAction applyInputResult(const InputResult& r, const std::string& scope_id = "");
    InputResultAction applyInputResult(const InputResult& r, const WindowScopeId& scope_id);

private:
    // ── Private helpers ──

    /// Build simulator JSON from current bp2 model.
    std::string build_simulation_json();

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
    const TypeRegistry* type_registry_ = nullptr;

    static int next_id_;
};
