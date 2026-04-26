#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "core/strings/interned_id.h"

namespace editor {

enum class SignalKeyContextMode {
    Root,
    EmbeddedScope,
    ExternalReference
};

struct SignalKeyContext {
    SignalKeyContextMode mode;
    core::InternedId parent_instance_id;  // Changed from std::string_view
};

inline SignalKeyContext root_signal_context() {
    return {SignalKeyContextMode::Root, {}};
}

inline SignalKeyContext embedded_signal_context(core::InternedId parent_instance_id) {
    return {SignalKeyContextMode::EmbeddedScope, parent_instance_id};
}

inline SignalKeyContext external_ref_signal_context(core::InternedId parent_instance_id) {
    return {SignalKeyContextMode::ExternalReference, parent_instance_id};
}

struct SignalEndpoint {
    const bp2::Blueprint::Node* node;
    core::InternedId node_iid;
    core::InternedId port_iid;
};

/// Resolve runtime signal key as InternedId.
/// Takes both the blueprint interner (for resolving node/port names to strings
/// for key construction) and the simulation interner (for looking up the result).
/// Returns empty InternedId if endpoint is invalid or key not found.
core::InternedId resolve_runtime_signal_key(
    const bp2::Blueprint& bp,
    const core::StringInterner& bp_interner,
    const core::StringInterner& sim_interner,
    const SignalEndpoint& endpoint,
    const SignalKeyContext& context);

} // namespace editor