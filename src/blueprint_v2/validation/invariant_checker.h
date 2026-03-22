#pragma once

#include "blueprint_v2/validation/wire_validator.h"

namespace bp2 {

class InvariantChecker {
public:
    struct Result {
        bool valid = false;
        std::string error;
    };

    static Result validate(Blueprint const& bp,
                           PathArena const& arena,
                           TypeRegistry const& registry);
};

} // namespace bp2
