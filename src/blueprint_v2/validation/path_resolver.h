#pragma once

#include "blueprint_v2/blueprint/blueprint.h"

struct ComponentRegistry;

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
                                        const ::ComponentRegistry& parser_registry,
                                        core::StringInterner& interner) const;

    std::optional<ResolvedPort> resolve(WireEndpoint const& ep,
                                        Blueprint const& root,
                                        const ::ComponentRegistry& parser_registry,
                                        core::StringInterner& interner) const;

    bool can_connect(Path const& source,
                     Path const& target,
                     Blueprint const& root,
                     PathArena const& arena,
                     const ::ComponentRegistry& parser_registry,
                     core::StringInterner& interner) const;

    bool can_connect(WireEndpoint const& source,
                     WireEndpoint const& target,
                     Blueprint const& root,
                     const ::ComponentRegistry& parser_registry,
                     core::StringInterner& interner) const;

private:
    bool direction_compatible(Direction source, Direction target) const;
};

} // namespace bp2
