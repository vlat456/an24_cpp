#pragma once

#include "blueprint_v2/blueprint/blueprint.h"

namespace bp2 {

Blueprint clone_metadata(const Blueprint& bp);
Blueprint::Node canonicalize_composite_host_iface(const Blueprint& bp, Blueprint::Node node);
Blueprint canonicalize_composite_host_ifaces(Blueprint bp);

} // namespace bp2
