#include "invariant_checker.h"

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

    // Blueprint instances are now node-owned; no separate nested collection.
    // Validate blueprint-instance nodes inline during node iteration below.

    for (auto const& node : bp.nodes()) {
        if (!parser_registry.has(std::string(interner.resolve(node.semantic.type)))) {
            // Blueprint-instance nodes can carry a user-given type name
            // that won't be in the library registry; authority is the source blueprint_id.
            if (node.is_blueprint_instance()) {
                continue;
            }
            out.error = "unknown node type at node id=" + iid_to_string(node.semantic.id)
                + " type=" + iid_to_string(node.semantic.type);
            return out;
        }
    }

    for (auto const& node : bp.nodes()) {
        // Validate blueprint-instance nodes (node-owned sources)
        if (node.is_blueprint_instance()) {
            if (!node.source) {
                out.error = "blueprint-instance node missing source at node id="
                    + iid_to_string(node.semantic.id);
                return out;
            }

            if (node.source->is_reference()) {
                if (!parser_registry.has(std::string(interner.resolve(node.source->blueprint_id())))) {
                    out.error = "unknown referenced blueprint id=" + iid_to_string(node.semantic.id)
                        + " blueprint_id=" + iid_to_string(node.source->blueprint_id());
                    return out;
                }
                const auto* def = parser_registry.get(std::string(interner.resolve(node.source->blueprint_id())));
                if (!def) {
                    out.error = "unknown referenced blueprint id=" + iid_to_string(node.semantic.id)
                        + " blueprint_id=" + iid_to_string(node.source->blueprint_id());
                    return out;
                }
                Interface expected = interface_from_type_definition(*def, interner);
                if (node.source->resolved_iface() != expected) {
                    out.error = "referenced blueprint resolved iface desynced from registry at node id="
                        + iid_to_string(node.semantic.id);
                    return out;
                }
            }

            // Validate semantic interface matches source authority
            if (node.semantic.iface != node.source->resolved_iface()) {
                out.error = "blueprint-instance node iface desynced from source authority at node id="
                    + iid_to_string(node.semantic.id);
                return out;
            }

            continue;
        }
    }

    for (auto const& w : bp.wires()) {
        auto wr = WireValidator::validate(w, bp, parser_registry, interner);
        if (!wr.valid) {
            out.error = "wire id=" + iid_to_string(w.id) + ": " + wr.error;
            return out;
        }
    }

    out.valid = true;
    return out;
}

} // namespace bp2
