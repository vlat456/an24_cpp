#pragma once

#include "blueprint_v2/blueprint/blueprint.h"

struct TypeRegistry;

namespace bp2 {

struct ResolvedPort {
    PortDescriptor port;
    Path blueprint_path;
    bool is_boundary = false;
};

class PathResolver {
public:
    std::optional<ResolvedPort> resolve(Path const& path,
                                        Blueprint const& root,
                                        PathArena const& arena,
                                        const ::TypeRegistry& parser_registry,
                                        ui::StringInterner& interner) const;

    bool can_connect(Path const& source,
                     Path const& target,
                     Blueprint const& root,
                     PathArena const& arena,
                     const ::TypeRegistry& parser_registry,
                     ui::StringInterner& interner) const;

private:
    bool direction_compatible(Direction source, Direction target) const;
};

} // namespace bp2
