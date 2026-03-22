#include "path_resolver.h"

#include <algorithm>

namespace bp2 {

Blueprint const* PathResolver::descend_nested(Blueprint::Nested const& nested,
                                              TypeRegistry const& registry) const {
    if (nested.embedded && nested.inline_def) {
        return nested.inline_def.get();
    }
    if (!nested.embedded) {
        auto* entry = registry.find(nested.blueprint_id);
        if (entry && entry->blueprint) {
            return entry->blueprint;
        }
    }
    return nullptr;
}

Interface const* PathResolver::node_interface(Blueprint::Node const& node,
                                              TypeRegistry const& registry) const {
    if (!node.iface.empty()) {
        return &node.iface;
    }
    auto* entry = registry.find(node.type);
    if (!entry) {
        return nullptr;
    }
    return &entry->iface;
}

std::optional<PathResolver::NestedContext> PathResolver::resolve_nested_context(
    Path nested_path,
    Blueprint const& root,
    PathArena const& arena,
    TypeRegistry const& registry) const {
    if (nested_path.kind() != PathKind::Nested) {
        return std::nullopt;
    }

    std::vector<Path> chain;
    Path cur = nested_path;
    while (cur.kind() != PathKind::Root) {
        chain.push_back(cur);
        cur = arena.parent(cur);
    }
    std::reverse(chain.begin(), chain.end());

    Blueprint const* current_bp = &root;
    Path current_bp_path = arena.root();
    Blueprint const* parent_bp = nullptr;
    Blueprint::Nested const* current_nested = nullptr;

    for (Path seg : chain) {
        if (seg.kind() != PathKind::Nested) {
            return std::nullopt;
        }
        parent_bp = current_bp;
        current_nested = current_bp->find_nested(seg.segment());
        if (!current_nested) {
            return std::nullopt;
        }
        current_bp_path = seg;
        current_bp = descend_nested(*current_nested, registry);
        if (!current_bp) {
            return std::nullopt;
        }
    }

    NestedContext ctx;
    ctx.parent_blueprint = parent_bp;
    ctx.nested = current_nested;
    ctx.nested_path = nested_path;
    return ctx;
}

std::optional<PathResolver::NodeContext> PathResolver::resolve_node_context(
    Path node_path,
    Blueprint const& root,
    PathArena const& arena,
    TypeRegistry const& registry) const {
    if (node_path.kind() != PathKind::Node) {
        return std::nullopt;
    }

    std::vector<Path> chain;
    Path cur = node_path;
    while (cur.kind() != PathKind::Root) {
        chain.push_back(cur);
        cur = arena.parent(cur);
    }
    std::reverse(chain.begin(), chain.end());

    Blueprint const* current_bp = &root;
    Path current_bp_path = arena.root();

    for (size_t i = 0; i < chain.size(); ++i) {
        Path seg = chain[i];
        bool is_last = (i + 1 == chain.size());
        if (seg.kind() == PathKind::Nested) {
            auto const* nested = current_bp->find_nested(seg.segment());
            if (!nested) {
                return std::nullopt;
            }
            current_bp = descend_nested(*nested, registry);
            if (!current_bp) {
                return std::nullopt;
            }
            current_bp_path = seg;
            continue;
        }
        if (seg.kind() == PathKind::Node && is_last) {
            auto const* node = current_bp->find_node(seg.segment());
            if (!node) {
                return std::nullopt;
            }
            NodeContext ctx;
            ctx.blueprint = current_bp;
            ctx.node = node;
            ctx.blueprint_path = current_bp_path;
            return ctx;
        }
        return std::nullopt;
    }

    return std::nullopt;
}

std::optional<ResolvedPort> PathResolver::resolve(Path const& path,
                                                  Blueprint const& root,
                                                  PathArena const& arena,
                                                  TypeRegistry const& registry) const {
    if (path.kind() != PathKind::Port) {
        return std::nullopt;
    }

    Path parent = arena.parent(path);
    ui::InternedId port_name = path.segment();

    if (parent.kind() == PathKind::Root) {
        auto maybe = root.iface().find(port_name);
        if (!maybe.has_value()) {
            return std::nullopt;
        }
        ResolvedPort rp;
        rp.port = *maybe;
        rp.blueprint_path = arena.root();
        rp.is_boundary = true;
        return rp;
    }

    if (parent.kind() == PathKind::Nested) {
        auto nctx = resolve_nested_context(parent, root, arena, registry);
        if (!nctx) {
            return std::nullopt;
        }
        auto maybe = nctx->nested->iface.find(port_name);
        if (!maybe.has_value()) {
            return std::nullopt;
        }
        ResolvedPort rp;
        rp.port = *maybe;
        rp.blueprint_path = arena.parent(parent);
        rp.is_boundary = true;
        return rp;
    }

    if (parent.kind() == PathKind::Node) {
        auto ctx = resolve_node_context(parent, root, arena, registry);
        if (!ctx) {
            return std::nullopt;
        }
        auto const* iface = node_interface(*ctx->node, registry);
        if (!iface) {
            return std::nullopt;
        }
        auto maybe = iface->find(port_name);
        if (!maybe.has_value()) {
            return std::nullopt;
        }
        ResolvedPort rp;
        rp.port = *maybe;
        rp.blueprint_path = ctx->blueprint_path;
        rp.is_boundary = false;
        return rp;
    }

    return std::nullopt;
}

bool PathResolver::direction_compatible(Direction source, Direction target) const {
    bool src_can_drive = (source == Direction::Output || source == Direction::InOut);
    bool tgt_can_receive = (target == Direction::Input || target == Direction::InOut);
    return src_can_drive && tgt_can_receive;
}

bool PathResolver::can_connect(Path const& source,
                               Path const& target,
                               Blueprint const& root,
                               PathArena const& arena,
                               TypeRegistry const& registry) const {
    if (source == target) {
        return false;
    }

    auto src = resolve(source, root, arena, registry);
    auto tgt = resolve(target, root, arena, registry);
    if (!src || !tgt) {
        return false;
    }

    if (src->port.domain != tgt->port.domain) {
        return false;
    }

    if (!direction_compatible(src->port.direction, tgt->port.direction)) {
        return false;
    }

    // Boundary rule: cannot connect directly across two different internal
    // blueprint scopes. Crossing must go through an interface boundary port.
    if (!src->is_boundary && !tgt->is_boundary
        && src->blueprint_path != tgt->blueprint_path) {
        return false;
    }

    // If one side is a boundary port and the other is internal, they must
    // belong to the same blueprint scope.
    if (src->is_boundary != tgt->is_boundary
        && src->blueprint_path != tgt->blueprint_path) {
        return false;
    }

    return true;
}

} // namespace bp2
