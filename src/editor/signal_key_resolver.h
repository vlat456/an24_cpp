#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "ui/core/interned_id.h"
#include "external_ref_mapping.h"
#include <string>
#include <string_view>

namespace editor {

/// Signal key resolution context mode
enum class SignalKeyContextMode {
    Root,               ///< Root-level blueprint (may contain expandable composites)
    ExternalReference   ///< External reference window (child blueprint of a composite)
};

/// Context for signal key resolution
struct SignalKeyContext {
    SignalKeyContextMode mode;
    std::string_view parent_instance_id;  ///< For ExternalReference mode, the parent composite instance id
};

/// Signal endpoint descriptor for resolver input
struct SignalEndpoint {
    const bp2::Blueprint::Node* node;     ///< Node pointer (may be null)
    ui::InternedId node_iid;              ///< Node interned id
    ui::InternedId port_iif;              ///< Port interned id
};

/// Resolve runtime signal key for a blueprint port endpoint.
///
/// This is the canonical resolver for all UI signal key lookups.
/// It handles:
/// - Root mode normal nodes → "node.port"
/// - Root mode expandable composites → "node:port.ext"
/// - External reference mode (child blueprint) → "parent:child_node.port"
///
/// @param bp            The blueprint being displayed (root or external)
/// @param interner      String interner for that blueprint
/// @param endpoint      Signal endpoint (node and port ids)
/// @param context       Resolution context (mode + parent info)
/// @return              Runtime simulator signal key
std::string resolve_runtime_signal_key(
    const bp2::Blueprint& bp,
    const ui::StringInterner& interner,
    const SignalEndpoint& endpoint,
    const SignalKeyContext& context);

} // namespace editor

