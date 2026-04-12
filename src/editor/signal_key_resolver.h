#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "ui/core/interned_id.h"
#include <string>
#include <string_view>

namespace editor {

/// Signal key resolution context mode
enum class SignalKeyContextMode {
    Root,               ///< Root-level blueprint (may contain expandable composites)
    EmbeddedScope,      ///< Embedded child blueprint window under a parent instance id
    ExternalReference   ///< External reference window (child blueprint of a composite)
};

/// Context for signal key resolution
struct SignalKeyContext {
    SignalKeyContextMode mode;
    std::string_view parent_instance_id;  ///< For ExternalReference mode, the parent composite instance id
};

inline SignalKeyContext root_signal_context() {
    return {SignalKeyContextMode::Root, ""};
}

inline SignalKeyContext embedded_signal_context(std::string_view parent_instance_id) {
    return {SignalKeyContextMode::EmbeddedScope, parent_instance_id};
}

inline SignalKeyContext external_ref_signal_context(std::string_view parent_instance_id) {
    return {SignalKeyContextMode::ExternalReference, parent_instance_id};
}

/// Signal endpoint descriptor for resolver input
struct SignalEndpoint {
    const bp2::Blueprint::Node* node;     ///< Node pointer (may be null)
    ui::InternedId node_iid;              ///< Node interned id
    ui::InternedId port_iid;              ///< Port interned id
};

/// Resolve runtime signal key for a blueprint port endpoint.
///
/// This is the canonical resolver for all UI signal key lookups.
/// It handles:
/// - Root mode normal nodes → "node.port"
/// - Root mode expandable composites → "node.port"
/// - External reference mode (child blueprint) → "parent:child_node.port"
///
/// Returns empty string if endpoint node_iid or port_iid is empty (defensive).
///
/// @param bp            The blueprint being displayed (root or external)
/// @param interner      String interner for that blueprint
/// @param endpoint      Signal endpoint (node and port ids)
/// @param context       Resolution context (mode + parent info)
/// @return              Runtime simulator signal key, or empty string if IDs are invalid
std::string resolve_runtime_signal_key(
    const bp2::Blueprint& bp,
    const ui::StringInterner& interner,
    const SignalEndpoint& endpoint,
    const SignalKeyContext& context);

} // namespace editor
