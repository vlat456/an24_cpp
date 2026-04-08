#pragma once

#include "ui/core/interned_id.h"
#include "json_parser/json_parser.h"
#include <cstdint>

namespace bp2 {

enum class Direction : uint8_t {
    Input,
    Output,
    InOut
};

struct PortDescriptor {
    ui::InternedId name;
    Domain domain;
    bp2::Direction direction;
    PortType port_type = PortType::Any;

    bool operator==(PortDescriptor const& o) const {
        return name == o.name && domain == o.domain
            && direction == o.direction && port_type == o.port_type;
    }
    bool operator!=(PortDescriptor const& o) const { return !(*this == o); }
};

} // namespace bp2
