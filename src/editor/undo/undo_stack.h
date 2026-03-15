#pragma once

#include "../commands/commands.h"
#include <vector>
#include <cstddef>

struct UndoStack {
    std::vector<Command> undo_stack;
    std::vector<Command> redo_stack;
    static constexpr size_t MAX_HISTORY = 100;
    
    void clear() {
        undo_stack.clear();
        redo_stack.clear();
    }
    
    void push(Command inverse) {
        undo_stack.push_back(std::move(inverse));
        redo_stack.clear();
        if (undo_stack.size() > MAX_HISTORY) {
            undo_stack.erase(undo_stack.begin());
        }
    }
    
    bool can_undo() const { return !undo_stack.empty(); }
    bool can_redo() const { return !redo_stack.empty(); }
    
    Command pop_undo() {
        Command cmd = std::move(undo_stack.back());
        undo_stack.pop_back();
        return cmd;
    }
    
    void push_redo(Command cmd) {
        redo_stack.push_back(std::move(cmd));
    }
    
    Command pop_redo() {
        Command cmd = std::move(redo_stack.back());
        redo_stack.pop_back();
        return cmd;
    }
};
