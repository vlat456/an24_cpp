#pragma once

#include "codegen_internal.h"
#include "../common/signal_union_rules.h"
#include "../../utils/union_find.h"

namespace codegen_composite_detail {

using UnionFind = core::utils::UnionFind;

template <typename DeviceT>
void build_port_index_map(
    const std::vector<DeviceT>& expanded_devices,
    const std::vector<BridgePortDefinition>& bridge_ports,
    std::vector<std::string>& out_all_ports,
    std::unordered_map<std::string, uint32_t>& out_port_to_idx
) {
    for (const auto& dev : expanded_devices) {
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

template <typename DeviceT>
void apply_signal_allocation_rules(
    UnionFind& uf,
    const std::vector<DeviceT>& expanded_devices,
    const std::vector<BridgePortDefinition>& bridge_ports,
    const std::vector<Connection>& expanded_connections,
    const std::unordered_map<std::string, uint32_t>& port_to_idx
) {
    signal_union_rules::apply_structural_bridge_unions(uf, bridge_ports, port_to_idx);
    signal_union_rules::apply_signal_union_rules(
        uf,
        expanded_devices,
        expanded_connections,
        port_to_idx,
        [](const std::string&, const std::string&, bool, bool) {});
}

std::unordered_map<std::string, uint32_t> finalize_signal_indices(
    const UnionFind& uf,
    const std::vector<std::string>& all_ports,
    const std::unordered_map<std::string, uint32_t>& port_to_idx,
    uint32_t& out_signal_count
);

} // namespace codegen_composite_detail
