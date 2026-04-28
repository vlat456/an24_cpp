#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "editor/visual/presentation/node_presentation.h"
#include "core/strings/interned_id.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bp2 {
class EditorModel;
class BlueprintLibrary;
class PathArena;
}

namespace core {
class StringInterner;
}

struct ComponentRegistry;

/// Narrow abstraction for CanvasInput's editing operations.
/// Covers only the mutation and query surface CanvasInput actually uses.
/// No fallback, no backward-compat layering.
class EditingHost {
public:
    virtual ~EditingHost() = default;

    /// Read the current blueprint (non-mutable).
    virtual const bp2::Blueprint& current_blueprint() const = 0;

    /// Look up a node in the current blueprint.
    virtual const bp2::Blueprint::Node* find_node(core::InternedId id) const = 0;

    /// Look up a wire in the current blueprint.
    virtual const bp2::Blueprint::Wire* find_wire(core::InternedId id) const = 0;

    /// Access all wires in the current blueprint.
    virtual const std::vector<bp2::Blueprint::Wire>& wires() const = 0;

    /// Access all nodes in the current blueprint.
    virtual const std::vector<bp2::Blueprint::Node>& nodes() const = 0;

    /// Save current state as undo checkpoint.
    virtual void push_checkpoint() = 0;

    /// Execute a batch of mutations as one undoable operation.
    virtual bool mutate_atomically(const std::function<void()>& fn) = 0;

    /// Replace entire blueprint with new one.
    virtual void replace_current(bp2::Blueprint bp) = 0;

    /// Add a wire to the current blueprint.
    /// Returns true if successfully added.
    virtual bool add_wire(bp2::Blueprint::Wire wire) = 0;

    /// Remove a wire by ID.
    /// Returns true if successfully removed.
    virtual bool remove_wire(core::InternedId id) = 0;

    /// Update a wire by ID with a mutation function.
    /// Returns true if successfully updated.
    virtual bool update_wire(core::InternedId id,
                             std::function<void(bp2::Blueprint::Wire&)> fn) = 0;

    /// Update a node's position.
    /// Returns true if successfully updated.
    virtual bool update_node_position(core::InternedId id, float x, float y) = 0;

    /// Update a node by ID with a mutation function.
    /// Returns true if successfully updated.
    virtual bool update_node(core::InternedId id,
                             std::function<void(bp2::Blueprint::Node&)> fn) = 0;

    /// Remove a node and any explicitly provided connected wires.
    virtual bool remove_node(core::InternedId id,
                             std::vector<core::InternedId> connected_wire_ids) = 0;

    /// Bake a referenced blueprint-instance node into an embedded source inside
    /// the current scoped blueprint.
    virtual bool bake_blueprint_instance(core::InternedId id,
                                         const bp2::BlueprintLibrary& library) = 0;

    /// Allocate a unique wire ID.
    virtual std::string allocate_wire_id() = 0;

    // ── Registry-backed queries ──
    // These delegate to ComponentRegistry internally. CanvasInput never
    // touches the registry directly — all model-level queries go through host.

    /// Resolve the visual frame kind for a node (Bus, Reference, Standard, etc.).
    /// Returns Standard for unknown nodes or when no registry is set.
    virtual editor::presentation::NodeFrameKind resolve_frame_kind(
        core::InternedId node_id) const {
        return editor::presentation::NodeFrameKind::Standard;
    }

    /// Resolve the model-level port type for a specific port on a node.
    /// Returns PortType::Any for unknown nodes/ports or when no registry is set.
    virtual PortType resolve_port_type(
        core::InternedId node_id, core::InternedId port_name) const {
        return PortType::Any;
    }

    /// Resolve the full port descriptor (direction + type) for a port.
    /// Returns nullopt for unknown nodes/ports or when no registry is set.
    virtual std::optional<bp2::PortDescriptor> resolve_port_descriptor(
        core::InternedId node_id, core::InternedId port_name) const {
        return std::nullopt;
    }

    /// Validate a potential wire between two endpoints.
    /// Returns {valid, resolved_domain}.
    struct WireValidation {
        bool valid = false;
        Domain resolved_domain = Domain::Electrical;
    };
    virtual WireValidation validate_wire(
        bp2::WireEndpoint source, bp2::WireEndpoint target) const {
        return {true, Domain::Electrical};
    }

    /// Resolve the domain for a potential wire between two endpoints.
    /// Returns Electrical as default when no registry is set.
    virtual Domain resolve_wire_domain(
        bp2::WireEndpoint source, bp2::WireEndpoint target) const {
        return Domain::Electrical;
    }

    /// Resolve the full compiled presentation spec for a node.
    /// Returns a default-constructed spec when no registry is set.
    virtual editor::presentation::CompiledPresentationSpec resolve_presentation_spec(
        core::InternedId node_id) const {
        return {};
    }

    /// Debug-only integrity check after mutations.
    /// No-op in release; asserts in debug.
    virtual void debug_validate_integrity() const {}

    /// Access the type registry (for scene rebuild).
    /// Returns nullptr when no registry is set.
    virtual const ComponentRegistry* type_registry() const { return nullptr; }
};

/// Create editing host backed by EditorModel.
/// Owned by caller; expects model to outlive the host.
/// registry/interner/arena are optional — when provided, enables type-aware
/// queries (resolve_frame_kind, validate_wire, etc.).
std::unique_ptr<EditingHost> create_editor_model_host(
    bp2::EditorModel& model,
    const ComponentRegistry* registry = nullptr,
    core::StringInterner* interner = nullptr,
    const bp2::PathArena* arena = nullptr);

/// Create editing host backed by a deeply-nested embedded inline blueprint.
/// Walks the full instance path on every access; propagates mutations back
/// up through all ancestor nodes to produce a new root Blueprint.
std::unique_ptr<EditingHost> create_pathful_embedded_host(
    bp2::EditorModel& root_model,
    std::vector<core::InternedId> instance_path,
    const ComponentRegistry* registry = nullptr,
    core::StringInterner* interner = nullptr,
    const bp2::PathArena* arena = nullptr);

/// Create a read-only host backed by a const blueprint reference.
/// All mutation operations are no-ops. Used for external-ref windows
/// that render a blueprint they do not own.
std::unique_ptr<EditingHost> create_read_only_host(const bp2::Blueprint& blueprint);
