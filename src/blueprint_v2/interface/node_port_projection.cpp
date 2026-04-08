#include "node_port_projection.h"

namespace bp2 {

std::vector<NodePort> derive_input_ports(const Interface& iface) {
    std::vector<NodePort> ports;
    ports.reserve(iface.size());
    for (const auto& pd : iface.ports()) {
        if (pd.direction == Direction::Input) {
            ports.emplace_back(pd.name, PortSide::Input, pd.port_type);
        } else if (pd.direction == Direction::InOut) {
            ports.emplace_back(pd.name, PortSide::InOut, pd.port_type);
        }
    }
    return ports;
}

std::vector<NodePort> derive_output_ports(const Interface& iface) {
    std::vector<NodePort> ports;
    ports.reserve(iface.size());
    for (const auto& pd : iface.ports()) {
        if (pd.direction == Direction::Output) {
            ports.emplace_back(pd.name, PortSide::Output, pd.port_type);
        } else if (pd.direction == Direction::InOut) {
            ports.emplace_back(pd.name, PortSide::InOut, pd.port_type);
        }
    }
    return ports;
}

} // namespace bp2
