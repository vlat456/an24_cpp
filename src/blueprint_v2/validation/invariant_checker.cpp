#include "invariant_checker.h"

#include "owner_scope.h"
#include "blueprint_v2/interface/type_definition_interface.h"
#include "blueprint_v2/library/library_path.h"
#include <unordered_set>

namespace {

static std::string iid_to_string(ui::InternedId id) {
    return std::to_string(id.raw());
}

} // namespace

namespace bp2 {

InvariantChecker::Result InvariantChecker::validate(Blueprint const& bp,
                                                    PathArena const& arena,
                                                    const ::TypeRegistry& parser_registry,
                                                    ui::StringInterner& interner) {
    Result out;
    out.valid = false;

    std::unordered_set<ui::InternedId> node_ids;
    for (auto const& n : bp.nodes()) {
        if (!node_ids.insert(n.semantic.id).second) {
            out.error = "duplicate node ID: " + iid_to_string(n.semantic.id);
            return out;
        }
    }

    std::unordered_set<ui::InternedId> wire_ids;
    for (auto const& w : bp.wires()) {
        if (!wire_ids.insert(w.id).second) {
            out.error = "duplicate wire ID: " + iid_to_string(w.id);
            return out;
        }
    }

    std::unordered_set<ui::InternedId> nested_ids;
    for (auto const& n : bp.nested()) {
        if (!nested_ids.insert(n.id).second) {
            out.error = "duplicate nested ID: " + iid_to_string(n.id);
            return out;
        }
    }

    for (auto const& n : bp.nested()) {
        if (n.is_reference()
            && !parser_registry.has(std::string(interner.resolve(n.blueprint_id())))) {
            out.error = "unknown nested blueprint id=" + iid_to_string(n.id)
                + " blueprint_id=" + iid_to_string(n.blueprint_id());
            return out;
        }
        if (n.is_reference()) {
            const auto* def = parser_registry.get(std::string(interner.resolve(n.blueprint_id())));
            if (!def) {
                out.error = "unknown nested blueprint id=" + iid_to_string(n.id)
                    + " blueprint_id=" + iid_to_string(n.blueprint_id());
                return out;
            }
            Interface expected = interface_from_type_definition(*def, interner);
            if (n.resolved_iface() != expected) {
                out.error = "referenced nested resolved iface desynced from registry at nested id="
                    + iid_to_string(n.id);
                return out;
            }
        }

        const auto* host = bp.find_host_node(n);
        if (!host) {
            out.error = "nested instance missing host node id=" + iid_to_string(n.id);
            return out;
        }
        if (!host->view.expandable) {
            out.error = "nested host node must be expandable id=" + iid_to_string(n.id);
            return out;
        }
    }

    for (auto const& node : bp.nodes()) {
        if (!parser_registry.has(std::string(interner.resolve(node.semantic.type)))) {
            // Embedded blueprint proxy nodes carry a user-given type name
            // that won't be in the library registry. They are valid only when
            // the host↔nested structural invariant has already been satisfied.
            if (bp.is_embedded_proxy_node(node)) {
                continue;
            }
            out.error = "unknown node type at node id=" + iid_to_string(node.semantic.id)
                + " type=" + iid_to_string(node.semantic.type);
            return out;
        }
    }

    for (auto const& node : bp.nodes()) {
        const auto* nested = bp.find_hosted_nested(node);
        if (!nested) {
            if (auto owner_scope_err = validate_owner_scope_reference(bp, node, interner)) {
                out.error = "invalid owner_scope at node id=" + iid_to_string(node.semantic.id)
                    + ": " + *owner_scope_err;
                return out;
            }
            continue;
        }
        if (node.semantic.iface != nested->resolved_iface()) {
            out.error = "composite host iface desynced from nested authority at node id="
                + iid_to_string(node.semantic.id);
            return out;
        }

        // Reference-mode authority check: blueprint_id is authoritative.
        // Host blueprint_path is only an optional checked mirror.
        if (nested->is_reference() && !node.view.blueprint_path.empty()) {
            auto canonical_path = bp2::resolve_category_relative_blueprint_path(
                parser_registry,
                std::string(interner.resolve(nested->blueprint_id())));
            if (!canonical_path.has_value()) {
                out.error = "referenced nested blueprint not found in registry: id="
                    + iid_to_string(nested->id)
                    + " blueprint_id=" + iid_to_string(nested->blueprint_id());
                return out;
            }

            if (node.view.blueprint_path != *canonical_path) {
                out.error = "referenced nested host blueprint_path mismatch at node id="
                    + iid_to_string(node.semantic.id)
                    + " expected=" + *canonical_path
                    + " got=" + node.view.blueprint_path;
                return out;
            }
        }

        if (auto owner_scope_err = validate_owner_scope_reference(bp, node, interner)) {
            out.error = "invalid owner_scope at node id=" + iid_to_string(node.semantic.id)
                + ": " + *owner_scope_err;
            return out;
        }
    }

    for (auto const& w : bp.wires()) {
        auto wr = WireValidator::validate(w, bp, arena, parser_registry, interner);
        if (!wr.valid) {
            out.error = "wire id=" + iid_to_string(w.id) + ": " + wr.error;
            return out;
        }
    }

    out.valid = true;
    return out;
}

} // namespace bp2
