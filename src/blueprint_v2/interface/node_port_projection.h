#pragma once

#include "blueprint_v2/interface/interface.h"
#include <vector>

namespace bp2 {

std::vector<PortDescriptor> derive_input_ports(const Interface& iface);
std::vector<PortDescriptor> derive_output_ports(const Interface& iface);

}