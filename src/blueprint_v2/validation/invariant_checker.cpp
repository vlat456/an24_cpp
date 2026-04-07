#include "invariant_checker.h"

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
        if (!node_ids.insert(n.id).second) {
            out.error = "duplicate node ID: " + iid_to_string(n.id);
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

    for (auto const& node : bp.nodes()) {
        if (!parser_registry.has(std::string(interner.resolve(node.type)))) {
            // Embedded blueprint proxy nodes carry a user-given type name
            // that won't be in the library registry.  They are valid as long
            // as a matching embedded nested definition exists.
            if (node.expandable) {
                const auto* nested = bp.find_nested(node.id);
                if (nested && nested->embedded) continue;
            }
            out.error = "unknown node type at node id=" + iid_to_string(node.id)
                + " type=" + iid_to_string(node.type);
            return out;
        }
    }

    for (auto const& n : bp.nested()) {
        if (n.embedded && !n.inline_def) {
            out.error = "embedded nested missing inline_def id=" + iid_to_string(n.id);
            return out;
        }
        if (!n.embedded && n.blueprint_id.empty()) {
            out.error = "non-embedded nested missing blueprint_id id=" + iid_to_string(n.id);
            return out;
        }
        if (!n.embedded && !n.blueprint_id.empty()
            && !parser_registry.has(std::string(interner.resolve(n.blueprint_id)))) {
            out.error = "unknown nested blueprint id=" + iid_to_string(n.id)
                + " blueprint_id=" + iid_to_string(n.blueprint_id);
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
