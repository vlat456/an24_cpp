#include "codegen_composite_helpers.h"

#include <map>

namespace codegen_composite_detail {

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
