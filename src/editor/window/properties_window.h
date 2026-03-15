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
/// On "OK", the window computes the diff between snapshot and edited values,
/// reverts the node to the snapshot, then emits CmdSetParam commands for each
/// change. This ensures all mutations go through the undo stack.
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

    /// Apply changes and close. Diffs against snapshot, emits CmdSetParam
    /// commands to the undo stack, then invokes the on_apply callback.
    void apply();

    /// Call every frame. Renders ImGui window when open.
    void render();

    // Test accessors
    const std::string& targetNodeId() const { return target_node_id_; }

private:
    bool open_ = false;
    Blueprint* bp_ = nullptr;
    UndoStack* undo_stack_ = nullptr;
    std::string target_node_id_;
    PropertyCallback on_apply_;

    // Snapshot for cancel/revert
    std::string snapshot_name_;
    std::unordered_map<std::string, std::string> snapshot_params_;

    /// Resolve the target node from the blueprint. Returns nullptr if
    /// the node no longer exists (e.g. deleted by undo while open).
    Node* resolveTarget();

    void cancelAndClose();

    /// Render a dropdown for "port_edge" param (Bus nodes)
    void renderPortEdgeParam(Node& node, const std::string& key);

    /// Render an ImGui table editor for a LUT "table" param
    void renderTableParam(Node& node, const std::string& key);
};
