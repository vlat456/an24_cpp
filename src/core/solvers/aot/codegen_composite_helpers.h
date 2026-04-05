#pragma once

#include "codegen_internal.h"

namespace codegen_composite_detail {

struct UnionFind {
    mutable std::vector<uint32_t> parent;
    std::vector<uint32_t> rank;

    explicit UnionFind(size_t size);

    uint32_t find(uint32_t x) const;
    void unite(uint32_t a, uint32_t b);
};

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
