#pragma once

#include "data/blueprint.h"
#include <vector>
#include <cstddef>
#include <optional>

// =============================================================================
// Snapshot-based UndoStack
// =============================================================================
//
// Stores full Blueprint copies. Before any mutation, call snapshot() to save
// the current state. Undo/Redo swap the live Blueprint with stored copies.
//
// Typical usage:
//   undo_stack.snapshot(blueprint);   // save current state
//   /* ... mutate blueprint ... */    // do whatever you want
//   // To undo:
//   undo_stack.undo(blueprint);       // restores previous state
//   // To redo:
//   undo_stack.redo(blueprint);       // re-applies the mutation

struct UndoStack {
    static constexpr size_t MAX_HISTORY = 100;

    void clear() {
        undo_stack_.clear();
        redo_stack_.clear();
        save_point_.reset();
        dirty_ = false;
    }

    /// Take a snapshot of the current blueprint state before a mutation.
    /// Clears the redo stack (new action invalidates redo history).
    void snapshot(const Blueprint& bp) {
        undo_stack_.push_back(bp);  // deep copy
        redo_stack_.clear();
        if (undo_stack_.size() > MAX_HISTORY) {
            // If save point was at index 0, it's being evicted -- mark permanently dirty
            if (save_point_.has_value() && *save_point_ == 0) {
                save_point_.reset();
            } else if (save_point_.has_value()) {
                --(*save_point_);
            }
            undo_stack_.erase(undo_stack_.begin());
        }
        update_dirty();
    }

    /// Undo: restore the blueprint to the previous state.
    /// The current state is pushed onto the redo stack.
    /// Returns true if undo was performed.
    bool undo(Blueprint& bp) {
        if (undo_stack_.empty()) return false;

        redo_stack_.push_back(std::move(bp));   // save current for redo
        bp = std::move(undo_stack_.back());     // restore previous
        undo_stack_.pop_back();

        // Rebuild indices (snapshot doesn't store derived indices)
        rebuild_indices(bp);
        update_dirty();
        return true;
    }

    /// Redo: re-apply the previously undone state.
    /// The current state is pushed back onto the undo stack.
    /// Returns true if redo was performed.
    bool redo(Blueprint& bp) {
        if (redo_stack_.empty()) return false;

        undo_stack_.push_back(std::move(bp));   // save current for undo
        bp = std::move(redo_stack_.back());     // restore redo state
        redo_stack_.pop_back();

        rebuild_indices(bp);
        update_dirty();
        return true;
    }

    /// Discard the most recent snapshot without pushing it onto the redo stack.
    /// Use this when a drag/resize resulted in no actual change to avoid
    /// polluting the undo/redo stacks with no-op entries.
    void discard_last_snapshot() {
        if (undo_stack_.empty()) return;
        undo_stack_.pop_back();
        update_dirty();
    }

    /// Restore the most recent snapshot into `bp` and remove it from the
    /// undo stack, WITHOUT pushing the current state onto the redo stack.
    /// Use this to silently revert mutations that should not leave a redo
    /// entry (e.g. TransactionGuard::discard()).
    bool restore_last_snapshot(Blueprint& bp) {
        if (undo_stack_.empty()) return false;
        bp = std::move(undo_stack_.back());
        undo_stack_.pop_back();
        rebuild_indices(bp);
        update_dirty();
        return true;
    }

    bool can_undo() const { return !undo_stack_.empty(); }
    bool can_redo() const { return !redo_stack_.empty(); }

    // ── Dirty flag / save-point tracking ──

    /// Mark the current state as "saved". Future dirty() checks compare against this point.
    void mark_saved() {
        save_point_ = undo_stack_.size();
        dirty_ = false;
    }

    /// Returns true if state has changed since last mark_saved().
    bool is_dirty() const { return dirty_; }

private:
    std::vector<Blueprint> undo_stack_;
    std::vector<Blueprint> redo_stack_;
    std::optional<size_t> save_point_ = 0;  // Index into undo_stack_ representing "clean" state
    bool dirty_ = false;

    void update_dirty() {
        if (!save_point_.has_value()) {
            dirty_ = true;  // Save point was evicted, permanently dirty until next save
        } else {
            dirty_ = (undo_stack_.size() != *save_point_);
        }
    }

    /// Rebuild all derived indices after restoring a snapshot.
    static void rebuild_indices(Blueprint& bp) {
        bp.rebuild_node_index();
        bp.rebuild_wire_index();
        bp.rebuild_wire_id_index();
        bp.rebuild_bus_wire_index();
        bp.rebuild_port_occupancy_index();
    }
};
