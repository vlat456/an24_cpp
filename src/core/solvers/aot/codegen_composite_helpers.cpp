#include "codegen_composite_helpers.h"

#include <map>

namespace codegen_composite_detail {

UnionFind::UnionFind(size_t size) : parent(size), rank(size, 0) {
    for (uint32_t i = 0; i < static_cast<uint32_t>(size); ++i) {
        parent[i] = i;
    }
}

uint32_t UnionFind::find(uint32_t x) const {
    while (parent[x] != x) {
        parent[x] = parent[parent[x]];
        x = parent[x];
    }
    return x;
}

void UnionFind::unite(uint32_t a, uint32_t b) {
    uint32_t ra = find(a);
    uint32_t rb = find(b);
    if (ra == rb) {
        return;
    }
    if (rank[ra] < rank[rb]) {
        std::swap(ra, rb);
    }
    parent[rb] = ra;
    if (rank[ra] == rank[rb]) {
        rank[ra]++;
    }
}

void build_port_index_map(
    const std::vector<DeviceInstance>& expanded_devices,
    std::vector<std::string>& out_all_ports,
    std::unordered_map<std::string, uint32_t>& out_port_to_idx
) {
    for (const auto& dev : expanded_devices) {
        for (const auto& [port_name, port] : dev.ports) {
            (void)port;
            std::string full_port = dev.name + "." + port_name;
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
    for (const auto& dev : expanded_devices) {
        if (dev.classname == "BlueprintInput" || dev.classname == "BlueprintOutput") {
            std::string ext_key = dev.name + ".ext";
            std::string port_key = dev.name + ".port";
            auto it_ext = port_to_idx.find(ext_key);
            auto it_port = port_to_idx.find(port_key);
            if (it_ext != port_to_idx.end() && it_port != port_to_idx.end()) {
                uf.unite(it_ext->second, it_port->second);
            }
        }
    }

    for (const auto& conn : expanded_connections) {
        auto it_from = port_to_idx.find(conn.from);
        auto it_to = port_to_idx.find(conn.to);
        if (it_from != port_to_idx.end() && it_to != port_to_idx.end()) {
            uf.unite(it_from->second, it_to->second);
        }
    }

    for (const auto& dev : expanded_devices) {
        for (const auto& [port_name, port] : dev.ports) {
            if (port.alias.has_value() && !port.alias->empty()) {
                std::string full_port = dev.name + "." + port_name;
                std::string full_alias = dev.name + "." + *port.alias;
                auto it_port = port_to_idx.find(full_port);
                auto it_alias = port_to_idx.find(full_alias);
                if (it_port != port_to_idx.end() && it_alias != port_to_idx.end()) {
                    uf.unite(it_port->second, it_alias->second);
                }
            }
        }
    }
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
    out_signal_count = next_signal;

    return port_to_signal;
}

} // namespace codegen_composite_detail
