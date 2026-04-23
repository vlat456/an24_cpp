#pragma once

/// Simulation-adjacent internal utilities.
/// These are used by the simulation step, node content updates, and
/// blueprint-level node walking. For embedded path resolution and mutation,
/// see embedded_path_utils.h.

#include "blueprint_v2/blueprint/blueprint.h"
#include "data/node_content.h"
#include "data/node_state.h"
#include "ui/core/interned_id.h"
#include "window/window_scope_id.h"

#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <variant>
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
// Content-typed port caches — per-content-type resolved InternedIds
// ===========================================================================
//
// Each content type (Gauge, Indicator, Switch, Slider, Knob) gets its own
// port struct with exactly the ports it needs. This eliminates cross-type
// contamination — a Gauge cannot accidentally read a "brightness" port,
// and AZS-specific ports ("tripped") don't pollute the generic path.
//
// Built once at simulation start by walking all animated nodes and resolving
// their signal port InternedIds against the build-scoped StringInterner.
// The per-frame update loop reads cached InternedIds directly — zero string
// construction, zero hash table lookup.

struct GaugePorts {
    ui::InternedId v_in;
};

struct IndicatorPorts {
    ui::InternedId brightness;
};

struct SwitchPorts {
    ui::InternedId state;
    ui::InternedId control;
};

struct AzsPorts {
    ui::InternedId state;
    ui::InternedId control;
    ui::InternedId tripped;
};

struct SliderPorts {
    ui::InternedId readback;   ///< Either "out" or "control", resolved at cache build
    ui::InternedId control;
};

struct KnobPorts {
    ui::InternedId position;
    ui::InternedId control;
};

/// Discriminated union of per-content-type port sets.
/// std::monostate = unhandled/non-animated content type.
using ContentPorts = std::variant<
    std::monostate,
    GaugePorts,
    IndicatorPorts,
    SwitchPorts,
    AzsPorts,
    SliderPorts,
    KnobPorts
>;

struct NodeSignalCache {
    /// Static content from blueprint params — populated once at cache build.
    /// Contains min, max (positions for Knob), label, unit, etc.
    /// Per-frame code copies this and overlays dynamic simulation values.
    NodeContent base_content;

    /// Content-type-specific resolved ports. The variant tag determines
    /// which ports are read per-frame — no dead fields, no cross-contamination.
    ContentPorts ports;

    /// Pre-built scope for widget dispatch — constructed once at cache build,
    /// reused every frame. Eliminates per-frame string → WindowScopeId conversion.
    WindowScopeId scope = WindowScopeId::root();
};

/// Signal cache: maps NodeInstanceKey → pre-resolved InternedId ports.
/// Built at simulation start, cleared on stop.
using SignalCache = std::unordered_map<NodeInstanceKey, NodeSignalCache, NodeInstanceKeyHash>;

/// Pre-resolved wire energization InternedId — built once at simulation start.
/// Maps wire InternedId → signal InternedId for the wire's source endpoint.
/// buildEnergizedWireSet() iterates this cache per-frame — zero resolver calls.
using WireSignalCache = std::unordered_map<ui::InternedId, ui::InternedId, std::hash<ui::InternedId>>;

} // namespace editor
