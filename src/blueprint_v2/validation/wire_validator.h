#pragma once

#include "blueprint_v2/validation/path_resolver.h"

struct TypeRegistry;

namespace bp2 {

class WireValidator {
public:
    struct Result {
        bool valid = false;
        std::string error;
        Domain resolved_domain = Domain::Electrical;
    };

    static Result validate(Blueprint::Wire const& wire,
                           Blueprint const& bp,
                           const ::TypeRegistry& parser_registry,
                           ui::StringInterner& interner);
};

} // namespace bp2
