#pragma once

#include "blueprint_v2/validation/path_resolver.h"

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
                           PathArena const& arena,
                           TypeRegistry const& registry);
};

} // namespace bp2
