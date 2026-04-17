#include "path_resolver.h"

#include "blueprint_v2/interface/port_compatibility.h"
#include "blueprint_v2/interface/type_definition_interface.h"
#include "json_parser/json_parser.h"

#include <algorithm>

namespace bp2 {

std::optional<ResolvedPort> PathResolver::resolve(Path const& path,
                                                  Blueprint const& root,
                                                  PathArena const& arena,
                                                  const ::TypeRegistry& parser_registry,
                                                  ui::StringInterner& interner) const {
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
        return ResolvedPort{*maybe, arena.root(), true};
    }

    if (parent.kind() == PathKind::Nested) {
        // In the new model, nested paths refer to blueprint-instance nodes.
        // Walk up the path chain and resolve each blueprint-instance node.
        std::vector<Path> chain;
        Path cur = parent;
        while (cur.kind() != PathKind::Root) {
            chain.push_back(cur);
            cur = arena.parent(cur);
        }
        std::reverse(chain.begin(), chain.end());

        const Blueprint* current_bp = &root;
        for (Path seg : chain) {
            if (seg.kind() != PathKind::Node) {
                return std::nullopt;
            }
            // Find the blueprint-instance node with this ID
            const auto* bp_node = current_bp->find_blueprint_instance(seg.segment());
            if (!bp_node || !bp_node->source) {
                return std::nullopt;
            }
            // Only inline definitions allow traversal through ports
            if (auto* def = bp_node->source->inline_def()) {
                current_bp = def;
            } else {
                return std::nullopt;
            }
        }

        auto maybe = current_bp->iface().find(port_name);
        if (!maybe.has_value()) {
            return std::nullopt;
        }
        return ResolvedPort{*maybe, arena.parent(parent), true};
    }

    if (parent.kind() == PathKind::Node) {
        std::vector<Path> chain;
        Path cur = parent;
        while (cur.kind() != PathKind::Root) {
            chain.push_back(cur);
            cur = arena.parent(cur);
        }
        std::reverse(chain.begin(), chain.end());

        const Blueprint* current_bp = &root;
        Path current_bp_path = arena.root();
        const Blueprint::Node* node = nullptr;
        for (size_t i = 0; i < chain.size(); ++i) {
            Path seg = chain[i];
            const bool is_last = (i + 1 == chain.size());
            
            if (seg.kind() == PathKind::Node && is_last) {
                node = current_bp->find_node(seg.segment());
                if (!node) {
                    return std::nullopt;
                }
                break;
            }
            
            return std::nullopt;
        }

        if (!node) {
            return std::nullopt;
        }

        const Interface& node_iface = current_bp->effective_node_iface(*node);
        if (!node_iface.empty()) {
            auto maybe = node_iface.find(port_name);
            if (!maybe.has_value()) {
                return std::nullopt;
            }
            return ResolvedPort{*maybe, current_bp_path, false};
        }

        const std::string type_name(interner.resolve(node->semantic.type));
        const auto* def = parser_registry.get(type_name);
        if (!def) {
            return std::nullopt;
        }
        const std::string port_name_str(interner.resolve(port_name));
        auto pit = def->ports.find(port_name_str);
        if (pit == def->ports.end()) {
            return std::nullopt;
        }

        return ResolvedPort{port_descriptor_from_type_port(port_name, pit->second), current_bp_path, false};
    }

    return std::nullopt;
}

bool PathResolver::direction_compatible(Direction source, Direction target) const {
    bool src_can_drive = (source == Direction::Output || source == Direction::InOut);
    bool tgt_can_receive = (target == Direction::Input || target == Direction::InOut);
    return src_can_drive && tgt_can_receive;
}

// ==================================================================
// WireEndpoint overloads — wire endpoints are always Node→Port
// ==================================================================

std::optional<ResolvedPort> PathResolver::resolve(WireEndpoint const& ep,
                                                  Blueprint const& root,
                                                  const ::TypeRegistry& parser_registry,
                                                  ui::StringInterner& interner) const {
    const auto* node = root.find_node(ep.node);
    if (!node) {
        return std::nullopt;
    }

    const Interface& node_iface = root.effective_node_iface(*node);
    if (!node_iface.empty()) {
        auto maybe = node_iface.find(ep.port);
        if (!maybe.has_value()) {
            return std::nullopt;
        }
        return ResolvedPort{*maybe, Path{}, false};
    }

    const std::string type_name(interner.resolve(node->semantic.type));
    const auto* def = parser_registry.get(type_name);
    if (!def) {
        return std::nullopt;
    }
    const std::string port_name_str(interner.resolve(ep.port));
    auto pit = def->ports.find(port_name_str);
    if (pit == def->ports.end()) {
        return std::nullopt;
    }

    return ResolvedPort{port_descriptor_from_type_port(ep.port, pit->second), Path{}, false};
}

bool PathResolver::can_connect(WireEndpoint const& source,
                               WireEndpoint const& target,
                               Blueprint const& root,
                               const ::TypeRegistry& parser_registry,
                               ui::StringInterner& interner) const {
    if (source == target) {
        return false;
    }

    auto src = resolve(source, root, parser_registry, interner);
    auto tgt = resolve(target, root, parser_registry, interner);
    if (!src || !tgt) {
        return false;
    }

    if (!port_domains_compatible(src->port, tgt->port)) {
        return false;
    }

    if (!direction_compatible(src->port.direction, tgt->port.direction)) {
        return false;
    }

    return true;
}

bool PathResolver::can_connect(Path const& source,
                               Path const& target,
                               Blueprint const& root,
                               PathArena const& arena,
                               const ::TypeRegistry& parser_registry,
                               ui::StringInterner& interner) const {
    if (source == target) {
        return false;
    }

    auto src = resolve(source, root, arena, parser_registry, interner);
    auto tgt = resolve(target, root, arena, parser_registry, interner);
    if (!src || !tgt) {
        return false;
    }

    if (!port_domains_compatible(src->port, tgt->port)) {
        return false;
    }

    if (!direction_compatible(src->port.direction, tgt->port.direction)) {
        return false;
    }

    if (!src->is_boundary && !tgt->is_boundary && src->blueprint_path != tgt->blueprint_path) {
        return false;
    }

    if (src->is_boundary != tgt->is_boundary && src->blueprint_path != tgt->blueprint_path) {
        return false;
    }

    return true;
}

} // namespace bp2
