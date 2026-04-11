#include "editor_model.h"
#include "blueprint_v2/library/blueprint_library.h"

namespace bp2 {

Blueprint replace_node_preserve_order(const Blueprint& bp, Blueprint::Node updated) {
    Blueprint rebuilt = clone_metadata(bp);

    bool replaced = false;
    for (const auto& n : bp.nodes()) {
        if (n.semantic.id == updated.semantic.id) {
            rebuilt = rebuilt.with_node(std::move(updated));
            replaced = true;
        } else {
            rebuilt = rebuilt.with_node(n);
        }
    }
    if (!replaced) {
        rebuilt = rebuilt.with_node(std::move(updated));
    }

    for (const auto& w : bp.wires()) {
        rebuilt = rebuilt.with_wire(w);
    }

    return rebuilt;
}

Blueprint replace_wire_preserve_order(const Blueprint& bp, Blueprint::Wire updated) {
    Blueprint rebuilt = clone_metadata(bp);

    for (const auto& n : bp.nodes()) {
        rebuilt = rebuilt.with_node(n);
    }

    bool replaced = false;
    for (const auto& w : bp.wires()) {
        if (w.id == updated.id) {
            rebuilt = rebuilt.with_wire(std::move(updated));
            replaced = true;
        } else {
            rebuilt = rebuilt.with_wire(w);
        }
    }
    if (!replaced) {
        rebuilt = rebuilt.with_wire(std::move(updated));
    }

    return rebuilt;
}

EditorModel::EditorModel(Blueprint initial)
    : current_(std::move(initial)) {}  // Issue #91: No canonicalization of blueprint-instance iface

void EditorModel::push_checkpoint_if_enabled() {
    if (checkpoint_suppression_depth_ == 0) {
        push_checkpoint();
    }
}

bool EditorModel::mutate_atomically(const std::function<void()>& fn) {
    const bool is_outermost = (checkpoint_suppression_depth_ == 0);

    const Blueprint before = current_;
    const size_t undo_before = undo_stack_.size();
    const size_t redo_before = redo_stack_.size();

    if (is_outermost) {
        push_checkpoint();
    }
    ++checkpoint_suppression_depth_;
    try {
        fn();
        --checkpoint_suppression_depth_;
    } catch (...) {
        --checkpoint_suppression_depth_;
        if (is_outermost) {
            current_ = before;
            undo_stack_.resize(undo_before);
            redo_stack_.resize(redo_before);
            invalidate_indices();
        }
        throw;
    }

    if (is_outermost && current_ == before) {
        undo_stack_.resize(undo_before);
        redo_stack_.resize(redo_before);
        invalidate_indices();
        return false;
    }

    invalidate_indices();
    return is_outermost ? true : (current_ != before);
}

bool EditorModel::add_node(Blueprint::Node node) {
    if (current_.find_node(node.semantic.id)) return false;
    push_checkpoint_if_enabled();
    // Issue #91: No canonicalization of blueprint-instance iface. Add node as-is.
    current_ = current_.with_node(std::move(node));
    invalidate_indices();
    return true;
}

bool EditorModel::remove_node(ui::InternedId id) {
    if (!current_.find_node(id)) return false;
    push_checkpoint_if_enabled();
    current_ = current_.without_node(id);
    invalidate_indices();
    return true;
}

bool EditorModel::add_wire(Blueprint::Wire wire) {
    if (current_.find_wire(wire.id)) return false;
    if (wire.source == wire.target) return false;
    push_checkpoint_if_enabled();
    current_ = current_.with_wire(std::move(wire));
    invalidate_indices();
    return true;
}

bool EditorModel::remove_wire(ui::InternedId id) {
    if (!current_.find_wire(id)) return false;
    push_checkpoint_if_enabled();
    current_ = current_.without_wire(id);
    invalidate_indices();
    return true;
}

bool EditorModel::update_node(ui::InternedId id, std::function<void(Blueprint::Node&)> fn) {
    auto const* existing = current_.find_node(id);
    if (!existing) return false;
    Blueprint::Node updated = *existing;
    fn(updated);
    // Issue #91: No canonicalization of blueprint-instance iface. Update node as-is.
    push_checkpoint_if_enabled();
    current_ = replace_node_preserve_order(current_, std::move(updated));
    invalidate_indices();
    return true;
}

bool EditorModel::update_wire(ui::InternedId id, std::function<void(Blueprint::Wire&)> fn) {
    auto const* existing = current_.find_wire(id);
    if (!existing) return false;
    Blueprint::Wire updated = *existing;
    fn(updated);
    push_checkpoint_if_enabled();
    current_ = replace_wire_preserve_order(current_, std::move(updated));
    invalidate_indices();
    return true;
}

bool EditorModel::update_node_position(ui::InternedId id, float x, float y) {
    return update_node(id, [x, y](Blueprint::Node& n) {
        n.layout.x = x;
        n.layout.y = y;
    });
}

void EditorModel::push_checkpoint() {
    redo_stack_.clear();
    if (undo_stack_.size() >= max_history_) {
        undo_stack_.erase(undo_stack_.begin());
        // If the saved state was at position 0, it's been evicted.
        // Mark dirty permanently by using a sentinel value.
        if (save_depth_ == 0) {
            save_depth_ = SIZE_MAX;
        } else if (save_depth_ != SIZE_MAX) {
            --save_depth_;
        }
    }
    undo_stack_.push_back(current_);
}

void EditorModel::undo() {
    if (!can_undo()) return;
    redo_stack_.push_back(std::move(current_));
    current_ = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    invalidate_indices();
}

void EditorModel::redo() {
    if (!can_redo()) return;
    undo_stack_.push_back(std::move(current_));
    current_ = std::move(redo_stack_.back());
    redo_stack_.pop_back();
     invalidate_indices();
}

bool EditorModel::bake_blueprint_instance(ui::InternedId node_id,
                                        BlueprintLibrary const& library,
                                        ui::StringInterner& interner) {
    (void)interner;
    (void)library;
    auto const* node = current_.find_node(node_id);
    if (!node) return false;
    if (!node->is_blueprint_instance()) return false;
    if (!node->source || !node->source->is_reference()) return false;

    // TODO: Implement reference-to-embedded conversion for blueprint_instance nodes.
    // This will involve looking up the referenced blueprint and updating node.source.
    return false;
}

void EditorModel::ensure_indices() const {
    if (indices_.valid) return;

    indices_.node_pos.clear();
    indices_.wire_set.clear();

    indices_.node_pos.reserve(current_.nodes().size());
    for (auto const& n : current_.nodes()) {
        indices_.node_pos[n.semantic.id] = {n.layout.x, n.layout.y};
    }

    indices_.wire_set.reserve(current_.wires().size());
    for (auto const& w : current_.wires()) {
        indices_.wire_set.insert({w.source, w.target});
    }

    indices_.valid = true;
}

std::vector<ui::InternedId> EditorModel::nodes_in_rect(Rect const& r) const {
    ensure_indices();
    std::vector<ui::InternedId> out;
    for (auto const& kv : indices_.node_pos) {
        if (r.contains(kv.second.first, kv.second.second)) {
            out.push_back(kv.first);
        }
    }
    return out;
}

bool EditorModel::wire_exists(WireEndpoint const& source, WireEndpoint const& target) const {
    ensure_indices();
    return indices_.wire_set.find({source, target}) != indices_.wire_set.end();
}

std::string EditorModel::generate_unique_node_id(
        std::string const& base, ui::StringInterner const& interner) const {
    std::string lower = base;
    for (auto& c : lower)
        c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    for (int counter = 1; ; ++counter) {
        std::string candidate = lower + "_" + std::to_string(counter);
        auto existing = interner.lookup(candidate);
        if (existing.empty()) return candidate;
        if (current_.find_node(existing) == nullptr) return candidate;
    }
}

} // namespace bp2
