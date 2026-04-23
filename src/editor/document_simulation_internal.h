#pragma once

/// Simulation-adjacent internal utilities.
/// These are used by the simulation step, node content updates, and
/// blueprint-level node walking. For embedded path resolution and mutation,
/// see embedded_path_utils.h.

#include "blueprint_v2/blueprint/blueprint.h"
#include "data/node_state.h"
#include "ui/core/interned_id.h"
#include "window/window_scope_id.h"

#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace editor {

/// Select the appropriate readback port for a slider-type node.
std::optional<std::string_view> select_slider_readback_port(const bp2::Blueprint::Node& node,
                                                             ui::StringInterner& interner);

/// Recursively walk all nodes in a blueprint (including embedded blueprints),
/// building a typed instance_path as the recursion descends. The callback
/// receives each node and its current instance_path.
void walk_blueprint_nodes(
    const bp2::Blueprint& bp,
    std::vector<ui::InternedId>& instance_path,
    const std::function<void(const bp2::Blueprint::Node&, std::span<const ui::InternedId>)>& fn);

/// Convert a typed instance path to a colon-delimited scope string for
/// simulation signal key construction. Returns "" for root scope (empty path).
inline std::string instance_path_to_scope_string(
    const ui::StringInterner& interner,
    std::span<const ui::InternedId> path) {
    if (path.empty()) return "";
    std::string result;
    for (size_t i = 0; i < path.size(); ++i) {
        if (i > 0) result += ':';
        result += interner.resolve(path[i]);
    }
    return result;
}

// ===========================================================================
// NodeSignalCache — pre-resolved InternedIds for per-frame signal reads
// ===========================================================================
//
// Built once at simulation start by walking all animated nodes and resolving
// their signal port InternedIds against the build-scoped StringInterner.
// The per-frame update loop reads cached InternedIds directly — zero string
// construction, zero hash table lookup.
//
// Empty InternedId fields mean the port doesn't exist for this node type.
// get_signal_value(empty) returns 0.0f, so reads on absent ports are safe.

struct NodeSignalCache {
    // Readback ports (empty = port doesn't exist for this node type)
    ui::InternedId state;           ///< Switch, HoldButton, AZS
    ui::InternedId brightness;      ///< IndicatorLight
    ui::InternedId tripped;         ///< AZS
    ui::InternedId v_in;            ///< Voltmeter
    ui::InternedId slider_readback; ///< Slider (either "out" or "control")
    ui::InternedId position;        ///< KnobSwitch, RotarySwitch*

    // Override port
    ui::InternedId control;         ///< Switch, HoldButton, Slider, KnobSwitch

    // Metadata for dispatch (avoids type-name string comparison)
    bp2::NodeContentType content_type = bp2::NodeContentType::None;
    bool is_azs = false;

    // Pre-built scope for widget dispatch — constructed once at cache build,
    // reused every frame. Eliminates per-frame string → WindowScopeId conversion.
    WindowScopeId scope = WindowScopeId::root();
};

/// Signal cache: maps NodeInstanceKey → pre-resolved InternedId ports.
/// Built at simulation start, cleared on stop.
using SignalCache = std::unordered_map<NodeInstanceKey, NodeSignalCache, NodeInstanceKeyHash>;

} // namespace editor
