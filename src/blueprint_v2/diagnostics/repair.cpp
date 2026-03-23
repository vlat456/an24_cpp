#include "repair.h"

#include "blueprint_v2/validation/path_resolver.h"
#include <unordered_set>

namespace bp2::diagnostics {

namespace {

static std::string iid(ui::InternedId id) {
    return std::to_string(id.raw());
}

} // namespace

RepairReport diagnose_and_repair(Blueprint& bp,
                                 PathArena& arena,
                                 TypeRegistry const& registry) {
    RepairReport report;

    std::unordered_set<ui::InternedId> seen_nodes;
    for (const auto& n : bp.nodes()) {
        if (!seen_nodes.insert(n.id).second) {
            report.issues.push_back({
                IntegrityIssue::Kind::DuplicateNodeId,
                "duplicate node id=" + iid(n.id)
            });
        }
        if (!registry.has(n.type)) {
            report.issues.push_back({
                IntegrityIssue::Kind::UnknownNodeType,
                "unknown node type at node id=" + iid(n.id)
            });
        }
    }

    std::unordered_set<ui::InternedId> seen_nested;
    for (const auto& n : bp.nested()) {
        if (!seen_nested.insert(n.id).second) {
            report.issues.push_back({
                IntegrityIssue::Kind::DuplicateNestedId,
                "duplicate nested id=" + iid(n.id)
            });
        }
        if (n.embedded && !n.inline_def) {
            report.issues.push_back({
                IntegrityIssue::Kind::EmbeddedNestedMissingDefinition,
                "embedded nested missing definition id=" + iid(n.id)
            });
        }
        if (!n.embedded && !n.blueprint_id.empty() && !registry.has(n.blueprint_id)) {
            report.issues.push_back({
                IntegrityIssue::Kind::UnknownNestedBlueprint,
                "unknown nested blueprint id=" + iid(n.id)
            });
        }
    }

    std::unordered_set<ui::InternedId> seen_wires;
    std::vector<ui::InternedId> wires_to_remove;
    PathResolver resolver;
    for (const auto& w : bp.wires()) {
        if (!seen_wires.insert(w.id).second) {
            report.issues.push_back({
                IntegrityIssue::Kind::DuplicateWireId,
                "duplicate wire id=" + iid(w.id)
            });
        }

        const auto src = resolver.resolve(w.source, bp, arena, registry);
        const auto tgt = resolver.resolve(w.target, bp, arena, registry);
        if (!src || !tgt) {
            report.issues.push_back({
                IntegrityIssue::Kind::InvalidWireEndpoint,
                "invalid wire endpoint id=" + iid(w.id)
            });
            wires_to_remove.push_back(w.id);
        }
    }

    for (ui::InternedId wid : wires_to_remove) {
        bp = bp.without_wire(wid);
    }
    report.removed_wires = wires_to_remove.size();
    report.changed = !wires_to_remove.empty();

    return report;
}

} // namespace bp2::diagnostics
