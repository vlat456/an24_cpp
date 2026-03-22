#pragma once

#include "commands.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "ui/core/interned_id.h"
#include <cassert>

class TransactionGuard {
public:
    TransactionGuard(bp2::EditorModel& model, ui::StringInterner& interner)
        : model_(model), interner_(interner) {}

    ~TransactionGuard() { commit(); }

    TransactionGuard(const TransactionGuard&) = delete;
    TransactionGuard& operator=(const TransactionGuard&) = delete;
    TransactionGuard(TransactionGuard&&) = delete;
    TransactionGuard& operator=(TransactionGuard&&) = delete;

    void execute(Command cmd) {
        assert(!committed_);
        if (!snapshot_taken_) {
            model_.push_checkpoint();
            snapshot_taken_ = true;
        }
        ::execute(model_, interner_, std::move(cmd));
        ++cmd_count_;
    }

    void commit() {
        if (committed_) return;
        committed_ = true;
        if (snapshot_taken_ && cmd_count_ == 0)
            model_.discard_last_checkpoint();
    }

    void discard() {
        if (committed_) return;
        committed_ = true;
        if (snapshot_taken_) model_.undo();
    }

    size_t size() const { return cmd_count_; }
    bool empty() const { return cmd_count_ == 0; }

private:
    bp2::EditorModel& model_;
    ui::StringInterner& interner_;
    size_t cmd_count_ = 0;
    bool snapshot_taken_ = false;
    bool committed_ = false;
};
