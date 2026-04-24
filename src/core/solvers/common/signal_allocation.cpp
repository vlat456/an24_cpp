#include "signal_allocation.h"

#include <algorithm>
#include <map>

namespace signal_alloc {

std::unordered_map<std::string, uint32_t> finalize_signal_indices(
    const UnionFind& uf,
    const std::vector<std::string>& all_ports,
    const std::unordered_map<std::string, uint32_t>& port_to_idx,
    uint32_t& out_signal_count
) {
    // Map each port to its UnionFind root
    std::unordered_map<std::string, uint32_t> port_to_signal;
    for (const auto& port : all_ports) {
        port_to_signal[port] = uf.find(port_to_idx.at(port));
    }

    // Collect unique roots in sorted order for deterministic signal assignment
    std::vector<uint32_t> unique_roots;
    unique_roots.reserve(port_to_signal.size());
    for (const auto& [port, root] : port_to_signal) {
        (void)port;
        unique_roots.push_back(root);
    }
    std::sort(unique_roots.begin(), unique_roots.end());
    unique_roots.erase(std::unique(unique_roots.begin(), unique_roots.end()), unique_roots.end());

    // Assign compact signal indices [0, N)
    std::map<uint32_t, uint32_t> root_to_signal;
    uint32_t next_signal = 0;
    for (uint32_t root : unique_roots) {
        root_to_signal[root] = next_signal++;
    }

    // Remap port→root to port→signal
    for (auto& [port, sig] : port_to_signal) {
        (void)port;
        sig = root_to_signal[sig];
    }

    // +1 sentinel for unmapped/fallback bindings (aligned with JIT semantics)
    out_signal_count = next_signal + 1;

    return port_to_signal;
}

} // namespace signal_alloc
