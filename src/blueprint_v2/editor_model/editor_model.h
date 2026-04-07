#pragma once
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/path/path.h"
#include "ui/core/interned_id.h"
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace bp2 {

class BlueprintLibrary;

struct Rect {
    float x_min = 0.0f;
    float y_min = 0.0f;
    float x_max = 0.0f;
    float y_max = 0.0f;

    bool contains(float x, float y) const {
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
    bool remove_node(ui::InternedId id);
    bool add_wire(Blueprint::Wire wire);
    bool remove_wire(ui::InternedId id);
    bool add_nested(Blueprint::Nested nested);
    bool remove_nested(ui::InternedId id);
    bool update_node(ui::InternedId id, std::function<void(Blueprint::Node&)> fn);
    bool update_wire(ui::InternedId id, std::function<void(Blueprint::Wire&)> fn);
    bool update_node_position(ui::InternedId id, float x, float y);

    // === History ===
    bool can_undo() const { return !undo_stack_.empty(); }
    bool can_redo() const { return !redo_stack_.empty(); }
    void undo();
    void redo();
    void push_checkpoint();

    size_t undo_depth() const { return undo_stack_.size(); }
    size_t redo_depth() const { return redo_stack_.size(); }

    // === Dirty tracking ===
    void mark_saved() { save_depth_ = undo_stack_.size(); }
    bool is_dirty() const { return undo_stack_.size() != save_depth_; }

    // === Checkpoint management ===
    void discard_last_checkpoint() {
        if (!undo_stack_.empty()) undo_stack_.pop_back();
    }

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
                                       ui::StringInterner const& interner) const;

    // === Direct blueprint replacement ===
    void replace_current(Blueprint bp) { current_ = std::move(bp); invalidate_indices(); }

    // === Bake/Unbake ===
    bool bake_nested(ui::InternedId id, BlueprintLibrary const& library,
                     ui::StringInterner& interner);

    // === Derived queries ===
    std::vector<ui::InternedId> nodes_in_rect(Rect const& r) const;
    bool wire_exists(Path source, Path target) const;

private:
    struct PathPairHash {
        size_t operator()(std::pair<Path, Path> const& p) const noexcept {
            size_t h = std::hash<Path>{}(p.first);
            h ^= std::hash<Path>{}(p.second) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    struct Indices {
        std::unordered_map<ui::InternedId, std::pair<float, float>> node_pos;
        std::unordered_set<std::pair<Path, Path>, PathPairHash> wire_set;
        bool valid = false;
    };

    void invalidate_indices() { indices_.valid = false; }
    void ensure_indices() const;

    Blueprint current_;
    std::vector<Blueprint> undo_stack_;
    std::vector<Blueprint> redo_stack_;
    size_t max_history_ = 100;
    size_t save_depth_ = 0;
    mutable Indices indices_;
};

// === Order-preserving blueprint replacement helpers ===
// Rebuild a blueprint replacing (or appending) a single node/wire/nested
// while preserving the insertion order of all other elements.

Blueprint replace_node_preserve_order(const Blueprint& bp, Blueprint::Node updated);
Blueprint replace_wire_preserve_order(const Blueprint& bp, Blueprint::Wire updated);
Blueprint replace_nested_preserve_order(const Blueprint& bp, Blueprint::Nested updated);

} // namespace bp2
