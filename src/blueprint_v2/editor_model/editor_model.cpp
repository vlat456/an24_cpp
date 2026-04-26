#include "editor_model.h"
#include "blueprint_v2/library/blueprint_library.h"
#include "blueprint_v2/blueprint/embedded_mutation.h"

namespace bp2 {

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

bool EditorModel::remove_node(core::InternedId id) {
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

bool EditorModel::remove_wire(core::InternedId id) {
    if (!current_.find_wire(id)) return false;
    push_checkpoint_if_enabled();
    current_ = current_.without_wire(id);
    invalidate_indices();
    return true;
}

bool EditorModel::update_node(core::InternedId id, std::function<void(Blueprint::Node&)> fn) {
    Blueprint next = current_;
    const MutationResult result = try_update_node(next, id, fn);
    if (result != MutationResult::Changed) {
        return false;
    }
    push_checkpoint_if_enabled();
    current_ = std::move(next);
    invalidate_indices();
    return true;
}

bool EditorModel::update_wire(core::InternedId id, std::function<void(Blueprint::Wire&)> fn) {
    Blueprint next = current_;
    const MutationResult result = try_update_wire(next, id, fn);
    if (result != MutationResult::Changed) {
        return false;
    }
    push_checkpoint_if_enabled();
    current_ = std::move(next);
    invalidate_indices();
    return true;
}

bool EditorModel::update_node_position(core::InternedId id, float x, float y) {
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

std::vector<core::InternedId> EditorModel::nodes_in_rect(Rect const& r) const {
    ensure_indices();
    std::vector<core::InternedId> out;
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
        std::string const& base, core::StringInterner const& interner) const {
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

MutationResult EditorModel::mutate_embedded(
    std::span<const core::InternedId> path,
    const std::function<Blueprint(const Blueprint&)>& mutation)
{
    if (path.empty()) return MutationResult::NotFound;

    const EmbeddedMutationResult result = mutate_embedded_blueprint(current_, path, mutation);

    switch (result.kind) {
        case EmbeddedMutationResultKind::PathNotFound:
            return MutationResult::NotFound;
        case EmbeddedMutationResultKind::NoChange:
            return MutationResult::NoChange;
        case EmbeddedMutationResultKind::Changed:
            if (result.blueprint.has_value()) {
                push_checkpoint_if_enabled();
                replace_current(std::move(*result.blueprint));
            }
            return MutationResult::Changed;
    }
    return MutationResult::NotFound;
}

bool EditorModel::update_embedded_node(
    std::span<const core::InternedId> path,
    core::InternedId node_id,
    const std::function<void(Blueprint::Node&)>& fn)
{
    const MutationResult result = mutate_embedded(path,
        [&](const Blueprint& embedded) -> Blueprint {
            Blueprint next = embedded;
            (void)try_update_node(next, node_id, fn);
            return next;
        });
    return result == MutationResult::Changed;
}

} // namespace bp2
