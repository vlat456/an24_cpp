#pragma once

/// Signal allocation via UnionFind — the single source of truth.
///
/// Given a set of devices, bridge ports, and connections, assigns compact
/// signal indices such that connected ports share an index. Used by:
///   - JIT JSON production path (jit_solver.cpp::compute_signal_mapping)
///   - AOT codegen (codegen_composite.cpp)
///   - JIT test infrastructure (jit_build_input_test_helper.h)
///   - AOT↔JIT parity tests (test_electrical_parity_fixtures.cpp, etc.)
///
/// Three-phase pipeline:
///   1. build_port_index_map()      — enumerate all ports, assign sequential indices
///   2. apply_signal_allocation_rules() — union connected ports via UnionFind
///   3. finalize_signal_indices()   — compact UnionFind roots to dense signal range

#include "signal_key.h"
#include "signal_union_rules.h"
#include "core/utils/union_find.h"
#include "core/model/component_types.h"
#include "core/model/connection.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace signal_alloc {

using UnionFind = core::utils::UnionFind;

/// Phase 1: Enumerate all ports across devices and bridge ports.
/// Populates `out_all_ports` (ordered list) and `out_port_to_idx` (name → flat index).
template <typename DeviceT>
void build_port_index_map(
    const std::vector<DeviceT>& devices,
    const std::vector<BridgePortDefinition>& bridge_ports,
    std::vector<std::string>& out_all_ports,
    std::unordered_map<std::string, uint32_t>& out_port_to_idx
) {
    for (const auto& dev : devices) {
        for (const auto& [port_name, port] : dev.ports) {
            (void)port;
            std::string full_port = signal_key::make_node_port_key(dev.name, port_name);
            uint32_t idx = static_cast<uint32_t>(out_all_ports.size());
            out_all_ports.push_back(full_port);
            out_port_to_idx[full_port] = idx;
        }
    }

    for (const auto& bridge : bridge_ports) {
        const std::string ext_port = signal_key::make_bridge_external_key(bridge.id);
        const std::string port_port = signal_key::make_bridge_internal_key(bridge.id);

        uint32_t idx = static_cast<uint32_t>(out_all_ports.size());
        out_all_ports.push_back(ext_port);
        out_port_to_idx[ext_port] = idx;

        idx = static_cast<uint32_t>(out_all_ports.size());
        out_all_ports.push_back(port_port);
        out_port_to_idx[port_port] = idx;

        const std::string exposed = signal_union_rules::bridge_exposed_key(bridge);
        if (!exposed.empty() && !out_port_to_idx.count(exposed)) {
            idx = static_cast<uint32_t>(out_all_ports.size());
            out_all_ports.push_back(exposed);
            out_port_to_idx[exposed] = idx;
        }
    }
}

/// Phase 2: Apply all union rules — wires, bridges, aliases.
/// After this, `uf.find(i)` gives the canonical root for each port index.
/// ConnectionT: any type with connection_from()/connection_to() overloads
///   (Connection, std::pair<std::string,string>, etc.)
/// OnMissingFn: callback for unresolved wire endpoints.
template <typename DeviceT, typename ConnectionT = Connection, typename OnMissingFn = decltype([](const std::string&, const std::string&, bool, bool) {})>
void apply_signal_allocation_rules(
    UnionFind& uf,
    const std::vector<DeviceT>& devices,
    const std::vector<BridgePortDefinition>& bridge_ports,
    const std::vector<ConnectionT>& connections,
    const std::unordered_map<std::string, uint32_t>& port_to_idx,
    OnMissingFn&& on_missing = {}
) {
    signal_union_rules::apply_structural_bridge_unions(uf, bridge_ports, port_to_idx);
    signal_union_rules::apply_signal_union_rules(
        uf,
        devices,
        connections,
        port_to_idx,
        std::forward<OnMissingFn>(on_missing)
    );
}

/// Phase 3: Compact UnionFind roots to a dense signal range [0, signal_count).
/// Returns port_name → signal_index map.
/// Sets `out_signal_count` to the number of unique signals + 1 sentinel.
std::unordered_map<std::string, uint32_t> finalize_signal_indices(
    const UnionFind& uf,
    const std::vector<std::string>& all_ports,
    const std::unordered_map<std::string, uint32_t>& port_to_idx,
    uint32_t& out_signal_count
);

} // namespace signal_alloc
