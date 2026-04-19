#pragma once

#include "json_parser/json_parser.h"
#include "signal_key.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace signal_union_rules {

inline bool should_skip_device(const DeviceInstance& dev, bool skip_visual_only) {
    return skip_visual_only && dev.spec && spec_visual_only(*dev.spec);
}

inline std::string bridge_internal_key(const BridgePortDefinition& bridge) {
    return signal_key::make_bridge_internal_key(bridge.id);
}

inline std::string bridge_external_key(const BridgePortDefinition& bridge) {
    return signal_key::make_bridge_external_key(bridge.id);
}

inline std::string bridge_exposed_key(const BridgePortDefinition& bridge) {
    if (bridge.exposed_port.empty()) {
        return signal_key::make_exposed_node_port_from_bridge_node(bridge.id);
    }
    const size_t sep = bridge.id.rfind(':');
    if (sep == std::string::npos || sep == 0) {
        return "";
    }
    return signal_key::make_node_port_key(bridge.id.substr(0, sep), bridge.exposed_port);
}

inline const std::string& connection_from(const std::pair<std::string, std::string>& conn) {
    return conn.first;
}

inline const std::string& connection_to(const std::pair<std::string, std::string>& conn) {
    return conn.second;
}

inline const std::string& connection_from(const Connection& conn) {
    return conn.from;
}

inline const std::string& connection_to(const Connection& conn) {
    return conn.to;
}

template <typename UnionFindT>
void apply_structural_bridge_unions(
    UnionFindT& uf,
    const std::vector<BridgePortDefinition>& bridges,
    const std::unordered_map<std::string, uint32_t>& port_to_idx
) {
    for (const auto& bridge : bridges) {
        const std::string ext_key = bridge_external_key(bridge);
        const std::string port_key = bridge_internal_key(bridge);
        const std::string exposed_key = bridge_exposed_key(bridge);
        auto it_ext = port_to_idx.find(ext_key);
        auto it_port = port_to_idx.find(port_key);
        if (it_ext != port_to_idx.end() && it_port != port_to_idx.end()) {
            uf.unite(it_ext->second, it_port->second);
        }
        if (!exposed_key.empty()) {
            auto it_exposed = port_to_idx.find(exposed_key);
            if (it_exposed != port_to_idx.end() && it_ext != port_to_idx.end()) {
                uf.unite(it_exposed->second, it_ext->second);
            }
        }
    }
}
template <typename UnionFindT, typename ConnectionT, typename OnMissingFn>
void apply_connection_unions(
    UnionFindT& uf,
    const std::vector<ConnectionT>& connections,
    const std::unordered_map<std::string, uint32_t>& port_to_idx,
    OnMissingFn&& on_missing
) {
    for (const auto& conn : connections) {
        const std::string& from = connection_from(conn);
        const std::string& to = connection_to(conn);

        auto it_from = port_to_idx.find(from);
        auto it_to = port_to_idx.find(to);
        if (it_from != port_to_idx.end() && it_to != port_to_idx.end()) {
            uf.unite(it_from->second, it_to->second);
            continue;
        }

        on_missing(from, to, it_from == port_to_idx.end(), it_to == port_to_idx.end());
    }
}

template <typename UnionFindT>
void apply_alias_unions(
    UnionFindT& uf,
    const std::vector<DeviceInstance>& devices,
    const std::unordered_map<std::string, uint32_t>& port_to_idx,
    bool skip_visual_only
) {
    for (const auto& dev : devices) {
        if (should_skip_device(dev, skip_visual_only)) {
            continue;
        }

        for (const auto& [port_name, port] : dev.ports) {
             if (!port.alias.has_value() || port.alias->empty()) {
                 continue;
             }

             const std::string full_port = signal_key::make_node_port_key(dev.name, port_name);
             const std::string full_alias = signal_key::make_node_port_key(dev.name, *port.alias);
             auto it_port = port_to_idx.find(full_port);
             auto it_alias = port_to_idx.find(full_alias);
             if (it_port != port_to_idx.end() && it_alias != port_to_idx.end()) {
                 uf.unite(it_port->second, it_alias->second);
             }
         }
    }
}

template <typename UnionFindT, typename ConnectionT, typename OnMissingFn>
void apply_signal_union_rules(
    UnionFindT& uf,
    const std::vector<DeviceInstance>& devices,
    const std::vector<ConnectionT>& connections,
    const std::unordered_map<std::string, uint32_t>& port_to_idx,
    bool skip_visual_only,
    OnMissingFn&& on_missing
) {
    apply_connection_unions(uf, connections, port_to_idx, std::forward<OnMissingFn>(on_missing));
    apply_alias_unions(uf, devices, port_to_idx, skip_visual_only);
}

} // namespace signal_union_rules
