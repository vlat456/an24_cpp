#pragma once

#include <string>
#include <optional>

#include "core/domain_types.h"
#include "blueprint_v2/interface/direction.h"

struct Port {
    bp2::Direction direction = bp2::Direction::Output;
    PortType type = PortType::Any;
    Domain domain = Domain::Electrical;
    bool source_writer = false;
    std::optional<std::string> alias;

    Port() = default;
    Port(bp2::Direction direction_)
        : direction(direction_), type(PortType::Any), domain(Domain::Electrical), source_writer(false), alias(std::nullopt) {}
    Port(bp2::Direction direction_, PortType type_, std::optional<std::string> alias_ = std::nullopt)
        : direction(direction_), type(type_), domain(domain_for_port_type(type_)), source_writer(false), alias(std::move(alias_)) {}
    Port(bp2::Direction direction_, PortType type_, Domain domain_, bool source_writer_, std::optional<std::string> alias_ = std::nullopt)
        : direction(direction_), type(type_), domain(domain_), source_writer(source_writer_), alias(std::move(alias_)) {}
};
