#pragma once

#include "visual/inspector/display_tree.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/interface/port_descriptor.h"
#include "blueprint_v2/path/path.h"
#include "core/strings/interned_id.h"
#include "window/window_scope_id.h"

struct ComponentRegistry;
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

    /// Default constructor (blueprint can be set later via setBlueprint)
    Inspector() = default;

    /// Constructor with blueprint pointer and group filter
    Inspector(const bp2::Blueprint* bp, const bp2::PathArena* arena,
              core::StringInterner* interner, const WindowScopeId& scope_id = WindowScopeId::root(),
              const ComponentRegistry* registry = nullptr);

    /// Set the blueprint to inspect (for switching between documents)
    void setBlueprint(const bp2::Blueprint& bp, const bp2::PathArena& arena,
                      core::StringInterner& interner,
                      const WindowScopeId& scope_id = WindowScopeId::root(),
                      const ComponentRegistry* registry = nullptr) {
        bp_ = &bp;
        arena_ = &arena;
        interner_ = &interner;
        scope_id_ = scope_id;
        registry_ = registry;
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
    const bp2::Blueprint*    bp_       = nullptr;
    const bp2::PathArena*    arena_    = nullptr;
    core::StringInterner* interner_ = nullptr;
    const ComponentRegistry* registry_ = nullptr;
    WindowScopeId scope_id_            = WindowScopeId::root();
    std::vector<DisplayNode> display_tree_;

    // Dirty tracking
    bool dirty_ = true;
    size_t last_node_count_ = 0;
    size_t last_wire_count_ = 0;

    /// Check whether a node belongs to this inspector's blueprint (implicit - all nodes in bp_ belong to this scope)
    bool ownsNode(const bp2::Blueprint::Node& /* n */) const { return true; }
    /// Check whether a wire belongs to this inspector's group (both endpoints)
    bool ownsWire(const bp2::Blueprint::Wire& w) const;

    /// Decode a Port path: extract node_id and port_name.
    /// Returns {node_id, port_name} or empty InternedIds on failure.
    std::pair<core::InternedId, core::InternedId> decode_port_path(bp2::Path p) const;
    std::pair<core::InternedId, core::InternedId> decode_port_path(bp2::WireEndpoint const& ep) const;

    /// Check scene topology and mark dirty if changed. Returns true if rebuild needed.
    bool detectSceneChange();

    // Search / sort state
    SortMode sort_mode_ = SortMode::Name;
    std::string search_;
    std::string search_lower_;  // precomputed lowercase of search_

    /// Pre-decoded wire endpoints (built once per display tree rebuild).
    struct DecodedWire {
        core::InternedId src_node, src_port, tgt_node, tgt_port;
    };

    // Data model helpers (inspector_core.cpp)
    std::string findConnectionFor(const bp2::Blueprint::Node& node,
                                  const bp2::PortDescriptor& port, bp2::Direction direction,
                                  const std::vector<DecodedWire>& wires) const;
    void sortDisplayTree();
    bool passesFilter(const bp2::Blueprint::Node& node) const;

    // Selection output (consumed by main loop)
    std::string clicked_node_id_;
};
