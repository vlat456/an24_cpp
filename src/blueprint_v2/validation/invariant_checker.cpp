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

            // Issue #88 Gap #4: Validate embedded blueprints recursively
            if (node.source->is_embedded()) {
                const Blueprint* embedded_bp = node.source->inline_def();
                if (!embedded_bp) {
                    out.error = "blueprint-instance node embedded blueprint is null at node id="
                        + iid_to_string(node.semantic.id);
                    return out;
                }
                // Recursively validate embedded blueprint
                auto nested_result = InvariantChecker::validate(*embedded_bp, arena, parser_registry, interner);
                if (!nested_result.valid) {
                    out.error = "embedded blueprint at node id=" + iid_to_string(node.semantic.id)
                        + " contains invalid content: " + nested_result.error;
                    return out;
                }
            }

            // Blueprint-instance interface derives from source authority only.
            // node.semantic.iface must stay empty so misleading mirror state
            // cannot drift alongside source authority.
            if (!node.semantic.iface.ports().empty()) {
                out.error = "blueprint-instance node carries non-empty semantic.iface at node id="
                    + iid_to_string(node.semantic.id);
                return out;
            }

            continue;
        }
    }

    // Issue #88 Gap #5: Validate component node interfaces match registry
    // Bridge nodes (BlueprintInput/BlueprintOutput) are intentionally specialized
    // at creation time (port_type/domain narrowed from Any to actual wire type),
    // so they are exempt from the exact-match check.
    const ui::InternedId bridge_input_type  = interner.intern("BlueprintInput");
    const ui::InternedId bridge_output_type = interner.intern("BlueprintOutput");
    for (auto const& node : bp.nodes()) {
        if (node.is_component()) {
            if (node.semantic.type == bridge_input_type || node.semantic.type == bridge_output_type) {
                continue;
            }
            const std::string type_name(interner.resolve(node.semantic.type));
            const auto* def = parser_registry.get(type_name);
            if (!def) {
                // Type not found (already caught by earlier type check)
                continue;
            }
            Interface expected = interface_from_type_definition(*def, interner);
            if (node.semantic.iface != expected) {
                out.error = "component node iface desynced from registry at node id="
                    + iid_to_string(node.semantic.id) + " type=" + iid_to_string(node.semantic.type);
                return out;
            }
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
