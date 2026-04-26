#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "core/strings/interned_id.h"
#include "blueprint_v2/path/path.h"
#include <string>
#include <optional>

struct ComponentRegistry;

namespace bp2 {

struct DecodeError {
    std::string message;
    int line = -1;
};

class BlueprintCodec {
public:
    static std::string encode(Blueprint const& bp,
                              core::StringInterner const& interner,
                              PathArena const& arena,
                              const ::ComponentRegistry* parser_registry = nullptr);

    static std::optional<Blueprint> decode(
        std::string_view json,
        core::StringInterner& interner,
        PathArena& arena,
        const ::ComponentRegistry& parser_registry,
        DecodeError* error_out = nullptr);
};

} // namespace bp2
