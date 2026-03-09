#pragma once

#include "data/node.h"
#include <functional>
#include <string>
#include <unordered_map>

/// Callback when properties are applied: receives the node ID
using PropertyCallback = std::function<void(const std::string& node_id)>;

/// Modal properties window for editing Node::params via ImGui.
/// Lifecycle: open(node, callback) → render() each frame → OK or Cancel.
class PropertiesWindow {
public:
    void open(Node& node, PropertyCallback on_apply);
    void close();
    bool isOpen() const { return open_; }

    /// Call every frame. Renders ImGui window when open.
    void render();

    // Test accessors
    const std::string& targetNodeId() const { return target_node_id_; }

private:
    bool open_ = false;
    Node* target_ = nullptr;
    std::string target_node_id_;
    PropertyCallback on_apply_;

    // Snapshot for cancel/revert
    std::string snapshot_name_;
    std::unordered_map<std::string, std::string> snapshot_params_;

    void applyAndClose();
    void cancelAndClose();

    /// Render an ImGui table editor for a LUT "table" param
    void renderTableParam(const std::string& key);
};
