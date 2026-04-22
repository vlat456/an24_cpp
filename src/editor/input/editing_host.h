#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "ui/core/interned_id.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bp2 {
class EditorModel;
class BlueprintLibrary;
}

namespace ui {
class StringInterner;
}

/// Narrow abstraction for CanvasInput's editing operations.
/// Covers only the mutation and query surface CanvasInput actually uses.
/// No fallback, no backward-compat layering.
class EditingHost {
public:
    virtual ~EditingHost() = default;

    /// Read the current blueprint (non-mutable).
    virtual const bp2::Blueprint& current_blueprint() const = 0;

    /// Look up a node in the current blueprint.
    virtual const bp2::Blueprint::Node* find_node(ui::InternedId id) const = 0;

    /// Look up a wire in the current blueprint.
    virtual const bp2::Blueprint::Wire* find_wire(ui::InternedId id) const = 0;

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
    virtual bool remove_wire(ui::InternedId id) = 0;

    /// Update a wire by ID with a mutation function.
    /// Returns true if successfully updated.
    virtual bool update_wire(ui::InternedId id,
                             std::function<void(bp2::Blueprint::Wire&)> fn) = 0;

    /// Update a node's position.
    /// Returns true if successfully updated.
    virtual bool update_node_position(ui::InternedId id, float x, float y) = 0;

    /// Update a node by ID with a mutation function.
    /// Returns true if successfully updated.
    virtual bool update_node(ui::InternedId id,
                             std::function<void(bp2::Blueprint::Node&)> fn) = 0;

    /// Remove a node and any explicitly provided connected wires.
    virtual bool remove_node(ui::InternedId id,
                             std::vector<ui::InternedId> connected_wire_ids) = 0;

    /// Bake a referenced blueprint-instance node into an embedded source inside
    /// the current scoped blueprint.
    virtual bool bake_blueprint_instance(ui::InternedId id,
                                         const bp2::BlueprintLibrary& library) = 0;

    /// Allocate a unique wire ID.
    virtual std::string allocate_wire_id() = 0;
};

/// Create editing host backed by EditorModel.
/// Owned by caller; expects model to outlive the host.
std::unique_ptr<EditingHost> create_editor_model_host(bp2::EditorModel& model);

/// Create editing host backed by an embedded nested inline blueprint.
/// Mutations are written through the authoritative root EditorModel.
std::unique_ptr<EditingHost> create_embedded_inline_host(bp2::EditorModel& root_model,
                                                         ui::InternedId nested_id);

/// Create editing host backed by a deeply-nested embedded inline blueprint.
/// Walks the full instance path on every access; propagates mutations back
/// up through all ancestor nodes to produce a new root Blueprint.
std::unique_ptr<EditingHost> create_pathful_embedded_host(
    bp2::EditorModel& root_model,
    std::vector<ui::InternedId> instance_path);

/// Create a read-only host backed by a const blueprint reference.
/// All mutation operations are no-ops. Used for external-ref windows
/// that render a blueprint they do not own.
std::unique_ptr<EditingHost> create_read_only_host(const bp2::Blueprint& blueprint);
