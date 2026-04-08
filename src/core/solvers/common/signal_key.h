#pragma once

#include <string>
#include <string_view>
#include <cstddef>

namespace signal_key {

inline std::string make_node_port_key(std::string_view node_id, std::string_view port_name) {
    std::string key;
    key.reserve(node_id.size() + 1 + port_name.size());
    key.append(node_id);
    key.push_back('.');
    key.append(port_name);
    return key;
}

inline std::string make_child_scope_key(std::string_view parent_instance_id, std::string_view child_key) {
    std::string key;
    key.reserve(parent_instance_id.size() + 1 + child_key.size());
    key.append(parent_instance_id);
    key.push_back(':');
    key.append(child_key);
    return key;
}

inline std::string make_bridge_internal_key(std::string_view node_id) {
    std::string key;
    key.reserve(node_id.size() + 5);
    key.append(node_id);
    key.append(".port");
    return key;
}

inline std::string make_bridge_external_key(std::string_view node_id) {
    std::string key;
    key.reserve(node_id.size() + 4);
    key.append(node_id);
    key.append(".ext");
    return key;
}

inline std::string make_exposed_node_port_from_bridge_node(std::string_view bridge_node_id) {
    const size_t sep = bridge_node_id.rfind(':');
    if (sep == std::string_view::npos || sep == 0 || (sep + 1) >= bridge_node_id.size()) {
        return "";
    }
    const std::string_view instance_id = bridge_node_id.substr(0, sep);
    const std::string_view port_name = bridge_node_id.substr(sep + 1);
    return make_node_port_key(instance_id, port_name);
}

} // namespace signal_key
