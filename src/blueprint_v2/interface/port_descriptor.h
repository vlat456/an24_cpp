#pragma once

#include "direction.h"
#include "core/strings/interned_id.h"
#include "core/domain_types.h"
#include <cstdint>
#include <optional>

namespace bp2 {

struct PortDescriptor {
    core::InternedId name;
    Domain domain;
    Direction direction;
    PortType port_type = PortType::Any;
    std::optional<core::InternedId> alias;
    bool source_writer = false;

    bool operator==(PortDescriptor const& o) const {
        return name == o.name && domain == o.domain
            && direction == o.direction && port_type == o.port_type
            && alias == o.alias && source_writer == o.source_writer;
    }
    bool operator!=(PortDescriptor const& o) const { return !(*this == o); }
};

} // namespace bp2
