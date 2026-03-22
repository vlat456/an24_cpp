#pragma once
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/path/path.h"
#include "ui/core/interned_id.h"
#include <vector>
#include <memory>
#include <functional>

namespace bp2 {

// Forward declaration for registry
class TypeRegistry;

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

    // === Wire ID generation ===
    int next_wire_id_ = 0;
    std::string allocate_wire_id() {
        return "wire_" + std::to_string(next_wire_id_++);
    }

    /// Generate a unique node ID from a base name: "battery" -> "battery_1", etc.
    std::string generate_unique_node_id(std::string const& base,
                                       ui::StringInterner const& interner) const;

    // === Direct blueprint replacement ===
    void replace_current(Blueprint bp) { current_ = std::move(bp); }

    // === Bake/Unbake ===
    bool bake_nested(ui::InternedId id, TypeRegistry const& registry,
                     ui::StringInterner& interner);

private:
    Blueprint current_;
    std::vector<Blueprint> undo_stack_;
    std::vector<Blueprint> redo_stack_;
    size_t max_history_ = 100;
    size_t save_depth_ = 0;
};

} // namespace bp2
