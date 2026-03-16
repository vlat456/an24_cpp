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
/// resolve_target(). This is safe across undo/redo which replaces the
/// entire Blueprint contents.
class PropertiesWindow {
public:
    void open(Node& node, const std::string& node_id_str,
              Blueprint& bp, UndoStack& undo_stack, PropertyCallback on_apply);
    void close();
    bool is_open() const { return open_; }

    /// Apply changes and close. Diffs pending state against snapshot, emits
    /// CmdSetParam commands to the undo stack, then invokes the on_apply callback.
    void apply();

    /// Call every frame. Renders ImGui window when open.
    void render();

    // Test accessors
    const std::string& target_node_id_str() const { return target_node_id_; }

    /// Set a pending param value (for testing without ImGui).
    void set_pending_param(const std::string& key, const std::string& value) {
        pending_params_[key] = value;
    }

    /// Set the pending name (for testing without ImGui).
    void set_pending_name(const std::string& name) {
        pending_name_ = name;
    }

    /// Set pending layout overrides (for testing without ImGui).
    void set_pending_layout_overrides(const std::vector<PortLayoutOverride>& overrides) {
        pending_layout_overrides_ = overrides;
    }

    /// Read pending layout overrides (for testing).
    const std::vector<PortLayoutOverride>& pending_layout_overrides() const {
        return pending_layout_overrides_;
    }

    /// Read pending param value (for testing / display).
    const std::unordered_map<std::string, std::string>& pending_params() const {
        return pending_params_;
    }

    /// Read pending name (for testing / display).
    const std::string& pending_name() const { return pending_name_; }

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
    std::vector<PortLayoutOverride> pending_layout_overrides_;
    std::vector<PortLayoutOverride> snapshot_layout_overrides_;

    /// Resolve the target node from the blueprint. Returns nullptr if
    /// the node no longer exists (e.g. deleted by undo while open).
    Node* resolve_target();

    void cancel_and_close();

    /// Render a dropdown for "port_edge" param (Bus nodes)
    void render_port_edge_param(const std::string& key);

    /// Render an ImGui table editor for a LUT "table" param
    void render_table_param(const std::string& key);

    /// Render a multiline text editor for "text" param (Text nodes)
    void render_text_param(const std::string& key);

    /// Render a dropdown for "font_size" param (Small / Medium / Large)
    void render_font_size_param(const std::string& key);

    /// Render a generic single-line InputText for a param
    void render_generic_param(const std::string& key);

    /// Render port layout override section (side/position for each port)
    void render_port_layout_section(const Node& node);

    /// Render a single row in the port layout table
    void render_port_layout_row(const std::string& port_name);
};
