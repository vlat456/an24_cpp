#pragma once

#include "ui/core/interned_id.h"
#include "blueprint_v2/path/path.h"
#include "blueprint_v2/interface/port_descriptor.h"
#include "json_parser/json_parser.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace bp2 {

using SignalIndex = uint32_t;

struct FlatNetlist {
    struct Component {
        Path path;
        ui::InternedId type;
        std::unordered_map<ui::InternedId, float> params;
        std::unordered_map<std::string, std::string> string_params;
        std::vector<PortDescriptor> ports;
        std::vector<std::pair<ui::InternedId, SignalIndex>> port_signals;
    };

    struct Signal {
        SignalIndex index;
        Domain domain;
        std::vector<Path> connected_ports;
    };

    std::vector<Component> components;
    std::vector<Signal> signals;
    uint32_t signal_count = 0;
};

} // namespace bp2
