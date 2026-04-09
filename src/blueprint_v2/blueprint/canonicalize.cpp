#include "canonicalize.h"

namespace bp2 {

Blueprint clone_metadata(const Blueprint& bp) {
    Blueprint rebuilt;
    rebuilt = rebuilt.with_id(bp.id());
    rebuilt = rebuilt.with_display_name(bp.display_name());
    rebuilt = rebuilt.with_interface(bp.iface());
    rebuilt = rebuilt.with_viewport(bp.pan_x(), bp.pan_y(), bp.zoom(), bp.grid_step());
    rebuilt = rebuilt.with_name(bp.name());
    return rebuilt;
}

Blueprint::Node canonicalize_composite_host_iface(const Blueprint& bp, Blueprint::Node node) {
    const auto* nested = bp.find_hosted_nested(node);
    if (!nested) {
        return node;
    }
    node.semantic.iface = nested->resolved_iface();
    return node;
}

Blueprint canonicalize_composite_host_ifaces(Blueprint bp) {
    bool changed = false;
    Blueprint rebuilt = clone_metadata(bp);

    for (const auto& node : bp.nodes()) {
        Blueprint::Node normalized = canonicalize_composite_host_iface(bp, node);
        changed = changed || !(normalized.semantic.iface == node.semantic.iface);
        rebuilt = rebuilt.with_node(std::move(normalized));
    }
    for (const auto& wire : bp.wires()) {
        rebuilt = rebuilt.with_wire(wire);
    }
    for (const auto& nested : bp.nested()) {
        rebuilt = rebuilt.with_nested(nested);
    }

    return changed ? rebuilt : bp;
}

} // namespace bp2
