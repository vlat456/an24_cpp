#pragma once

#include "data/node.h"
#include "data/port.h"
#include "visual/scene/scene.h"
#include <string>
#include <vector>

/// Cached port info for display
struct DisplayPort {
    std::string name;
    PortSide side;
    std::string connection;  // "Battery.v_out" or "[not connected]"
};

/// Cached node info for display
struct DisplayNode {
    std::string name;
    std::string type_name;
    size_t connection_count = 0;
    std::vector<DisplayPort> ports;
};

/// Inspector - renders component tree with port connections (READ-ONLY)
/// Uses ImGui native widgets (not IDrawList/canvas rendering)
/// Simple display of blueprint state: nodes, ports, and their connections
///
/// Dirty tracking: Only rebuilds display tree when scene changes or search updates
/// to avoid per-frame string comparisons (expensive without string interning)
class Inspector {
public:
    /// Sort mode for display tree
    enum class SortMode { Name, Type, Connections };

    explicit Inspector(VisualScene& scene);

    /// Mark scene as dirty (call after adding/removing nodes or wires)
    void markDirty() { dirty_ = true; }

    /// Render inspector window (ImGui::Begin/End handled by caller)
    void render();

    /// Set search filter (marks dirty)
    void setSearch(const char* search);

    /// Set sort mode (marks dirty)
    void setSortMode(SortMode mode) {
        if (sort_mode_ != mode) {
            sort_mode_ = mode;
            dirty_ = true;
        }
    }

    /// Get current display tree (for tests, ImGui renders this)
    const std::vector<DisplayNode>& displayTree() const { return display_tree_; }

    /// Build display tree (public for testing, usually called by render())
    void buildDisplayTree();

private:
    VisualScene& scene_;
    std::vector<DisplayNode> display_tree_;
    bool dirty_;

    // Find connection string for a port (cached during build)
    std::string findConnectionFor(const Node& node, const Port& port, PortSide side);

    // Sort cached display tree
    void sortDisplayTree();

    // Filter nodes by search text
    bool passesFilter(const Node& node) const;

    // UI state
    SortMode sort_mode_ = SortMode::Name;
    char search_buffer_[128];

    // Scene change detection (for dirty tracking)
    size_t last_node_count_ = 0;
    size_t last_wire_count_ = 0;
};
