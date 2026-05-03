#pragma once

#include "blueprint_v2/interface/interface.h"

namespace bp2 {

struct BridgePortNames {
    core::InternedId ext;
    core::InternedId port;

    BridgePortNames(core::StringInterner& interner)
        : ext(interner.intern("ext")), port(interner.intern("port")) {}
};

inline Interface interface_from_bridge_port(bp2::BridgeDirection direction,
                                            PortType port_type,
                                            core::StringInterner& interner) {
    const Domain domain = domain_for_port_type(port_type);
    const core::InternedId ext = interner.intern("ext");
    const core::InternedId port = interner.intern("port");
    if (direction == bp2::BridgeDirection::Input) {
        return Interface({
            {ext, domain, Direction::Input, port_type},
            {port, domain, Direction::Output, port_type},
        });
    }

    return Interface({
        {port, domain, Direction::Input, port_type},
        {ext, domain, Direction::Output, port_type},
    });
}

} // namespace bp2
