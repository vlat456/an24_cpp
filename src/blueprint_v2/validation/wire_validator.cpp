#include "wire_validator.h"

#include "core/domain_string.h"

namespace bp2 {

/// When either endpoint has PortType::Any, domain comparison is bypassed
/// because Any is a wildcard that can connect across domains.
/// Returns the concrete domain when one side is Any, or the common domain
/// when both sides agree, or std::nullopt on mismatch.
static std::optional<Domain> reconcile_endpoint_domains(
        PortDescriptor const& src, PortDescriptor const& tgt) {
    const bool src_any = (src.port_type == PortType::Any);
    const bool tgt_any = (tgt.port_type == PortType::Any);

    if (src_any && tgt_any) {
        // Both wildcard — prefer the non-default domain if they differ,
        // otherwise just pick source. This is a best-effort heuristic;
        // the concrete domain is unknowable when both sides are Any.
        return src.domain;
    }
    if (src_any) return tgt.domain;   // concrete side wins
    if (tgt_any) return src.domain;   // concrete side wins

    // Neither side is Any — strict equality required.
    if (src.domain != tgt.domain) return std::nullopt;
    return src.domain;
}

WireValidator::Result WireValidator::validate(Blueprint::Wire const& wire,
                                              Blueprint const& bp,
                                              const ::TypeRegistry& parser_registry,
                                              ui::StringInterner& interner) {
    Result out;
    out.valid = false;
    out.resolved_domain = wire.domain;

    if (wire.source == wire.target) {
        out.error = "wire is self-loop";
        return out;
    }

    PathResolver resolver;
    auto src = resolver.resolve(wire.source, bp, parser_registry, interner);
    auto tgt = resolver.resolve(wire.target, bp, parser_registry, interner);
    if (!src || !tgt) {
        out.error = "wire endpoint path unresolved";
        return out;
    }

    // Domain compatibility: PortType::Any is a wildcard that crosses domains.
    auto resolved = reconcile_endpoint_domains(src->port, tgt->port);
    if (!resolved.has_value()) {
        out.error = "wire endpoint domain mismatch";
        return out;
    }
    out.resolved_domain = *resolved;

    if (wire.domain != *resolved) {
        out.error = "wire domain mismatch: declared as " + domain_to_string(wire.domain)
                  + " but endpoints are " + domain_to_string(*resolved);
        return out;
    }

    if (!resolver.can_connect(wire.source, wire.target, bp, parser_registry, interner)) {
        out.error = "wire direction/domain invalid";
        return out;
    }

    out.valid = true;
    return out;
}

} // namespace bp2
