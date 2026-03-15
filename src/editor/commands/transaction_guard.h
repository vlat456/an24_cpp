#pragma once

#include "commands.h"
#include "../undo/undo_stack.h"
#include <cassert>

// =============================================================================
// TransactionGuard — RAII scope for grouping commands into a single undo entry
// =============================================================================
//
// Usage:
//   {
//       TransactionGuard txn(blueprint, undo_stack);
//       txn.execute(cmd_move_node(id1, pos1));
//       txn.execute(cmd_move_node(id2, pos2));
//   }  // ~TransactionGuard commits (snapshot already taken)
//
// Takes a single snapshot before the first mutation. All commands within
// the guard are applied directly to the blueprint. On destruction, the
// snapshot remains on the undo stack as a single undo entry.
//
// If discard() is called, the snapshot is popped and the mutations are
// reverted (the blueprint is restored to its pre-transaction state).
//
// If no commands are executed, the snapshot is discarded (no undo entry).
//
// The guard is non-copyable and non-movable to prevent misuse.

class TransactionGuard {
public:
    TransactionGuard(Blueprint& bp, UndoStack& stack)
        : bp_(bp), stack_(stack) {}

    ~TransactionGuard() {
        commit();
    }

    /// Non-copyable, non-movable
    TransactionGuard(const TransactionGuard&) = delete;
    TransactionGuard& operator=(const TransactionGuard&) = delete;
    TransactionGuard(TransactionGuard&&) = delete;
    TransactionGuard& operator=(TransactionGuard&&) = delete;

    /// Execute a command within this transaction.
    /// The first call takes a snapshot; subsequent calls just mutate.
    void execute(Command cmd) {
        assert(!committed_ && "Cannot execute after commit/discard");
        if (!snapshot_taken_) {
            stack_.snapshot(bp_);
            snapshot_taken_ = true;
        }
        ::execute(bp_, cmd);
        ++cmd_count_;
    }

    /// Manually commit (also called by destructor). Idempotent.
    /// If no commands were executed, discards the snapshot.
    void commit() {
        if (committed_) return;
        committed_ = true;

        if (snapshot_taken_ && cmd_count_ == 0) {
            // No commands executed — discard the snapshot cleanly
            // (discard_last_snapshot does not pollute the redo stack)
            stack_.discard_last_snapshot();
        }
    }

    /// Discard: revert all mutations by undoing the snapshot.
    /// The blueprint is restored to its pre-transaction state.
    void discard() {
        if (committed_) return;
        committed_ = true;

        if (snapshot_taken_) {
            // Restore bp from snapshot without pushing onto redo stack
            stack_.restore_last_snapshot(bp_);
        }
    }

    /// Number of commands executed so far.
    size_t size() const { return cmd_count_; }
    bool empty() const { return cmd_count_ == 0; }

private:
    Blueprint& bp_;
    UndoStack& stack_;
    size_t cmd_count_ = 0;
    bool snapshot_taken_ = false;
    bool committed_ = false;
};
