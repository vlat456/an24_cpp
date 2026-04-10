#include "wire_validator.h"

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

    if (src->port.domain != tgt->port.domain) {
        out.error = "wire endpoint domain mismatch";
        return out;
    }

    out.resolved_domain = src->port.domain;
    if (wire.domain != src->port.domain) {
        out.valid = true;
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
