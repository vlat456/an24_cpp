#include "signal_key_resolver.h"
#include "external_ref_mapping.h"
#include "core/solvers/common/signal_key.h"

namespace editor {

/// For a blueprint-instance node with embedded children, find the actual bridge node ID
/// for a given interface port. Bridge nodes use the canonical colon convention:
///   "proxy_id:port_name"
/// Returns the bridge node InternedId if found, or empty InternedId.
static ui::InternedId find_embedded_bridge_node(
    const bp2::Blueprint& bp,
    const ui::StringInterner& sim_interner,
    std::string_view proxy_id,
    std::string_view port_name) {

    // Canonical colon-convention node: proxy_id:port_name
    std::string colon_id = signal_key::make_child_scope_key(proxy_id, port_name);

    auto colon_iid = sim_interner.lookup(colon_id);
    if (!colon_iid.empty()) {
        const auto* colon_node = bp.find_node(colon_iid);
        if (colon_node) {
            return colon_iid;
        }
    }

    return {};
}

ui::InternedId resolve_runtime_signal_key(
    const bp2::Blueprint& bp,
    const ui::StringInterner& bp_interner,
    const ui::StringInterner& sim_interner,
    const SignalEndpoint& endpoint,
    const SignalKeyContext& context) {

    // Defensive: reject empty endpoint IDs early
    if (endpoint.node_iid.empty() || endpoint.port_iid.empty()) {
        return {};
    }

    std::string_view node_sv = bp_interner.resolve(endpoint.node_iid);
    std::string_view port_sv = bp_interner.resolve(endpoint.port_iid);

    if (context.mode == SignalKeyContextMode::Root) {
        // Root mode: check if node is a blueprint instance
        if (endpoint.node && endpoint.node->is_blueprint_instance()) {
            // For embedded blueprints with materialized children, the bridge
            // node ID may differ from the default colon convention.
            if (endpoint.node->has_embedded_blueprint()) {
                ui::InternedId bridge_iid = find_embedded_bridge_node(bp, sim_interner, node_sv, port_sv);
                if (!bridge_iid.empty()) {
                    std::string bridge_str(bp_interner.resolve(bridge_iid));
                    std::string exposed_key = signal_key::make_exposed_node_port_from_bridge_node(bridge_str);
                    if (!exposed_key.empty()) {
                        return sim_interner.lookup(exposed_key);
                    }
                }
            }
            if (!endpoint.node->has_embedded_blueprint() && !endpoint.node->has_referenced_blueprint()) {
                return sim_interner.lookup(build_signal_key(node_sv, port_sv));
            }
            return sim_interner.lookup(map_composite_port_key(node_sv, port_sv));
        }
        return sim_interner.lookup(build_signal_key(node_sv, port_sv));
    } else {
        // Embedded and external child-blueprint views use the same runtime key
        // rule: resolve the local child node.port, then prefix with the parent
        // instance id that owns the child blueprint in the runtime graph.
        std::string_view parent_sv = bp_interner.resolve(context.parent_instance_id);
        std::string child_key = build_signal_key(node_sv, port_sv);
        return sim_interner.lookup(resolve_external_ref_signal_key(parent_sv, child_key));
    }
}

} // namespace editor