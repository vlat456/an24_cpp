#pragma once

// Thin namespace alias — all canonical mappings live in domain_types.h.
// This header exists solely to avoid mass-renaming editor call sites.
#include "core/domain_types.h"

namespace editor::common {

inline Domain domain_for_port_type(PortType t) {
    return ::domain_for_port_type(t);
}

inline PortType port_type_for_domain(Domain d) {
    return ::port_type_for_domain(d);
}

} // namespace editor::common
