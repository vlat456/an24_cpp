#include "wire_validator.h"

#include "blueprint_v2/interface/port_compatibility.h"
#include "core/domain_string.h"

namespace bp2 {

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
    auto resolved = resolve_port_domain(src->port, tgt->port);
    if (!resolved.compatible()) {
        out.error = "wire endpoint domain mismatch";
        return out;
    }
    out.resolved_domain = *resolved.domain;

    if (wire.domain != *resolved.domain) {
        out.error = "wire domain mismatch: declared as " + domain_to_string(wire.domain)
                  + " but endpoints are " + domain_to_string(*resolved.domain);
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
