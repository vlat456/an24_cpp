#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/registry/type_registry.h"
#include "ui/core/interned_id.h"
#include "blueprint_v2/path/path.h"
#include <string>
#include <optional>

namespace bp2 {

struct DecodeError {
    std::string message;
    int line = -1;
};

class BlueprintCodec {
public:
    static std::string encode(Blueprint const& bp,
                              ui::StringInterner const& interner,
                              PathArena const& arena,
                              TypeRegistry const* registry = nullptr);

    static std::optional<Blueprint> decode(
        std::string_view json,
        ui::StringInterner& interner,
        PathArena& arena,
        TypeRegistry const& registry,
        DecodeError* error_out = nullptr);
};

} // namespace bp2
