#include "blueprint_replace.h"
#include "blueprint_v2/blueprint/canonicalize.h"

namespace bp2 {

Blueprint replace_node_preserve_order(const Blueprint& bp, Blueprint::Node updated) {
    Blueprint rebuilt = clone_metadata(bp);

    bool replaced = false;
    for (const auto& n : bp.nodes()) {
        if (n.semantic.id == updated.semantic.id) {
            rebuilt = rebuilt.with_node(std::move(updated));
            replaced = true;
        } else {
            rebuilt = rebuilt.with_node(n);
        }
    }
    if (!replaced) {
        rebuilt = rebuilt.with_node(std::move(updated));
    }

    for (const auto& w : bp.wires()) {
        rebuilt = rebuilt.with_wire(w);
    }

    return rebuilt;
}

Blueprint replace_wire_preserve_order(const Blueprint& bp, Blueprint::Wire updated) {
    Blueprint rebuilt = clone_metadata(bp);

    for (const auto& n : bp.nodes()) {
        rebuilt = rebuilt.with_node(n);
    }

    bool replaced = false;
    for (const auto& w : bp.wires()) {
        if (w.id == updated.id) {
            rebuilt = rebuilt.with_wire(std::move(updated));
            replaced = true;
        } else {
            rebuilt = rebuilt.with_wire(w);
        }
    }
    if (!replaced) {
        rebuilt = rebuilt.with_wire(std::move(updated));
    }

    return rebuilt;
}

MutationResult try_update_node(Blueprint& bp,
                               core::InternedId id,
                               const std::function<void(Blueprint::Node&)>& fn) {
    const auto* existing = bp.find_node(id);
    if (!existing) {
        return MutationResult::NotFound;
    }

    Blueprint::Node updated = *existing;
    fn(updated);
    if (updated == *existing) {
        return MutationResult::NoChange;
    }

    bp = replace_node_preserve_order(bp, std::move(updated));
    return MutationResult::Changed;
}

MutationResult try_update_wire(Blueprint& bp,
                               core::InternedId id,
                               const std::function<void(Blueprint::Wire&)>& fn) {
    const auto* existing = bp.find_wire(id);
    if (!existing) {
        return MutationResult::NotFound;
    }

    Blueprint::Wire updated = *existing;
    fn(updated);
    if (updated == *existing) {
        return MutationResult::NoChange;
    }

    bp = replace_wire_preserve_order(bp, std::move(updated));
    return MutationResult::Changed;
}

} // namespace bp2