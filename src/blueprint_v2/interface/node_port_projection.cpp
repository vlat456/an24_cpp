#include "node_port_projection.h"

namespace bp2 {

std::vector<PortDescriptor> derive_input_ports(const Interface& iface) {
    std::vector<PortDescriptor> ports;
    ports.reserve(iface.size());
    for (const auto& pd : iface.ports()) {
        if (pd.direction == Direction::Input || pd.direction == Direction::InOut) {
            ports.push_back(pd);
        }
    }
    return ports;
}

std::vector<PortDescriptor> derive_output_ports(const Interface& iface) {
    std::vector<PortDescriptor> ports;
    ports.reserve(iface.size());
    for (const auto& pd : iface.ports()) {
        if (pd.direction == Direction::Output || pd.direction == Direction::InOut) {
            ports.push_back(pd);
        }
    }
    return ports;
}

} // namespace bp2