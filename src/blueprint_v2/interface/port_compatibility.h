#pragma once

#include "port_descriptor.h"

#include <optional>

namespace bp2 {

enum class PortDomainResolutionKind : uint8_t {
    ExactMatch,
    SourceAnyAdoptsTarget,
    TargetAnyAdoptsSource,
    BothAnyAmbiguous,
    Mismatch,
};

struct PortDomainResolution {
    std::optional<Domain> domain;
    PortDomainResolutionKind kind = PortDomainResolutionKind::Mismatch;

    bool compatible() const { return domain.has_value(); }
    bool ambiguous() const { return kind == PortDomainResolutionKind::BothAnyAmbiguous; }
};

/// Shared authority for current PortType::Any domain reconciliation.
///
/// This centralizes the current wildcard/domain-resolution behavior used by
/// validator, editor wire creation, codec decode, and type-definition import.
/// It does NOT yet model future contextual port typing semantics.
PortDomainResolution resolve_port_domain(const PortDescriptor& source,
                                         const PortDescriptor& target);

bool port_domains_compatible(const PortDescriptor& source,
                             const PortDescriptor& target);

} // namespace bp2
