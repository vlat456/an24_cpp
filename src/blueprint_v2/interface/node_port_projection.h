#pragma once

#include "blueprint_v2/blueprint/node_port.h"
#include "blueprint_v2/interface/interface.h"

#include <vector>

namespace bp2 {

std::vector<NodePort> derive_input_ports(const Interface& iface);
std::vector<NodePort> derive_output_ports(const Interface& iface);

}
