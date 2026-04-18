#include "editor/input/editing_host.h"
#include "blueprint_v2/editor_model/editor_model.h"

#include <stdexcept>

namespace {

/// EditorModel-backed implementation of EditingHost.
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
        return model_.update_wire(id, fn);
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

    std::string allocate_wire_id() override {
        return model_.allocate_wire_id();
    }

private:
    bp2::EditorModel& model_;
};

class EmbeddedInlineHost : public EditingHost {
public:
    EmbeddedInlineHost(bp2::EditorModel& root_model, ui::InternedId nested_id)
        : root_model_(root_model), nested_id_(nested_id) {}

    const bp2::Blueprint& current_blueprint() const override {
        return require_inline_blueprint(require_node());
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
            replace_inline_def(std::move(bp));
        });
    }

    bool add_wire(bp2::Blueprint::Wire wire) override {
        return root_model_.mutate_atomically([&] {
            auto next = current_blueprint().with_wire(std::move(wire));
            replace_inline_def(std::move(next));
        });
    }

    bool remove_wire(ui::InternedId id) override {
        if (!current_blueprint().find_wire(id)) {
            return false;
        }
        return root_model_.mutate_atomically([&] {
            auto next = current_blueprint().without_wire(id);
            replace_inline_def(std::move(next));
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
            replace_inline_def(std::move(next));
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
            replace_inline_def(std::move(next));
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
            replace_inline_def(std::move(next));
        });
    }

    std::string allocate_wire_id() override {
        return root_model_.allocate_wire_id();
    }

private:
    bp2::EditorModel& root_model_;
    ui::InternedId nested_id_;

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

    const bp2::Blueprint::Node& require_node() const {
        const auto* node = root_model_.current().find_node(nested_id_);
        if (!node) {
            throw std::logic_error("EmbeddedInlineHost missing blueprint-instance node");
        }
        if (!node->has_embedded_blueprint()) {
            throw std::logic_error("EmbeddedInlineHost requires embedded blueprint-instance node");
        }
        return *node;
    }

    void replace_inline_def(bp2::Blueprint next_inline) {
        const bp2::Blueprint::Node& node = require_node();
        auto updated = node;
        updated.blueprint_instance().source.set_inline_def(std::make_unique<bp2::Blueprint>(std::move(next_inline)));
        root_model_.replace_current(bp2::replace_node_preserve_order(root_model_.current(), std::move(updated)));
    }
};

}  // namespace

std::unique_ptr<EditingHost> create_editor_model_host(bp2::EditorModel& model) {
    return std::make_unique<EditorModelHost>(model);
}

std::unique_ptr<EditingHost> create_embedded_inline_host(bp2::EditorModel& root_model,
                                                         ui::InternedId nested_id) {
    return std::make_unique<EmbeddedInlineHost>(root_model, nested_id);
}
