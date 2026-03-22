#include "editor_model.h"
#include "blueprint_v2/registry/type_registry.h"

namespace bp2 {

EditorModel::EditorModel(Blueprint initial)
    : current_(std::move(initial)) {}

bool EditorModel::add_node(Blueprint::Node node) {
    if (current_.find_node(node.id)) return false;
    push_checkpoint();
    current_ = current_.with_node(std::move(node));
    return true;
}

bool EditorModel::remove_node(ui::InternedId id) {
    if (!current_.find_node(id)) return false;
    push_checkpoint();
    current_ = current_.without_node(id);
    return true;
}

bool EditorModel::add_wire(Blueprint::Wire wire) {
    if (current_.find_wire(wire.id)) return false;
    if (wire.source == wire.target) return false;  // self-loop
    push_checkpoint();
    current_ = current_.with_wire(std::move(wire));
    return true;
}

bool EditorModel::remove_wire(ui::InternedId id) {
    if (!current_.find_wire(id)) return false;
    push_checkpoint();
    current_ = current_.without_wire(id);
    return true;
}

bool EditorModel::add_nested(Blueprint::Nested nested) {
    if (current_.find_nested(nested.id)) return false;
    push_checkpoint();
    current_ = current_.with_nested(std::move(nested));
    return true;
}

bool EditorModel::remove_nested(ui::InternedId id) {
    if (!current_.find_nested(id)) return false;
    push_checkpoint();
    current_ = current_.without_nested(id);
    return true;
}

bool EditorModel::update_node_position(ui::InternedId id, float x, float y) {
    auto const* existing = current_.find_node(id);
    if (!existing) return false;
    Blueprint::Node updated = *existing;
    updated.x = x;
    updated.y = y;
    push_checkpoint();
    current_ = current_.without_node(id).with_node(std::move(updated));
    return true;
}

void EditorModel::push_checkpoint() {
    redo_stack_.clear();
    if (undo_stack_.size() >= max_history_) {
        undo_stack_.erase(undo_stack_.begin());
    }
    undo_stack_.push_back(current_);
}

void EditorModel::undo() {
    if (!can_undo()) return;
    redo_stack_.push_back(std::move(current_));
    current_ = std::move(undo_stack_.back());
    undo_stack_.pop_back();
}

void EditorModel::redo() {
    if (!can_redo()) return;
    undo_stack_.push_back(std::move(current_));
    current_ = std::move(redo_stack_.back());
    redo_stack_.pop_back();
}

bool EditorModel::bake_nested(ui::InternedId id,
                               TypeRegistry const& registry,
                               ui::StringInterner& interner) {
    auto const* nested = current_.find_nested(id);
    if (!nested) return false;
    if (nested->embedded) return false;

    auto* entry = registry.find(nested->blueprint_id);
    if (!entry || !entry->blueprint) return false;

    Blueprint::Nested baked;
    baked.id = nested->id;
    baked.blueprint_id = {};
    baked.embedded = true;
    baked.inline_def = std::make_unique<Blueprint>(*entry->blueprint);
    baked.iface = nested->iface;
    baked.x = nested->x;
    baked.y = nested->y;

    push_checkpoint();
    current_ = current_.without_nested(id).with_nested(std::move(baked));
    return true;
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
