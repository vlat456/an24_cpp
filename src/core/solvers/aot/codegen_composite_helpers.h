#pragma once

#include "codegen_internal.h"
#include "../../utils/union_find.h"

namespace codegen_composite_detail {

using UnionFind = core::utils::UnionFind;

void build_port_index_map(
    const std::vector<DeviceInstance>& expanded_devices,
    std::vector<std::string>& out_all_ports,
    std::unordered_map<std::string, uint32_t>& out_port_to_idx
);

void apply_signal_allocation_rules(
    UnionFind& uf,
    const std::vector<DeviceInstance>& expanded_devices,
    const std::vector<Connection>& expanded_connections,
    const std::unordered_map<std::string, uint32_t>& port_to_idx
);

std::unordered_map<std::string, uint32_t> finalize_signal_indices(
    const UnionFind& uf,
    const std::vector<std::string>& all_ports,
    const std::unordered_map<std::string, uint32_t>& port_to_idx,
    uint32_t& out_signal_count
);

} // namespace codegen_composite_detail
