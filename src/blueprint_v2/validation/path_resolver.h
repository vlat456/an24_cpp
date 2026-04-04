#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/registry/type_registry.h"

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
                                        TypeRegistry const& registry) const;

    bool can_connect(Path const& source,
                     Path const& target,
                     Blueprint const& root,
                     PathArena const& arena,
                     TypeRegistry const& registry) const;

private:
    struct NodeContext {
        Blueprint const* blueprint = nullptr;
        Blueprint::Node const* node = nullptr;
        Path blueprint_path;
    };

    struct NestedContext {
        Blueprint const* parent_blueprint = nullptr;
        Blueprint::Nested const* nested = nullptr;
        Path nested_path;
    };

    std::optional<NodeContext> resolve_node_context(Path node_path,
                                                    Blueprint const& root,
                                                    PathArena const& arena,
                                                    TypeRegistry const& registry) const;

    std::optional<NestedContext> resolve_nested_context(Path nested_path,
                                                        Blueprint const& root,
                                                        PathArena const& arena,
                                                        TypeRegistry const& registry) const;

    Blueprint const* descend_nested(Blueprint::Nested const& nested,
                                    TypeRegistry const& registry) const;

    Interface const* node_interface(Blueprint::Node const& node,
                                    TypeRegistry const& registry) const;

    bool direction_compatible(Direction source, Direction target) const;
};

} // namespace bp2
