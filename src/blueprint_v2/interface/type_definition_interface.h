#pragma once

#include "blueprint_v2/interface/interface.h"
#include "blueprint_v2/interface/port_descriptor.h"
#include "json_parser/json_parser.h"
#include "ui/core/interned_id.h"

#include <vector>

namespace bp2 {

inline PortDescriptor port_descriptor_from_type_port(ui::InternedId name, const Port& port) {
    PortDescriptor pd;
    pd.name = name;
    pd.domain = port.domain;
    pd.direction = port.direction;
    pd.port_type = port.type;
    pd.source_writer = port.source_writer;
    return pd;
}

inline Interface interface_from_type_definition(const ComponentSpec& spec,
                                                ui::StringInterner& interner) {
    const auto& ports = spec_ports(spec);
    std::vector<PortDescriptor> iface_ports;
    iface_ports.reserve(ports.size());
    for (const auto& [name, port] : ports) {
        auto pd = port_descriptor_from_type_port(interner.intern(name), port);
        if (port.alias.has_value() && !port.alias->empty()) {
            pd.alias = interner.intern(*port.alias);
        }
        iface_ports.push_back(std::move(pd));
    }
    return Interface(std::move(iface_ports));
}

} // namespace bp2
