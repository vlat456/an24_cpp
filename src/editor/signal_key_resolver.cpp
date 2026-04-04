#include "signal_key_resolver.h"
#include "external_ref_mapping.h"

namespace editor {

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
        if (endpoint.node && endpoint.node->expandable && !endpoint.node->blueprint_path.empty()) {
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
