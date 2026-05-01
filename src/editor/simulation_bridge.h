#pragma once

/// SimulationBridge — owns all simulation binding state and lifecycle.
///
/// Extracted from Document (god object decomposition). Document decides WHEN
/// to start/stop simulation; SimulationBridge handles the HOW. Owns:
/// - Simulator instance (via PIMPL — header does not expose JIT internals)
/// - Signal caches (pre-resolved InternedIds, zero allocation per frame)
/// - Interaction→simulation binding (switch/slider/knob/button overrides)
/// - Runtime node states (live slider positions, switch states, etc.)
///
/// Document retains: file I/O, blueprint mutation, window management,
/// undo/redo, input dispatch, component addition.
///
/// PIMPL isolation: all private state lives in Impl (defined in .cpp).
/// The header only exposes the public API and lightweight types needed
/// by callers (InternedId, WindowScopeId, RuntimeNodeStateStore).

#include "window/window_scope_id.h"
#include "core/strings/interned_id.h"
#include "data/node_state.h"
#include "visual/string_view_hash.h"
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

// Forward declarations — full definitions only needed in .cpp
struct JitBuildInput;
namespace bp2 { class EditorModel; class PathArena; struct LibraryIndex; }
class WindowManager;
class ComponentRegistry;
class SimvarProviderHost;

class SimulationBridge {
public:
    /// Construct with stable references to Document-owned services.
    /// All references must outlive this object (owned by same Document).
    SimulationBridge(bp2::EditorModel& model,
                     WindowManager& window_manager,
                     core::StringInterner& interner,
                     bp2::PathArena& arena);
    ~SimulationBridge();

    // Non-copyable, non-movable (PIMPL with references)
    SimulationBridge(const SimulationBridge&) = delete;
    SimulationBridge& operator=(const SimulationBridge&) = delete;
    SimulationBridge(SimulationBridge&&) = delete;
    SimulationBridge& operator=(SimulationBridge&&) = delete;

    // ── Lifecycle ──

    void start(const JitBuildInput& input);
    void stop();
    void rebuild(const JitBuildInput& input);
    [[nodiscard]] bool is_running() const;

    // ── Per-frame ──

    void step(double dt);
    void update_node_content();

    // ── JitBuildInput factory ──

    /// Build JitBuildInput from current model + registry.
    /// Called by Document before start/rebuild.
    [[nodiscard]] JitBuildInput build_jit_input(const ComponentRegistry* type_registry,
                                                  const bp2::LibraryIndex* library_index);

    // ── Interaction → simulation binding ──

    void trigger_switch(core::InternedId node_id, const WindowScopeId& scope_id);
    void set_slider_value(core::InternedId node_id, float value, const WindowScopeId& scope_id);
    void set_knob_position(core::InternedId node_id, int position, const WindowScopeId& scope_id);
    void hold_button_press(core::InternedId node_id, const WindowScopeId& scope_id);
    void hold_button_release(core::InternedId node_id, const WindowScopeId& scope_id);

    /// Direct access to typed overrides (for edge cases like PI tuner).
    std::vector<std::pair<core::InternedId, float>>& typed_overrides();

    // ── Signal queries (replaces direct Simulator access) ──

    /// Read a signal value by pre-resolved InternedId.
    [[nodiscard]] float get_signal_value(core::InternedId key) const;

    /// Access the simulation's signal key interner for key lookup.
    [[nodiscard]] const core::StringInterner& signal_key_interner() const;

    /// Access the provider host for adapter management (menu UI).
    [[nodiscard]] SimvarProviderHost* provider_host();
    [[nodiscard]] const SimvarProviderHost* provider_host() const;

    // ── Scope-based signal key resolution ──

    [[nodiscard]] core::InternedId resolve_endpoint_signal_key(
        const WindowScopeId& scope_id,
        std::string_view node_id,
        std::string_view port_name) const;
    [[nodiscard]] core::InternedId resolve_wire_signal_key(
        const WindowScopeId& scope_id,
        std::string_view wire_id) const;

    // ── Energized wire set ──

    void build_energized_wire_set(
        std::unordered_set<std::string_view, visual::StringViewHash>& out,
        const WindowScopeId& scope_id) const;

    // ── Runtime node states ──

    /// Used by scene_mutations, document_layout, document_io for content restoration.
    [[nodiscard]] const editor::RuntimeNodeStateStore& runtime_node_states() const;

    /// Reset runtime node states from blueprint (e.g. after load).
    void reset_node_content();

    /// Remove runtime states for nodes that no longer exist in the blueprint.
    void purge_transient_node_state();

    // ── Configuration ──

    void set_type_registry(const ComponentRegistry* reg);
    void set_windows_simulation_mode(bool running);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
