#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/interface/port_descriptor.h"

#include <optional>
#include <string>
#include <unordered_map>

struct TypeRegistry;

namespace bp2 {

enum class SignalTypingError {
    None,
    UnknownEndpoint,
    ConflictingConcreteDomains,
    ConflictingConcreteTypes,
    UnresolvedContextualSignal,
};

struct ResolvedSignalTyping {
    Domain domain = Domain::Electrical;
    PortType port_type = PortType::Any;
};

struct SignalTypingResult {
    std::optional<ResolvedSignalTyping> resolved;
    SignalTypingError error = SignalTypingError::None;
};

SignalTypingResult resolve_signal_typing(const Blueprint& bp,
                                         const TypeRegistry* parser_registry,
                                         ui::StringInterner& interner,
                                         WireEndpoint endpoint_a,
                                         WireEndpoint endpoint_b = WireEndpoint{});

bool port_types_compatible(const PortDescriptor& source,
                           const PortDescriptor& target);

} // namespace bp2
