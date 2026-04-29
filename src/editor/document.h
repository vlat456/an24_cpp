#pragma once

#include "window/window_manager.h"
#include "window/window_scope_id.h"
#include "simulation_bridge.h"
#include "scope_resolver.h"
#include "rendering_resources.h"
#include "visual/scene.h"
#include "visual/workspace_session.h"
#include "data/node_state.h"
#include "input/canvas_input.h"
#include "io/json/component_registry_json_loader.h"
#include "blueprint_v2/library/library_index.h"
#include "visual/render_context.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "blueprint_v2/layout/auto_layout.h"
#include "blueprint_v2/path/path.h"
#include "core/strings/interned_id.h"
#include "identity.h"
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <unordered_set>

/// A single open document: owns EditorModel + WindowManager + SimulationBridge.
///
/// Document is a coordinator — it decides WHEN to start/stop simulation,
/// WHEN to rebuild windows, WHEN to save/load. The HOW is delegated:
/// - SimulationBridge — owns simulator, signal caches, interaction binding
/// - WindowManager — owns windows, scenes, viewports
/// - EditorModel — owns blueprint state, undo/redo
class Document {
public:
    /// Create document with optional type registry, library index, and rendering resources.
    /// All are passed at construction (not two-phase init).
    explicit Document(const ComponentRegistry* type_registry = nullptr,
                      const bp2::LibraryIndex* library_index = nullptr,
                      const editor::RenderingResources* rendering_resources = nullptr);

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

    bool saveWorkspaceSession();
    bool loadWorkspaceSession();
    [[nodiscard]] WorkspaceSession captureWorkspaceSession() const;
    void applyWorkspaceSession(const WorkspaceSession& session);

    // ── Blueprint & window access ──

    bp2::Blueprint const& blueprint() const { return model_.current(); }
    bp2::EditorModel& model() { return model_; }
    const bp2::EditorModel& model() const { return model_; }
    core::StringInterner& interner() { return interner_; }
    bp2::PathArena& arena() { return arena_; }

    void sync_next_wire_id();

    bool canUndo() const { return model_.can_undo(); }
    bool canRedo() const { return model_.can_redo(); }
    bool performUndo();
    bool performRedo();

    WindowManager& windowManager() { return window_manager_; }
    const WindowManager& windowManager() const { return window_manager_; }

    BlueprintWindow& root() { return window_manager_.root(); }
    const BlueprintWindow& root() const { return window_manager_.root(); }
    visual::Scene& scene() { return root().scene; }
    Viewport& viewport() { return root().viewport; }
    CanvasInput& input() { return root().input; }

    // ── Simulation (delegates to SimulationBridge) ──

    bool isSimulationRunning() const { return sim_bridge_.is_running(); }

    /// Read a signal value by pre-resolved InternedId.
    float get_signal_value(core::InternedId key) const { return sim_bridge_.get_signal_value(key); }

    /// Access the simulation's signal key interner for key lookup.
    const core::StringInterner& signal_key_interner() const { return sim_bridge_.signal_key_interner(); }

    const ComponentRegistry* type_registry() const { return type_registry_; }
    const bp2::LibraryIndex* library_index() const { return library_index_; }
    const editor::RenderingResources* rendering_resources() const { return rendering_resources_; }

    /// Null-safe icon font extraction from rendering resources.
    const editor::IconFont* icon_font() const {
        return rendering_resources_ ? rendering_resources_->icon_font : nullptr;
    }

    void startSimulation();
    void stopSimulation();
    void rebuildSimulation();

    /// Cancel gestures, rebuild every window's scene, then rebuild simulation.
    void rebuildAllWindows();

    void updateSimulationStep(double dt);
    void updateNodeContentFromSimulation();
    void resetNodeContent(const ComponentRegistry& registry);
    void purge_transient_node_state();

    // ── Node appearance (Document-owning, not simulation) ──

    [[nodiscard]] std::optional<editor::NodeColor> node_color_for_scope(
        const WindowScopeId& scope_id, core::InternedId node_id) const;
    void set_node_color_for_scope(const WindowScopeId& scope_id,
                                   core::InternedId node_id,
                                   std::optional<editor::NodeColor> color);

    /// Find a node by id within a scoped blueprint (delegates to free function).
    [[nodiscard]] const bp2::Blueprint::Node* find_node_in_scope(
        const WindowScopeId& scope_id, core::InternedId node_id) const {
        return editor::find_node_in_scope(scope_id, node_id, model_, window_manager_, interner_);
    }

    [[nodiscard]] const editor::RuntimeNodeStateStore& runtime_node_states() const {
        return sim_bridge_.runtime_node_states();
    }

    void buildEnergizedWireSet(
        std::unordered_set<std::string_view, visual::StringViewHash>& out,
        const WindowScopeId& scope_id) const {
        sim_bridge_.build_energized_wire_set(out, scope_id);
    }

    // ── Signal overrides (delegates to SimulationBridge) ──

    std::vector<std::pair<core::InternedId, float>>& typedOverrides() {
        return sim_bridge_.typed_overrides();
    }

    void triggerSwitch(core::InternedId node_id, const WindowScopeId& scope_id = WindowScopeId::root()) {
        sim_bridge_.trigger_switch(node_id, scope_id);
    }
    void setSliderValue(core::InternedId node_id, float value, const WindowScopeId& scope_id = WindowScopeId::root()) {
        sim_bridge_.set_slider_value(node_id, value, scope_id);
    }
    void setKnobPosition(core::InternedId node_id, int position, const WindowScopeId& scope_id = WindowScopeId::root()) {
        sim_bridge_.set_knob_position(node_id, position, scope_id);
    }
    void holdButtonPress(core::InternedId node_id, const WindowScopeId& scope_id = WindowScopeId::root()) {
        sim_bridge_.hold_button_press(node_id, scope_id);
    }
    void holdButtonRelease(core::InternedId node_id, const WindowScopeId& scope_id = WindowScopeId::root()) {
        sim_bridge_.hold_button_release(node_id, scope_id);
    }

    // ── Signal key resolution ──

    editor::ResolvedScope resolve_signal_scope(const WindowScopeId& scope_id) const {
        return editor::resolve_scope(scope_id, model_, window_manager_, interner_);
    }
    core::InternedId resolve_endpoint_signal_key(const WindowScopeId& scope_id,
                                                std::string_view node_id,
                                                std::string_view port_name) const {
        return sim_bridge_.resolve_endpoint_signal_key(scope_id, node_id, port_name);
    }
    core::InternedId resolve_wire_signal_key(const WindowScopeId& scope_id,
                                            std::string_view wire_id) const {
        return sim_bridge_.resolve_wire_signal_key(scope_id, wire_id);
    }

    // ── Component/blueprint addition ──

    void addComponent(const std::string& classname, Pt world_pos,
                      const WindowScopeId& scope_id,
                      ComponentRegistry& registry);
    void addBlueprint(const std::string& blueprint_name, Pt world_pos,
                      const WindowScopeId& scope_id,
                      ComponentRegistry& registry);

    bool normalizeNodeSizesToFit(bool preserve_manual = true);

    /// Auto-layout the root blueprint. Pushes a single undo checkpoint.
    bool autoLayout(const bp2::layout::LayoutOptions& options = {});

    /// Auto-layout an embedded blueprint at the given scope.
    /// Handles undo checkpoint and propagation automatically.
    bool autoLayoutEmbedded(const WindowScopeId& scope_id,
                            const bp2::layout::LayoutOptions& options = {});

    bool extractToBlueprint(const std::vector<core::InternedId>& selected_node_ids,
                           const std::string& blueprint_name,
                           const WindowScopeId& scope_id,
                           std::string* error_out = nullptr,
                           bool allow_nonembedded_descendant_refs = false);

    // ── Sub-windows ──

    void openSubWindow(const WindowScopeId& target_scope);
    void openSubWindow(const WindowScopeId& parent_scope, core::InternedId local_node_id);
    void openExternalRefWindow(const WindowScopeId& instance_scope,
                                 const std::string& blueprint_file_path);

    // ── Input result dispatch ──

    struct InputResultAction {
        bool show_context_menu = false;
        Pt context_menu_pos;
        WindowScopeId context_menu_scope_id = WindowScopeId::root();

        bool show_node_context_menu = false;
        core::InternedId context_menu_node_id;
        WindowScopeId node_context_menu_scope_id = WindowScopeId::root();

        core::InternedId toggle_probe_wire_id;
        WindowScopeId toggle_probe_scope_id = WindowScopeId::root();
        bool has_toggle_probe_world_pos = false;
        Pt toggle_probe_world_pos;

        bool open_inline_value_editor = false;
        core::InternedId inline_value_editor_node_id;
        WindowScopeId inline_value_editor_scope_id = WindowScopeId::root();
        bool has_inline_value_editor_screen_pos = false;
        Pt inline_value_editor_screen_pos;
    };
    InputResultAction applyInputResult(const InputResult& r, const WindowScopeId& scope_id);

private:
    void rebuild_window_scenes();

    bool apply_normalized_node_sizes(bool preserve_manual,
                                     bool push_checkpoint,
                                     bool rebuild_windows);

    // ── Private data ──
    // Member order matters: type_registry_ and library_index_ MUST come before
    // window_manager_ because WindowManager uses them during construction.

    editor::DocumentId id_;
    std::string filepath_;
    std::string display_name_ = "Untitled";

    const ComponentRegistry* type_registry_ = nullptr;
    const bp2::LibraryIndex* library_index_ = nullptr;
    const editor::RenderingResources* rendering_resources_ = nullptr;

    core::StringInterner interner_;
    bp2::PathArena arena_{interner_};
    bp2::EditorModel model_;
    WindowManager window_manager_{model_, interner_, arena_, type_registry_,
                                    rendering_resources_ ? rendering_resources_->icon_font : nullptr};

    /// Simulation binding — owns simulator, signal caches, interaction state.
    /// Receives stable references to model_, window_manager_, interner_, arena_.
    SimulationBridge sim_bridge_{model_, window_manager_, interner_, arena_};

    static int next_id_;
};
