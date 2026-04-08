#include "signal_key_resolver.h"
#include "external_ref_mapping.h"
#include "core/solvers/common/signal_key.h"

namespace editor {

/// For an embedded expandable proxy node, find the actual bridge node ID
/// for a given interface port. Bridge nodes use the canonical colon convention:
///   "proxy_id:port_name"
/// Returns the bridge node ID if found, or empty string.
static std::string find_embedded_bridge_node(
    const bp2::Blueprint& bp,
    const ui::StringInterner& interner,
    std::string_view proxy_id,
    std::string_view port_name) {

    // Canonical colon-convention node: proxy_id:port_name
    std::string colon_id = signal_key::make_child_scope_key(proxy_id, port_name);

    auto colon_iid = interner.lookup(colon_id);
    if (!colon_iid.empty()) {
        const auto* colon_node = bp.find_node(colon_iid);
        if (colon_node) {
            return colon_id;
        }
    }

    return "";
}

std::string resolve_runtime_signal_key(
    const bp2::Blueprint& bp,
    const ui::StringInterner& interner,
    const SignalEndpoint& endpoint,
    const SignalKeyContext& context) {
    
    // Defensive: reject empty endpoint IDs early
    if (endpoint.node_iid.empty() || endpoint.port_iid.empty()) {
        return "";
    }
    
    std::string_view node_sv = interner.resolve(endpoint.node_iid);
    std::string_view port_sv = interner.resolve(endpoint.port_iid);

    if (context.mode == SignalKeyContextMode::Root) {
        // Root mode: check if node is expandable composite
        if (endpoint.node && endpoint.node->view.expandable && !endpoint.node->view.blueprint_path.empty()) {
            // For embedded blueprints with materialized children, the bridge
            // node ID may differ from the default colon convention.
            const auto* nested = bp.find_nested(endpoint.node_iid);
            if (nested && nested->embedded) {
                std::string bridge_id = find_embedded_bridge_node(bp, interner, node_sv, port_sv);
                if (!bridge_id.empty()) {
                    std::string exposed_key = signal_key::make_exposed_node_port_from_bridge_node(bridge_id);
                    if (!exposed_key.empty()) {
                        return exposed_key;
                    }
                }
            }
            return map_composite_port_key(node_sv, port_sv);
        }
        return build_signal_key(node_sv, port_sv);
    } else {
        // ExternalReference mode: build child key, then parent-prefix it
        std::string child_key = build_signal_key(node_sv, port_sv);
        return resolve_external_ref_signal_key(context.parent_instance_id, child_key);
    }
}

} // namespace editor
