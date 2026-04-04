#include "jit_solver_internal.h"
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

    struct UnionFind {
        std::vector<uint32_t> parent;
        std::vector<uint32_t> rank;

        explicit UnionFind(size_t n) : parent(n), rank(n, 0) {
            for (uint32_t i = 0; i < n; ++i) {
                parent[i] = i;
            }
        }

        uint32_t find(uint32_t x) {
            if (parent[x] != x) {
                parent[x] = find(parent[x]);
            }
            return parent[x];
        }

        void unite(uint32_t a, uint32_t b) {
            const uint32_t ra = find(a);
            const uint32_t rb = find(b);
            if (ra == rb) {
                return;
            }

            if (rank[ra] < rank[rb]) {
                parent[ra] = rb;
            } else if (rank[ra] > rank[rb]) {
                parent[rb] = ra;
            } else {
                parent[rb] = ra;
                rank[ra]++;
            }
        }
    };

    UnionFind uf(all_ports.size());

    // === PARITY GUARD: BlueprintInput/Output Bridge Union ===
    // INVARIANT: ext↔port union MUST be mirrored in AOT codegen.
    // - BlueprintInput/BlueprintOutput bridge nodes have two ports:
    //   .ext (external, parent-facing) and .port (internal, child-facing).
    // - These ports must be unified into a single signal to implement transparent passthrough.
    // - Parser rewrite ensures parent connections use :instance:port.ext format.
    // - JIT (this path) and AOT (codegen.cpp) must unify these identically.
    // [CRITICAL] If this logic changes, codegen.cpp bridge unification must mirror it exactly.
    for (const auto& dev : devices) {
        if (dev.visual_only) continue;
        if (dev.classname == "BlueprintInput" || dev.classname == "BlueprintOutput") {
            std::string ext_key  = dev.name + ".ext";
            std::string port_key = dev.name + ".port";
            auto it_ext  = port_to_idx.find(ext_key);
            auto it_port = port_to_idx.find(port_key);
            if (it_ext != port_to_idx.end() && it_port != port_to_idx.end()) {
                uf.unite(it_ext->second, it_port->second);
            }
        }
    }

    for (const auto& [from, to] : connections) {
        auto it_from = port_to_idx.find(from);
        auto it_to = port_to_idx.find(to);
        if (it_from != port_to_idx.end() && it_to != port_to_idx.end()) {
            uf.unite(it_from->second, it_to->second);
        } else {
            if (it_from == port_to_idx.end()) {
                spdlog::warn("[build] Connection references non-existent port '{}' (connected to '{}')", from, to);
            }
            if (it_to == port_to_idx.end()) {
                spdlog::warn("[build] Connection references non-existent port '{}' (connected from '{}')", to, from);
            }
        }
    }

    // === PARITY GUARD: General Alias Port Union ===
    // INVARIANT: must match AOT codegen (codegen.cpp) alias union step.
    // If a port has an alias field, unify both ports so they share a signal.
    for (const auto& dev : devices) {
        if (dev.visual_only) continue;
        for (const auto& [port_name, port] : dev.ports) {
            if (port.alias.has_value() && !port.alias->empty()) {
                std::string full_port  = dev.name + "." + port_name;
                std::string full_alias = dev.name + "." + *port.alias;
                auto it_port  = port_to_idx.find(full_port);
                auto it_alias = port_to_idx.find(full_alias);
                if (it_port != port_to_idx.end() && it_alias != port_to_idx.end()) {
                    uf.unite(it_port->second, it_alias->second);
                }
            }
        }
    }

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
