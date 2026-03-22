#include "invariant_checker.h"

#include <unordered_set>

namespace bp2 {

InvariantChecker::Result InvariantChecker::validate(Blueprint const& bp,
                                                    PathArena const& arena,
                                                    TypeRegistry const& registry) {
    Result out;
    out.valid = false;

    std::unordered_set<ui::InternedId> node_ids;
    for (auto const& n : bp.nodes()) {
        if (!node_ids.insert(n.id).second) {
            out.error = "duplicate node ID";
            return out;
        }
    }

    std::unordered_set<ui::InternedId> wire_ids;
    for (auto const& w : bp.wires()) {
        if (!wire_ids.insert(w.id).second) {
            out.error = "duplicate wire ID";
            return out;
        }
    }

    std::unordered_set<ui::InternedId> nested_ids;
    for (auto const& n : bp.nested()) {
        if (!nested_ids.insert(n.id).second) {
            out.error = "duplicate nested ID";
            return out;
        }
    }

    for (auto const& node : bp.nodes()) {
        if (!registry.has(node.type)) {
            out.error = "unknown node type";
            return out;
        }
    }

    for (auto const& n : bp.nested()) {
        if (n.embedded && !n.inline_def) {
            out.error = "embedded nested missing inline_def";
            return out;
        }
        if (!n.embedded && n.blueprint_id.empty()) {
            out.error = "non-embedded nested missing blueprint_id";
            return out;
        }
        if (!n.embedded && !n.blueprint_id.empty() && !registry.has(n.blueprint_id)) {
            out.error = "unknown nested blueprint";
            return out;
        }
    }

    for (auto const& w : bp.wires()) {
        auto wr = WireValidator::validate(w, bp, arena, registry);
        if (!wr.valid) {
            out.error = wr.error;
            return out;
        }
    }

    out.valid = true;
    return out;
}

} // namespace bp2
