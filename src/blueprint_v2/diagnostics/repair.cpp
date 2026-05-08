#include "repair.h"
#include "blueprint_v2/validation/path_resolver.h"
#include "core/model/component_registry.h"
#include <unordered_set>

namespace bp2::diagnostics {

namespace {

static std::string iid(core::InternedId id) {
    return std::to_string(id.raw());
}

} // namespace

RepairReport diagnose_and_repair(Blueprint& bp,
                                 PathArena& arena,
                                 const ::ComponentRegistry& parser_registry,
                                 core::StringInterner& interner) {
    RepairReport report;

    std::unordered_set<core::InternedId> seen_nodes;
    for (const auto& n : bp.nodes()) {
        if (!seen_nodes.insert(n.semantic.id).second) {
            report.issues.push_back({
                IntegrityIssue::Kind::DuplicateNodeId,
                "duplicate node id=" + iid(n.semantic.id)
            });
        }
        if (!parser_registry.has(std::string(interner.resolve(n.semantic.type)))) {
            // Skip blueprint-instance nodes — their user-given type
            // is not in the library registry by design.
            const bool is_blueprint_instance = n.is_blueprint_instance();
            if (!is_blueprint_instance) {
                report.issues.push_back({
                    IntegrityIssue::Kind::UnknownNodeType,
                    "unknown node type at node id=" + iid(n.semantic.id)
                });
            }
        }
    }

    std::unordered_set<core::InternedId> seen_blueprint_instances;
    for (const auto& n : bp.nodes()) {
        if (!n.is_blueprint_instance()) {
            continue;
        }
        if (!seen_blueprint_instances.insert(n.semantic.id).second) {
            report.issues.push_back({
                IntegrityIssue::Kind::DuplicateNestedId,
                "duplicate blueprint instance id=" + iid(n.semantic.id)
            });
        }
        if (n.has_referenced_blueprint()
            && !parser_registry.has(std::string(interner.resolve(n.blueprint_instance().source.blueprint_id())))) {
            report.issues.push_back({
                IntegrityIssue::Kind::UnknownNestedBlueprint,
                "unknown blueprint instance id=" + iid(n.semantic.id)
            });
        }
    }

    std::unordered_set<core::InternedId> seen_wires;
    std::vector<core::InternedId> wires_to_remove;
    PathResolver const resolver;
    for (const auto& w : bp.wires()) {
        if (!seen_wires.insert(w.id).second) {
            report.issues.push_back({
                IntegrityIssue::Kind::DuplicateWireId,
                "duplicate wire id=" + iid(w.id)
            });
        }

        const auto src = resolver.resolve(w.source, bp, parser_registry, interner);
        const auto tgt = resolver.resolve(w.target, bp, parser_registry, interner);
        if (!src || !tgt) {
            report.issues.push_back({
                IntegrityIssue::Kind::InvalidWireEndpoint,
                "invalid wire endpoint id=" + iid(w.id)
            });
            wires_to_remove.push_back(w.id);
        }
    }

    for (core::InternedId const wid : wires_to_remove) {
        bp = bp.without_wire(wid);
    }
    report.removed_wires = wires_to_remove.size();
    report.changed = !wires_to_remove.empty();

    return report;
}

} // namespace bp2::diagnostics
