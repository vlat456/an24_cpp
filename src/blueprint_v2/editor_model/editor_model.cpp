#include "editor_model.h"
#include "blueprint_v2/library/blueprint_library.h"

namespace bp2 {

namespace {

Blueprint clone_metadata(const Blueprint& bp) {
    Blueprint rebuilt;
    rebuilt = rebuilt.with_id(bp.id());
    rebuilt = rebuilt.with_display_name(bp.display_name());
    rebuilt = rebuilt.with_interface(bp.iface());
    rebuilt = rebuilt.with_viewport(bp.pan_x(), bp.pan_y(), bp.zoom(), bp.grid_step());
    rebuilt = rebuilt.with_name(bp.name());
    return rebuilt;
}

} // namespace

Blueprint replace_node_preserve_order(const Blueprint& bp, Blueprint::Node updated) {
    Blueprint rebuilt = clone_metadata(bp);

    bool replaced = false;
    for (const auto& n : bp.nodes()) {
        if (n.id == updated.id) {
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
    for (const auto& n : bp.nested()) {
        rebuilt = rebuilt.with_nested(n);
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

    for (const auto& n : bp.nested()) {
        rebuilt = rebuilt.with_nested(n);
    }

    return rebuilt;
}

Blueprint replace_nested_preserve_order(const Blueprint& bp, Blueprint::Nested updated) {
    Blueprint rebuilt = clone_metadata(bp);

    for (const auto& n : bp.nodes()) {
        rebuilt = rebuilt.with_node(n);
    }
    for (const auto& w : bp.wires()) {
        rebuilt = rebuilt.with_wire(w);
    }

    bool replaced = false;
    for (const auto& n : bp.nested()) {
        if (n.id == updated.id) {
            rebuilt = rebuilt.with_nested(std::move(updated));
            replaced = true;
        } else {
            rebuilt = rebuilt.with_nested(n);
        }
    }
    if (!replaced) {
        rebuilt = rebuilt.with_nested(std::move(updated));
    }

    return rebuilt;
}

EditorModel::EditorModel(Blueprint initial)
    : current_(std::move(initial)) {}

bool EditorModel::add_node(Blueprint::Node node) {
    if (current_.find_node(node.id)) return false;
    push_checkpoint();
    current_ = current_.with_node(std::move(node));
    invalidate_indices();
    return true;
}

bool EditorModel::remove_node(ui::InternedId id) {
    if (!current_.find_node(id)) return false;
    push_checkpoint();
    current_ = current_.without_node(id);
    invalidate_indices();
    return true;
}

bool EditorModel::add_wire(Blueprint::Wire wire) {
    if (current_.find_wire(wire.id)) return false;
    if (wire.source == wire.target) return false;
    push_checkpoint();
    current_ = current_.with_wire(std::move(wire));
    invalidate_indices();
    return true;
}

bool EditorModel::remove_wire(ui::InternedId id) {
    if (!current_.find_wire(id)) return false;
    push_checkpoint();
    current_ = current_.without_wire(id);
    invalidate_indices();
    return true;
}

bool EditorModel::add_nested(Blueprint::Nested nested) {
    if (current_.find_nested(nested.id)) return false;
    push_checkpoint();
    current_ = current_.with_nested(std::move(nested));
    invalidate_indices();
    return true;
}

bool EditorModel::remove_nested(ui::InternedId id) {
    if (!current_.find_nested(id)) return false;
    push_checkpoint();
    current_ = current_.without_nested(id);
    invalidate_indices();
    return true;
}

bool EditorModel::update_node(ui::InternedId id, std::function<void(Blueprint::Node&)> fn) {
    auto const* existing = current_.find_node(id);
    if (!existing) return false;
    Blueprint::Node updated = *existing;
    fn(updated);
    push_checkpoint();
    current_ = replace_node_preserve_order(current_, std::move(updated));
    invalidate_indices();
    return true;
}

bool EditorModel::update_wire(ui::InternedId id, std::function<void(Blueprint::Wire&)> fn) {
    auto const* existing = current_.find_wire(id);
    if (!existing) return false;
    Blueprint::Wire updated = *existing;
    fn(updated);
    push_checkpoint();
    current_ = replace_wire_preserve_order(current_, std::move(updated));
    invalidate_indices();
    return true;
}

bool EditorModel::update_node_position(ui::InternedId id, float x, float y) {
    return update_node(id, [x, y](Blueprint::Node& n) {
        n.x = x;
        n.y = y;
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

bool EditorModel::bake_nested(ui::InternedId id,
                               BlueprintLibrary const& library,
                               ui::StringInterner& interner) {
    (void)interner;
    auto const* nested = current_.find_nested(id);
    if (!nested) return false;
    if (nested->embedded) return false;

    auto const* referenced = library.find(nested->blueprint_id);
    if (!referenced) return false;

    push_checkpoint();

    Blueprint::Nested baked;
    baked.id = nested->id;
    baked.blueprint_id = {};
    baked.embedded = true;
    baked.inline_def = std::make_unique<Blueprint>(*referenced);
    baked.iface = nested->iface;
    baked.x = nested->x;
    baked.y = nested->y;

    current_ = current_.without_nested(id).with_nested(std::move(baked));
    invalidate_indices();
    return true;
}

void EditorModel::ensure_indices() const {
    if (indices_.valid) return;

    indices_.node_pos.clear();
    indices_.wire_set.clear();

    indices_.node_pos.reserve(current_.nodes().size());
    for (auto const& n : current_.nodes()) {
        indices_.node_pos[n.id] = {n.x, n.y};
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

bool EditorModel::wire_exists(Path source, Path target) const {
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
