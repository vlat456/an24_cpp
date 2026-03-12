#pragma once

#include "visual/inspector/display_tree.h"
#include "visual/scene/scene.h"
#include <string>
#include <vector>

/// Inspector - renders component tree with port connections (READ-ONLY).
/// Uses ImGui native widgets (not IDrawList/canvas rendering).
///
/// Data model (buildDisplayTree) is separated from rendering (render) at the
/// file level: inspector_core.cpp has no ImGui dependency so tests don't need
/// to link ImGui.
///
/// Dirty tracking: rebuilds display tree only when scene topology or search
/// changes, avoiding per-frame string comparisons.
class Inspector {
public:
    enum class SortMode { Name, Type, Connections };

    /// Default constructor (scene can be set later via setScene)
    Inspector() : scene_(nullptr) {}

    /// Constructor with scene pointer
    Inspector(const VisualScene* scene);

    /// Set the scene to inspect (for switching between documents)
    void setScene(const VisualScene& scene) {
        scene_ = &scene;
        markDirty();
    }

    /// Mark data stale (call after structural scene changes)
    void markDirty() { dirty_ = true; }

    /// Render inspector widget (ImGui::Begin/End handled by caller)
    void render();

    /// Consume the node ID the user clicked in the inspector (empty if none).
    /// Resets after read (single-shot output).
    std::string consumeSelection();

    /// Set search filter (marks dirty if changed)
    void setSearch(std::string_view search);

    /// Set sort mode (marks dirty if changed)
    void setSortMode(SortMode mode);

    /// Read-only access to the cached display tree (tests + render)
    [[nodiscard]] const std::vector<DisplayNode>& displayTree() const { return display_tree_; }

    /// Force-rebuild display tree (public for testing; render() calls this lazily)
    void buildDisplayTree();

private:
    const VisualScene* scene_;
    std::vector<DisplayNode> display_tree_;

    // Dirty tracking
    bool dirty_ = true;
    size_t last_node_count_ = 0;
    size_t last_wire_count_ = 0;

    /// Check scene topology and mark dirty if changed. Returns true if rebuild needed.
    bool detectSceneChange();

    // Search / sort state
    SortMode sort_mode_ = SortMode::Name;
    std::string search_;
    std::string search_lower_;  // precomputed lowercase of search_

    // Data model helpers (inspector_core.cpp)
    std::string findConnectionFor(const Node& node, const EditorPort& port, PortSide side) const;
    void sortDisplayTree();
    bool passesFilter(const Node& node) const;

    // Selection output (consumed by main loop)
    std::string clicked_node_id_;
};
