#include "codegen_composite_helpers.h"
#include "../common/signal_union_rules.h"

#include <map>

namespace codegen_composite_detail {

void build_port_index_map(
    const std::vector<DeviceInstance>& expanded_devices,
    std::vector<std::string>& out_all_ports,
    std::unordered_map<std::string, uint32_t>& out_port_to_idx
) {
    for (const auto& dev : expanded_devices) {
        if (dev.visual_only) {
            continue;
        }
        for (const auto& [port_name, port] : dev.ports) {
            (void)port;
            std::string full_port = signal_key::make_node_port_key(dev.name, port_name);
            uint32_t idx = static_cast<uint32_t>(out_all_ports.size());
            out_all_ports.push_back(full_port);
            out_port_to_idx[full_port] = idx;
        }
    }
}

void apply_signal_allocation_rules(
    UnionFind& uf,
    const std::vector<DeviceInstance>& expanded_devices,
    const std::vector<Connection>& expanded_connections,
    const std::unordered_map<std::string, uint32_t>& port_to_idx
) {
    signal_union_rules::apply_signal_union_rules(
        uf,
        expanded_devices,
        expanded_connections,
        port_to_idx,
        /*skip_visual_only=*/true,
        [](const std::string&, const std::string&, bool, bool) {});
}

std::unordered_map<std::string, uint32_t> finalize_signal_indices(
    const UnionFind& uf,
    const std::vector<std::string>& all_ports,
    const std::unordered_map<std::string, uint32_t>& port_to_idx,
    uint32_t& out_signal_count
) {
    std::unordered_map<std::string, uint32_t> port_to_signal;
    for (const auto& port : all_ports) {
        port_to_signal[port] = uf.find(port_to_idx.at(port));
    }

    std::map<uint32_t, uint32_t> root_to_signal;
    std::vector<uint32_t> unique_roots;
    for (const auto& [port, root] : port_to_signal) {
        (void)port;
        unique_roots.push_back(root);
    }
    std::sort(unique_roots.begin(), unique_roots.end());
    unique_roots.erase(std::unique(unique_roots.begin(), unique_roots.end()), unique_roots.end());

    uint32_t next_signal = 0;
    for (uint32_t root : unique_roots) {
        root_to_signal[root] = next_signal++;
    }
    for (auto& [port, sig] : port_to_signal) {
        (void)port;
        sig = root_to_signal[sig];
    }
    // Keep AOT signal count semantics aligned with JIT:
    // reserve one trailing sentinel index used for unmapped/fallback bindings.
    out_signal_count = next_signal + 1;

    return port_to_signal;
}

} // namespace codegen_composite_detail
