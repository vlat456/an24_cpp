#pragma once

#include "blueprint_v2/validation/wire_validator.h"

struct TypeRegistry;

namespace bp2 {

class InvariantChecker {
public:
    struct Result {
        bool valid = false;
        std::string error;
    };

    static Result validate(Blueprint const& bp,
                           PathArena const& arena,
                           const ::TypeRegistry& parser_registry,
                           ui::StringInterner& interner);
};

} // namespace bp2
