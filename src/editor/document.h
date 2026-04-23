#pragma once

#include "window/window_manager.h"
#include "window/window_scope_id.h"
#include "signal_key_resolver.h"
#include "visual/scene.h"
#include "visual/workspace_session.h"
#include "data/node_state.h"
#include "input/canvas_input.h"
#include "document_simulation_internal.h"
#include "core/solvers/jit/simulator.h"
#include "io/json/component_registry_json_loader.h"
#include "blueprint_v2/library/library_index.h"
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
    struct ResolvedSignalScope {
        const bp2::Blueprint* blueprint = nullptr;
        const ui::StringInterner* interner = nullptr;
        editor::SignalKeyContext context = editor::root_signal_context();
    };

    /// Create new untitled document
    Document();

    /// Non-copyable, non-movable (owns WindowManager which holds references)
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    Document(Document&&) = delete;
    Document& operator=(Document&&) = delete;

    // ── Identity ──

    const editor::DocumentId& id() const { return id_; }
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

    // ── Workspace/session persistence (separate from blueprint) ──

    /// Save workspace/session state to a separate .workspace.json file.
    /// Path is derived from blueprint filepath.
    bool saveWorkspaceSession();

    /// Load workspace/session state from a separate .workspace.json file.
    /// Path is derived from blueprint filepath. Returns false if file missing or invalid.
    bool loadWorkspaceSession();

    /// Capture current editor-only workspace/session state.
    [[nodiscard]] WorkspaceSession captureWorkspaceSession() const;

    /// Apply workspace/session state onto the current document/editor windows.
    void applyWorkspaceSession(const WorkspaceSession& session);

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
    void setComponentRegistry(const ComponentRegistry* reg) {
        type_registry_ = reg;
        window_manager_.set_parser_registry(reg);
    }

    /// Get the type registry (may be nullptr if not yet set).
    const ComponentRegistry* type_registry() const { return type_registry_; }

    /// Set the library index used for blueprint path resolution.
    /// Must be called before openSubWindow() or addBlueprint(). Pointer must outlive the Document.
    void setLibraryIndex(const bp2::LibraryIndex* idx) { library_index_ = idx; }

    /// Get the library index (may be nullptr if not yet set).
    const bp2::LibraryIndex* library_index() const { return library_index_; }

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
    void resetNodeContent(const ComponentRegistry& registry);
    void purge_transient_node_state();

    [[nodiscard]] std::optional<editor::NodeColor> node_color_for_scope(const WindowScopeId& scope_id,
                                                                         ui::InternedId node_id) const;
    void set_node_color_for_scope(const WindowScopeId& scope_id,
                                  ui::InternedId node_id,
                                  std::optional<editor::NodeColor> color);

    /// Find a node by id within a scoped blueprint (root, embedded, or external).
    /// Returns nullptr if the scope or node does not exist.
    [[nodiscard]] const bp2::Blueprint::Node* find_node_in_scope(
        const WindowScopeId& scope_id, const editor::NodeId& node_id) const;

    [[nodiscard]] const editor::RuntimeNodeStateStore& runtime_node_states() const { return runtime_node_states_; }

    /// Build a set of wire IDs that are energized (have non-zero voltage).
    void buildEnergizedWireSet(
        std::unordered_set<std::string_view, visual::StringViewHash>& out,
        const WindowScopeId& scope_id) const;

    ResolvedSignalScope resolve_signal_scope(const WindowScopeId& scope_id) const;
    std::string resolve_endpoint_signal_key(const WindowScopeId& scope_id,
                                           std::string_view node_id,
                                           std::string_view port_name) const;
    std::string resolve_wire_signal_key(const WindowScopeId& scope_id,
                                        std::string_view wire_id) const;

    // ── Signal overrides (switch/button clicks) ──

    /// Direct access to typed overrides for edge cases.
    /// InternedId must be resolved against simulation's signal_key_interner().
    std::vector<std::pair<ui::InternedId, float>>& typedOverrides() { return typed_overrides_; }
    std::unordered_map<std::string, ui::InternedId>& heldButtons() { return held_buttons_; }

    void triggerSwitch(const editor::NodeId& node_id, const WindowScopeId& scope_id = WindowScopeId::root());
    void setSliderValue(const editor::NodeId& node_id, float value, const WindowScopeId& scope_id = WindowScopeId::root());
    void setKnobPosition(const editor::NodeId& node_id, int position, const WindowScopeId& scope_id = WindowScopeId::root());
    void holdButtonPress(const editor::NodeId& node_id, const WindowScopeId& scope_id = WindowScopeId::root());
    void holdButtonRelease(const editor::NodeId& node_id, const WindowScopeId& scope_id = WindowScopeId::root());

    // ── Component/blueprint addition ──

    void addComponent(const std::string& classname, Pt world_pos,
                      const WindowScopeId& scope_id,
                      ComponentRegistry& registry);
    void addBlueprint(const std::string& blueprint_name, Pt world_pos,
                      const WindowScopeId& scope_id,
                      ComponentRegistry& registry);

    /// Recompute node sizes from the current layout minimum-size contract.
    /// When preserve_manual is true, nodes explicitly marked manual_size are left unchanged.
    bool normalizeNodeSizesToFit(bool preserve_manual = true);

    bool extractToBlueprint(const std::vector<ui::InternedId>& selected_node_ids,
                           const std::string& blueprint_name,
                           const WindowScopeId& scope_id,
                           std::string* error_out = nullptr,
                           bool allow_nonembedded_descendant_refs = false);

    // ── Sub-windows ──

    void openSubWindow(const WindowScopeId& target_scope);
    void openSubWindow(const WindowScopeId& parent_scope, const std::string& local_node_id);

    /// Open a parent-bound external reference window for a composite node.
    /// Loads the external blueprint and creates a read-only sub-window with
    /// signal keys mapped through the parent instance id.
    void openExternalRefWindow(const WindowScopeId& instance_scope,
                                 const std::string& blueprint_file_path);

    // ── Input result dispatch ──

    struct InputResultAction {
        bool show_context_menu = false;
        Pt context_menu_pos;
        WindowScopeId context_menu_scope_id = WindowScopeId::root();

        bool show_node_context_menu = false;
        editor::NodeId context_menu_node_id;
        WindowScopeId node_context_menu_scope_id = WindowScopeId::root();

        std::string toggle_probe_wire_id;
        WindowScopeId toggle_probe_scope_id = WindowScopeId::root();
        bool has_toggle_probe_world_pos = false;
        Pt toggle_probe_world_pos;

        bool open_inline_value_editor = false;
        editor::NodeId inline_value_editor_node_id;
        WindowScopeId inline_value_editor_scope_id = WindowScopeId::root();
        bool has_inline_value_editor_screen_pos = false;
        Pt inline_value_editor_screen_pos;
    };
    InputResultAction applyInputResult(const InputResult& r, const WindowScopeId& scope_id);

private:
    // ── Private helpers ──

    /// Rebuild every open window scene from its authoritative blueprint source.
    /// Does not cancel gestures and does not rebuild the simulation.
    void rebuild_window_scenes();

    /// Build JitBuildInput directly from current bp2 model (no JSON intermediate).
    /// This is the canonical simulation start path.
    JitBuildInput build_jit_input();

    /// Apply node-size normalization with optional history/window side effects.
    bool apply_normalized_node_sizes(bool preserve_manual,
                                     bool push_checkpoint,
                                     bool rebuild_windows);

    /// Extract (node_id, port_name) InternedId pair from a bp2::Path
    /// (expects PathKind::Port with Node parent). Returns empty pair on error.
    /// arena_ is mutable because PathArena::parent() may lazily build cache.
    std::pair<ui::InternedId, ui::InternedId>
    bp2_path_to_node_port(const bp2::Path& path) const;

    /// Build the pre-resolved signal cache from current blueprint + simulation interner.
    /// Called after simulation_.start(). String work happens here (once), not per-frame.
    void build_signal_cache();

    /// Overload for WireEndpoint — trivially extracts node/port.
    std::pair<ui::InternedId, ui::InternedId>
    bp2_path_to_node_port(const bp2::WireEndpoint& ep) const;

    // ── Private data ──

    editor::DocumentId id_;
    std::string filepath_;
    std::string display_name_ = "Untitled";

    ui::StringInterner interner_;
    bp2::PathArena arena_{interner_};
    bp2::EditorModel model_;
    WindowManager window_manager_{model_, interner_, arena_};
    Simulator<JIT_Solver> simulation_;
    bool simulation_running_ = false;

    // Pre-resolved signal keys — zero allocation per frame.
    // Built at simulation start, cleared on stop.
    editor::SignalCache signal_cache_;

    // Typed signal overrides — InternedId resolved once at interaction time.
    std::vector<std::pair<ui::InternedId, float>> typed_overrides_;

    // Held buttons — key is sim_id (for erase matching), value is pre-resolved
    // control port InternedId (resolved at press time, not per-frame).
    std::unordered_map<std::string, ui::InternedId> held_buttons_;

    editor::RuntimeNodeStateStore runtime_node_states_;
    const ComponentRegistry* type_registry_ = nullptr;
    const bp2::LibraryIndex* library_index_ = nullptr;

    static int next_id_;
};
