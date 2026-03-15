#pragma once

#include "data/node.h"
#include "commands/commands.h"
#include "undo/undo_stack.h"
#include <functional>
#include <string>
#include <unordered_map>

/// Callback when properties are applied: receives the node ID
using PropertyCallback = std::function<void(const std::string& node_id)>;

/// Modal properties window for editing Node::params via ImGui.
/// Lifecycle: open(node, callback) → render() each frame → OK or Cancel.
///
/// Shadow-editing model: the window edits a local copy of the node's params
/// and name (pending_params_, pending_name_). The live Blueprint node is
/// NEVER mutated until the user clicks "OK" / apply(). On "Cancel", the
/// pending state is simply discarded.
///
/// On "OK", the window diffs pending vs snapshot, then emits CmdSetParam /
/// CmdSetName commands via the undo stack for each change.
///
/// Safety: The window stores only the node ID, never a raw Node*.
/// The node pointer is resolved fresh from the Blueprint each frame via
/// resolveTarget(). This is safe across undo/redo which replaces the
/// entire Blueprint contents.
class PropertiesWindow {
public:
    void open(Node& node, const std::string& node_id_str,
              Blueprint& bp, UndoStack& undo_stack, PropertyCallback on_apply);
    void close();
    bool isOpen() const { return open_; }

    /// Apply changes and close. Diffs pending state against snapshot, emits
    /// CmdSetParam commands to the undo stack, then invokes the on_apply callback.
    void apply();

    /// Call every frame. Renders ImGui window when open.
    void render();

    // Test accessors
    const std::string& targetNodeId() const { return target_node_id_; }

    /// Set a pending param value (for testing without ImGui).
    void setPendingParam(const std::string& key, const std::string& value) {
        pending_params_[key] = value;
    }

    /// Set the pending name (for testing without ImGui).
    void setPendingName(const std::string& name) {
        pending_name_ = name;
    }

    /// Read pending param value (for testing / display).
    const std::unordered_map<std::string, std::string>& pendingParams() const {
        return pending_params_;
    }

    /// Read pending name (for testing / display).
    const std::string& pendingName() const { return pending_name_; }

private:
    bool open_ = false;
    Blueprint* bp_ = nullptr;
    UndoStack* undo_stack_ = nullptr;
    std::string target_node_id_;
    PropertyCallback on_apply_;

    // Shadow copies: edited by the UI, never touching the live node until apply().
    std::string pending_name_;
    std::unordered_map<std::string, std::string> pending_params_;

    // Snapshot of the node state at open() time — used for diffing at apply().
    std::string snapshot_name_;
    std::unordered_map<std::string, std::string> snapshot_params_;

    /// Resolve the target node from the blueprint. Returns nullptr if
    /// the node no longer exists (e.g. deleted by undo while open).
    Node* resolveTarget();

    void cancelAndClose();

    /// Render a dropdown for "port_edge" param (Bus nodes)
    void renderPortEdgeParam(const std::string& key);

    /// Render an ImGui table editor for a LUT "table" param
    void renderTableParam(const std::string& key);
};
