#include "wire_validator.h"

#include "blueprint_v2/validation/signal_typing.h"
#include "core/domain_string.h"

namespace bp2 {

WireValidator::Result WireValidator::validate(Blueprint::Wire const& wire,
                                              Blueprint const& bp,
                                              const ::ComponentRegistry& parser_registry,
                                              core::StringInterner& interner) {
    Result out;
    out.valid = false;
    out.resolved_domain = wire.domain;

    if (wire.source == wire.target) {
        out.error = "wire is self-loop";
        return out;
    }

    PathResolver const resolver;
    auto src = resolver.resolve(wire.source, bp, parser_registry, interner);
    auto tgt = resolver.resolve(wire.target, bp, parser_registry, interner);
    if (!src || !tgt) {
        out.error = "wire endpoint path unresolved";
        return out;
    }

    if (!port_types_compatible(src->port, tgt->port)) {
        out.error = "wire endpoint type mismatch";
        return out;
    }

    auto resolved = resolve_signal_typing(bp, &parser_registry, interner, wire.source, wire.target);
    if (!resolved.resolved.has_value()) {
        switch (resolved.error) {
            case SignalTypingError::ConflictingConcreteDomains:
                out.error = "wire endpoint domain mismatch";
                break;
            case SignalTypingError::ConflictingConcreteTypes:
                out.error = "wire endpoint type mismatch";
                break;
            case SignalTypingError::UnresolvedContextualSignal:
                out.error = "wire signal typing unresolved";
                break;
            default:
                out.error = "wire signal typing failed";
                break;
        }
        return out;
    }
    out.resolved_domain = resolved.resolved->domain;

    if (wire.domain != resolved.resolved->domain) {
        out.error = "wire domain mismatch: declared as " + domain_to_string(wire.domain)
                  + " but endpoints are " + domain_to_string(resolved.resolved->domain);
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
