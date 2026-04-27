#pragma once

/// Shared element extraction functions for nodal domains.
///
/// Single source of truth for "what does each SolverRoleKind need?"
/// Parameterized by an Adapter type that provides resolution strategy.
/// Both JIT and AOT instantiate these templates with their own adapter.
///
/// Dependencies: nodal_types.h, component_types.h. Zero solver/device awareness.
///
/// Adapter contract (duck-typed, concept-verified):
///   float read_param(const SolverRole& role, const char* key, float default_val)
///     JIT: throws on missing param. AOT: returns default_val.
///
///   std::optional<uint32_t> resolve_port(const SolverRole& role, const char* key)
///     Both: throws if key not in role.port_map (configuration error).
///     JIT: throws if port not in signal map (dead code after throw).
///     AOT: returns nullopt if port not in signal map (skip element).
///
///   void emit(NodalElementKind kind, uint32_t node_a, uint32_t node_b,
///             float value_a, float value_b)
///     Pushes one element to the output. Adapter handles element_id
///     increment, device_name, and any path-specific fields.

#include "core/solvers/common/nodal_types.h"
#include "core/model/component_types.h"

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <optional>
namespace build_algo {

// == Shared constants ==

/// Maximum throw positions a KnobSwitch can have.
static constexpr int MAX_KNOB_POSITIONS = 5;

/// Terminal names indexed by throw position (0-based).
static constexpr const char* KNOB_TERMINAL_NAMES[] = {
    "throw1", "throw2", "throw3", "throw4", "throw5"
};

// =====================================================================
// Adapter concept — compile-time verification of the contract.
// =====================================================================

template<typename A>
concept ExtractionAdapter = requires(A& a, const SolverRole& role, const char* key,
                                     float dv, NodalElementKind k, uint32_t n) {
    { a.read_param(role, key, dv) } -> std::same_as<float>;
    { a.resolve_port(role, key) } -> std::same_as<std::optional<uint32_t>>;
    { a.emit(k, n, n, dv, dv) } -> std::same_as<void>;
};

// =====================================================================
// Extraction functions — one per SolverRoleKind.
// =====================================================================

/// FixedVoltageNode / FixedPressureNode: one boundary element.
template<ExtractionAdapter Adapter>
void extract_fixed_node(Adapter& adapter, const SolverRole& role) {
    float value = adapter.read_param(role, "voltage", 0.0f);
    auto node = adapter.resolve_port(role, "node");
    if (!node.has_value()) return;
    adapter.emit(NodalElementKind::FixedNode, *node, UINT32_MAX, value, 0.0f);
}

/// TheveninSource / PressureSource: source potential + internal resistance.
template<ExtractionAdapter Adapter>
void extract_thevenin_source(Adapter& adapter, const SolverRole& role) {
    float voltage = adapter.read_param(role, "voltage", 28.0f);
    float resistance = adapter.read_param(role, "resistance", 0.01f);
    auto pos = adapter.resolve_port(role, "pos");
    auto neg = adapter.resolve_port(role, "neg");
    if (!pos.has_value() || !neg.has_value()) return;
    adapter.emit(NodalElementKind::Source, *pos, *neg, voltage, resistance);
}

/// ConductanceBranch / FlowBranch: single conductance between two nodes.
template<ExtractionAdapter Adapter>
void extract_conductance_branch(Adapter& adapter, const SolverRole& role) {
    float g = adapter.read_param(role, "g", 0.1f);
    auto a = adapter.resolve_port(role, "a");
    auto b = adapter.resolve_port(role, "b");
    if (!a.has_value() || !b.has_value()) return;
    adapter.emit(NodalElementKind::Branch, *a, *b, g, 0.0f);
}

/// KnobSwitchBranches: N branches (wiper → throw_i), one per position.
/// Multi-element extractor — may emit 2..MAX_KNOB_POSITIONS elements.
template<ExtractionAdapter Adapter>
void extract_knob_switch(Adapter& adapter, const SolverRole& role) {
    int positions = static_cast<int>(adapter.read_param(role, "positions", 3.0f));
    positions = std::clamp(positions, 2, MAX_KNOB_POSITIONS);
    int initial_pos = static_cast<int>(
        adapter.read_param(role, "initial_position", 0.0f));
    initial_pos = std::clamp(initial_pos, 0, positions - 1);
    float g_open_val = adapter.read_param(role, "g_open", 1e-9f);
    float g_closed_val = adapter.read_param(role, "g_closed", 0.1f);

    auto node_wiper = adapter.resolve_port(role, "wiper");
    if (!node_wiper.has_value()) return;

    for (int i = 0; i < positions; ++i) {
        auto node_t = adapter.resolve_port(role, KNOB_TERMINAL_NAMES[i]);
        if (!node_t.has_value()) continue;
        float g = (i == initial_pos) ? g_closed_val : g_open_val;
        adapter.emit(NodalElementKind::Branch, *node_wiper, *node_t, g, 0.0f);
    }
}

/// FixedPressureNode: boundary element with "pressure" param key.
template<ExtractionAdapter Adapter>
void extract_fixed_pressure(Adapter& adapter, const SolverRole& role) {
    float value = adapter.read_param(role, "pressure", 0.0f);
    auto node = adapter.resolve_port(role, "node");
    if (!node.has_value()) return;
    adapter.emit(NodalElementKind::FixedNode, *node, UINT32_MAX, value, 0.0f);
}

/// PressureSource: source with "pressure" param key.
template<ExtractionAdapter Adapter>
void extract_pressure_source(Adapter& adapter, const SolverRole& role) {
    float pressure = adapter.read_param(role, "pressure", 0.0f);
    float resistance = adapter.read_param(role, "resistance", 0.01f);
    auto pos = adapter.resolve_port(role, "pos");
    auto neg = adapter.resolve_port(role, "neg");
    if (!pos.has_value() || !neg.has_value()) return;
    adapter.emit(NodalElementKind::Source, *pos, *neg, pressure, resistance);
}

/// FlowBranch reuses extract_conductance_branch — identical param/port keys.
/// (Listed here for discoverability; table entry points to extract_conductance_branch.)

// =====================================================================
// Extractor table — maps SolverRoleKind → extraction function.
// =====================================================================

template<typename Adapter>
using ExtractFn = void(*)(Adapter&, const SolverRole&);

template<typename Adapter>
struct ExtractorEntry {
    SolverRoleKind kind;
    ExtractFn<Adapter> fn;
};

/// Find extractor by SolverRoleKind. Linear scan over small array.
template<typename Adapter>
const ExtractorEntry<Adapter>* find_extractor(
    const ExtractorEntry<Adapter>* table, size_t count, SolverRoleKind kind)
{
    for (size_t i = 0; i < count; ++i) {
        if (table[i].kind == kind) return &table[i];
    }
    return nullptr;
}

// ---- Electrical extractor table (electrical domain) ----

template<typename Adapter>
static constexpr ExtractorEntry<Adapter> k_electrical_extractors[] = {
    {SolverRoleKind::FixedVoltageNode,   &extract_fixed_node<Adapter>},
    {SolverRoleKind::TheveninSource,     &extract_thevenin_source<Adapter>},
    {SolverRoleKind::ConductanceBranch,  &extract_conductance_branch<Adapter>},
    {SolverRoleKind::KnobSwitchBranches, &extract_knob_switch<Adapter>},
};

// ---- Pressure extractor table (hydraulic + pneumatic domains) ----

template<typename Adapter>
static constexpr ExtractorEntry<Adapter> k_pressure_extractors[] = {
    {SolverRoleKind::FixedPressureNode,  &extract_fixed_pressure<Adapter>},
    {SolverRoleKind::PressureSource,     &extract_pressure_source<Adapter>},
    {SolverRoleKind::FlowBranch,         &extract_conductance_branch<Adapter>},
};

/// Run a single device's extraction via the appropriate extractor.
/// Returns false if no extractor found for this kind.
template<ExtractionAdapter Adapter>
bool extract_with_table(
    Adapter& adapter,
    const ExtractorEntry<Adapter>* table, size_t count,
    const SolverRole& role)
{
    const auto* ext = find_extractor(table, count, role.kind);
    if (!ext) return false;
    ext->fn(adapter, role);
    return true;
}

} // namespace build_algo
