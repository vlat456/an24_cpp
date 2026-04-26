#pragma once

#include "blueprint_v2/interface/interface.h"

namespace bp2 {

inline Interface interface_from_bridge_port(bp2::BridgeDirection direction,
                                            PortType port_type,
                                            core::StringInterner& interner) {
    const Domain domain = domain_for_port_type(port_type);
    if (direction == bp2::BridgeDirection::Input) {
        return Interface({
            {interner.intern("ext"), domain, Direction::Input, port_type},
            {interner.intern("port"), domain, Direction::Output, port_type},
        });
    }

    return Interface({
        {interner.intern("port"), domain, Direction::Input, port_type},
        {interner.intern("ext"), domain, Direction::Output, port_type},
    });
}

} // namespace bp2
