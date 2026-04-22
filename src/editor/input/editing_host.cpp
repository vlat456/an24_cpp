#include "editor/input/editing_host.h"
#include "blueprint_v2/bake/bake_ops.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "embedded_path_utils.h"

#include <stdexcept>

namespace {

/// EditorModel-backed implementation of EditingHost.
/// Delegates all operations directly to the root EditorModel.
class EditorModelHost : public EditingHost {
public:
    explicit EditorModelHost(bp2::EditorModel& model) : model_(model) {}

    const bp2::Blueprint& current_blueprint() const override {
        return model_.current();
    }

    const bp2::Blueprint::Node* find_node(ui::InternedId id) const override {
        return model_.current().find_node(id);
    }

    const bp2::Blueprint::Wire* find_wire(ui::InternedId id) const override {
        return model_.current().find_wire(id);
    }

    const std::vector<bp2::Blueprint::Wire>& wires() const override {
        return model_.current().wires();
    }

    const std::vector<bp2::Blueprint::Node>& nodes() const override {
        return model_.current().nodes();
    }

    void push_checkpoint() override {
        model_.push_checkpoint();
    }

    bool mutate_atomically(const std::function<void()>& fn) override {
        return model_.mutate_atomically(fn);
    }

    void replace_current(bp2::Blueprint bp) override {
        model_.replace_current(std::move(bp));
    }

    bool add_wire(bp2::Blueprint::Wire wire) override {
        return model_.add_wire(std::move(wire));
    }

    bool remove_wire(ui::InternedId id) override {
        return model_.remove_wire(id);
    }

    bool update_wire(ui::InternedId id,
                     std::function<void(bp2::Blueprint::Wire&)> fn) override {
        return model_.update_wire(id, std::move(fn));
    }

    bool update_node_position(ui::InternedId id, float x, float y) override {
        return model_.update_node_position(id, x, y);
    }

    bool update_node(ui::InternedId id,
                     std::function<void(bp2::Blueprint::Node&)> fn) override {
        return model_.update_node(id, std::move(fn));
    }

    bool remove_node(ui::InternedId id,
                     std::vector<ui::InternedId> connected_wire_ids) override {
        return model_.mutate_atomically([&] {
            for (ui::InternedId wid : connected_wire_ids) {
                model_.remove_wire(wid);
            }
            if (!model_.remove_node(id)) {
                throw std::logic_error("EditorModelHost::remove_node target not found");
            }
        });
    }

    bool bake_blueprint_instance(ui::InternedId id,
                                 const bp2::BlueprintLibrary& library) override {
        if (!model_.current().find_blueprint_instance(id)) {
            return false;
        }
        return model_.mutate_atomically([&] {
            model_.replace_current(bp2::bake_node_blueprint_instance(model_.current(), id, library));
        });
    }

    std::string allocate_wire_id() override {
        return model_.allocate_wire_id();
    }

private:
    bp2::EditorModel& model_;
};

/// EditingHost that operates on an embedded inline blueprint at an arbitrary
/// nesting depth. Stores the full instance path and walks the chain from root
/// on every access. Mutations are propagated back up through all ancestor
/// nodes to produce a new root Blueprint.
class EmbeddedInlineHost : public EditingHost {
public:
    /// Construct from a full instance path (may be multi-segment for deeply
    /// nested embedded scopes). The path must be non-empty.
    EmbeddedInlineHost(bp2::EditorModel& root_model,
                       std::vector<ui::InternedId> instance_path)
        : root_model_(root_model), path_(std::move(instance_path))
    {
        if (path_.empty()) {
            throw std::logic_error("EmbeddedInlineHost requires non-empty instance path");
        }
    }

    const bp2::Blueprint& current_blueprint() const override {
        return require_inline_blueprint(walk_to_host_node());
    }

    const bp2::Blueprint::Node* find_node(ui::InternedId id) const override {
        return current_blueprint().find_node(id);
    }

    const bp2::Blueprint::Wire* find_wire(ui::InternedId id) const override {
        return current_blueprint().find_wire(id);
    }

    const std::vector<bp2::Blueprint::Wire>& wires() const override {
        return current_blueprint().wires();
    }

    const std::vector<bp2::Blueprint::Node>& nodes() const override {
        return current_blueprint().nodes();
    }

    void push_checkpoint() override {
        root_model_.push_checkpoint();
    }

    bool mutate_atomically(const std::function<void()>& fn) override {
        return root_model_.mutate_atomically(fn);
    }

    void replace_current(bp2::Blueprint bp) override {
        root_model_.mutate_atomically([&] {
            propagate_inline_change(std::move(bp));
        });
    }

    bool add_wire(bp2::Blueprint::Wire wire) override {
        return root_model_.mutate_atomically([&] {
            auto next = current_blueprint().with_wire(std::move(wire));
            propagate_inline_change(std::move(next));
        });
    }

    bool remove_wire(ui::InternedId id) override {
        if (!current_blueprint().find_wire(id)) {
            return false;
        }
        return root_model_.mutate_atomically([&] {
            auto next = current_blueprint().without_wire(id);
            propagate_inline_change(std::move(next));
        });
    }

    bool update_wire(ui::InternedId id,
                     std::function<void(bp2::Blueprint::Wire&)> fn) override {
        const auto* existing = current_blueprint().find_wire(id);
        if (!existing) {
            return false;
        }
        bp2::Blueprint::Wire updated = *existing;
        fn(updated);
        return root_model_.mutate_atomically([&] {
            auto next = bp2::replace_wire_preserve_order(current_blueprint(), std::move(updated));
            propagate_inline_change(std::move(next));
        });
    }

    bool update_node_position(ui::InternedId id, float x, float y) override {
        return update_node(id, [x, y](bp2::Blueprint::Node& n) {
            n.layout.x = x;
            n.layout.y = y;
        });
    }

    bool update_node(ui::InternedId id,
                     std::function<void(bp2::Blueprint::Node&)> fn) override {
        const auto* existing = current_blueprint().find_node(id);
        if (!existing) {
            return false;
        }
        bp2::Blueprint::Node updated = *existing;
        fn(updated);
        return root_model_.mutate_atomically([&] {
            auto next = bp2::replace_node_preserve_order(current_blueprint(), std::move(updated));
            propagate_inline_change(std::move(next));
        });
    }

    bool remove_node(ui::InternedId id,
                     std::vector<ui::InternedId> connected_wire_ids) override {
        if (!current_blueprint().find_node(id)) {
            return false;
        }
        return root_model_.mutate_atomically([&] {
            bp2::Blueprint next = current_blueprint();
            for (ui::InternedId wid : connected_wire_ids) {
                next = next.without_wire(wid);
            }
            next = next.without_node(id);
            propagate_inline_change(std::move(next));
        });
    }

    bool bake_blueprint_instance(ui::InternedId id,
                                 const bp2::BlueprintLibrary& library) override {
        if (!current_blueprint().find_blueprint_instance(id)) {
            return false;
        }
        return root_model_.mutate_atomically([&] {
            propagate_inline_change(bp2::bake_node_blueprint_instance(current_blueprint(), id, library));
        });
    }

    std::string allocate_wire_id() override {
        return root_model_.allocate_wire_id();
    }

private:
    bp2::EditorModel& root_model_;
    std::vector<ui::InternedId> path_;

    static const bp2::Blueprint& require_inline_blueprint(const bp2::Blueprint::Node& node) {
        if (!node.has_embedded_blueprint()) {
            throw std::logic_error("EmbeddedInlineHost requires embedded blueprint-instance node");
        }
        const auto* inline_bp = node.blueprint_instance().source.inline_def();
        if (!inline_bp) {
            throw std::logic_error("EmbeddedInlineHost missing embedded inline blueprint");
        }
        return *inline_bp;
    }

    /// Walk the full instance path from root to reach the host node
    /// (the node whose inline_def() contains the embedded blueprint).
    const bp2::Blueprint::Node& walk_to_host_node() const {
        const bp2::ResolvedEmbeddedNode resolved =
            bp2::resolve_embedded_node(root_model_.current(), path_);
        if (!resolved.node) {
            throw std::logic_error("EmbeddedInlineHost: path segment not found in blueprint");
        }
        return *resolved.node;
    }

    /// Replace the inline blueprint of the deepest host node and propagate
    /// the change back up through all ancestor nodes to produce a new root.
    void propagate_inline_change(bp2::Blueprint next_inline) {
        const bp2::EmbeddedMutationResult result = bp2::mutate_embedded_blueprint(
            root_model_.current(), path_,
            [&next_inline](const bp2::Blueprint&) -> bp2::Blueprint {
                return std::move(next_inline);
            });

        if (result.kind == bp2::EmbeddedMutationResultKind::PathNotFound || !result.blueprint.has_value()) {
            throw std::logic_error("EmbeddedInlineHost: path broken during propagation");
        }
        if (result.kind == bp2::EmbeddedMutationResultKind::NoChange) {
            return;
        }
        root_model_.replace_current(std::move(*result.blueprint));
    }
};

}  // namespace

/// Read-only host: delegates reads to a const blueprint, no-ops all mutations.
class ReadOnlyHost : public EditingHost {
public:
    explicit ReadOnlyHost(const bp2::Blueprint& bp) : bp_(bp) {}

    const bp2::Blueprint& current_blueprint() const override { return bp_; }
    const bp2::Blueprint::Node* find_node(ui::InternedId id) const override { return bp_.find_node(id); }
    const bp2::Blueprint::Wire* find_wire(ui::InternedId id) const override { return bp_.find_wire(id); }
    const std::vector<bp2::Blueprint::Wire>& wires() const override { return bp_.wires(); }
    const std::vector<bp2::Blueprint::Node>& nodes() const override { return bp_.nodes(); }

    void push_checkpoint() override {}
    bool mutate_atomically(const std::function<void()>&) override { return false; }
    void replace_current(bp2::Blueprint) override {}
    bool add_wire(bp2::Blueprint::Wire) override { return false; }
    bool remove_wire(ui::InternedId) override { return false; }
    bool update_wire(ui::InternedId, std::function<void(bp2::Blueprint::Wire&)>) override { return false; }
    bool update_node_position(ui::InternedId, float, float) override { return false; }
    bool update_node(ui::InternedId, std::function<void(bp2::Blueprint::Node&)>) override { return false; }
    bool remove_node(ui::InternedId, std::vector<ui::InternedId>) override { return false; }
    bool bake_blueprint_instance(ui::InternedId, const bp2::BlueprintLibrary&) override { return false; }
    std::string allocate_wire_id() override { return {}; }

private:
    const bp2::Blueprint& bp_;
};

std::unique_ptr<EditingHost> create_editor_model_host(bp2::EditorModel& model) {
    return std::make_unique<EditorModelHost>(model);
}

std::unique_ptr<EditingHost> create_pathful_embedded_host(
    bp2::EditorModel& root_model,
    std::vector<ui::InternedId> instance_path) {
    return std::make_unique<EmbeddedInlineHost>(root_model, std::move(instance_path));
}

std::unique_ptr<EditingHost> create_read_only_host(const bp2::Blueprint& blueprint) {
    return std::make_unique<ReadOnlyHost>(blueprint);
}
