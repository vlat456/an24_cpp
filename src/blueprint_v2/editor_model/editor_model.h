#pragma once
#include "blueprint_v2/blueprint/blueprint_replace.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/blueprint/canonicalize.h"
#include "blueprint_v2/path/path.h"
#include "core/strings/interned_id.h"
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include <span>

namespace bp2 {

class BlueprintLibrary;

struct Rect {
    float x_min = 0.0f;
    float y_min = 0.0f;
    float x_max = 0.0f;
    float y_max = 0.0f;

    [[nodiscard]] bool contains(float x, float y) const {
        return x >= x_min && x <= x_max && y >= y_min && y <= y_max;
    }
};

class EditorModel {
public:
    EditorModel() = default;
    explicit EditorModel(Blueprint initial);

    Blueprint const& current() const { return current_; }

    // === Commands (return true if changed) ===
    bool add_node(Blueprint::Node node);
    bool remove_node(core::InternedId id);
    bool add_wire(Blueprint::Wire wire);
    bool remove_wire(core::InternedId id);
    bool update_node(core::InternedId id, std::function<void(Blueprint::Node&)> fn);
    bool update_wire(core::InternedId id, std::function<void(Blueprint::Wire&)> fn);
    bool update_node_position(core::InternedId id, float x, float y);

    // === Embedded-scope commands (handle checkpoint + propagation) ===

    /// Apply a generic mutation to the embedded blueprint at the given path.
    /// Handles undo checkpoint and root replacement automatically.
    /// Returns NotFound if the path cannot be resolved, NoChange if the
    /// mutation produced no changes, Changed if the root was updated.
    MutationResult mutate_embedded(
        std::span<const core::InternedId> path,
        const std::function<Blueprint(const Blueprint&)>& mutation);

    /// Update a single node inside an embedded blueprint identified by path.
    /// Convenience wrapper over mutate_embedded(). Returns true if changed.
    bool update_embedded_node(
        std::span<const core::InternedId> path,
        core::InternedId node_id,
        const std::function<void(Blueprint::Node&)>& fn);

    // === History ===
    bool can_undo() const { return !undo_stack_.empty(); }
    bool can_redo() const { return !redo_stack_.empty(); }
    void undo();
    void redo();
    void push_checkpoint();
    bool mutate_atomically(const std::function<void()>& fn);

    size_t undo_depth() const { return undo_stack_.size(); }
    size_t redo_depth() const { return redo_stack_.size(); }

    // === Dirty tracking ===
    void mark_saved() { save_depth_ = undo_stack_.size(); }
    bool is_dirty() const { return undo_stack_.size() != save_depth_; }

    /// Clear undo/redo history (useful after loading a file or for test setup).
    void clear_history() {
        undo_stack_.clear();
        redo_stack_.clear();
        save_depth_ = 0;
    }

    // === Wire ID generation ===
    int next_wire_id_ = 0;
    std::string allocate_wire_id() {
        return "wire_" + std::to_string(next_wire_id_++);
    }

    /// Generate a unique node ID from a base name: "battery" -> "battery_1", etc.
    std::string generate_unique_node_id(std::string const& base,
                                       core::StringInterner const& interner) const;

    // === Direct blueprint replacement ===
    void replace_current(Blueprint bp) {
        // Issue #91: No canonicalization of blueprint-instance iface. Use blueprint as-is.
        current_ = std::move(bp);
        invalidate_indices();
    }

    // === Derived queries ===
    std::vector<core::InternedId> nodes_in_rect(Rect const& r) const;
    bool wire_exists(WireEndpoint const& source, WireEndpoint const& target) const;

private:
    struct EndpointPairHash {
        size_t operator()(std::pair<WireEndpoint, WireEndpoint> const& p) const noexcept {
            size_t h = std::hash<WireEndpoint>{}(p.first);
            h ^= std::hash<WireEndpoint>{}(p.second) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    struct Indices {
        std::unordered_map<core::InternedId, std::pair<float, float>> node_pos;
        std::unordered_set<std::pair<WireEndpoint, WireEndpoint>, EndpointPairHash> wire_set;
        bool valid = false;
    };

    void invalidate_indices() const { indices_.valid = false; }
    void ensure_indices() const;
    void push_checkpoint_if_enabled();

    Blueprint current_;
    std::vector<Blueprint> undo_stack_;
    std::vector<Blueprint> redo_stack_;
    size_t max_history_ = 100;
    size_t save_depth_ = 0;
    mutable Indices indices_;
    int checkpoint_suppression_depth_ = 0;
};

} // namespace bp2
