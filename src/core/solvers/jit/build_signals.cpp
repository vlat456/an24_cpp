#include "jit_solver_internal.h"
#include "../common/signal_union_rules.h"
#include "../../utils/union_find.h"
#include <algorithm>
#include <map>
#include <spdlog/spdlog.h>

namespace jit_solver_impl {

void process_port_unions(
    BuildResult& result,
    const std::vector<DeviceInstance>& devices,
    const std::vector<std::pair<std::string, std::string>>& connections)
{
    std::vector<std::string> all_ports;
    std::unordered_map<std::string, uint32_t> port_to_idx;

    for (const auto& dev : devices) {
        if (dev.visual_only) {
            continue;
        }

        for (const auto& [port_name, port] : dev.ports) {
            (void)port;
            const std::string full_port = dev.name + "." + port_name;
            const uint32_t idx = static_cast<uint32_t>(all_ports.size());
            all_ports.push_back(full_port);
            port_to_idx[full_port] = idx;
        }
    }

    if (all_ports.empty()) {
        result.signal_count = 1; // sentinel
        return;
    }

    core::utils::UnionFind uf(all_ports.size());

    // === PARITY GUARD: bridge/connection/alias unions ===
    // INVARIANT: this must stay behaviorally aligned with the AOT path.
    signal_union_rules::apply_signal_union_rules(
        uf,
        devices,
        connections,
        port_to_idx,
        /*skip_visual_only=*/true,
        [](const std::string& from, const std::string& to, bool missing_from, bool missing_to) {
            if (missing_from) {
                spdlog::warn("[build] Connection references non-existent port '{}' (connected to '{}')", from, to);
            }
            if (missing_to) {
                spdlog::warn("[build] Connection references non-existent port '{}' (connected from '{}')", to, from);
            }
        });

    std::map<uint32_t, uint32_t> root_to_signal;
    uint32_t next_signal = 0;
    for (const auto& [port, idx] : port_to_idx) {
        const uint32_t root = uf.find(idx);
        auto [it, inserted] = root_to_signal.emplace(root, next_signal);
        if (inserted) {
            next_signal++;
        }
        result.port_to_signal[port] = it->second;
    }

    result.signal_count = next_signal + 1; // sentinel at end
}

}  // namespace
