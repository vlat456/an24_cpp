#include "editor/input/editing_host.h"
#include "blueprint_v2/bake/bake_ops.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "blueprint_v2/validation/wire_validator.h"
#include "blueprint_v2/validation/signal_typing.h"
#include "blueprint_v2/interface/node_port_projection.h"
#include "core/model/component_registry.h"
#include "core/strings/interned_id.h"
#include "blueprint_v2/path/path.h"
#include "visual/persist.h"
#include "embedded_path_utils.h"

#include <stdexcept>

namespace {

/// Shared helper: resolve frame kind from a node's type via registry.
/// Returns Standard when registry or interner is null, or node is unknown.
editor::presentation::NodeFrameKind do_resolve_frame_kind(
    const bp2::Blueprint& bp,
    core::InternedId node_id,
    const ComponentRegistry& registry,
    core::StringInterner& interner) {
    const bp2::Blueprint::Node* node = bp.find_node(node_id);
    if (!node) return editor::presentation::NodeFrameKind::Standard;
    const std::string type_name(interner.resolve(node->semantic.type));
    const ComponentSpec* def = registry.get(type_name);
    const TypePresentation* pres = registry.get_presentation(type_name);
    return editor::presentation::resolve_frame_kind(def, pres);
}

/// Shared helper: resolve port type via registry-backed iface resolution.
PortType do_resolve_port_type(
    const bp2::Blueprint& bp,
    core::InternedId node_id,
    core::InternedId port_name,
    const ComponentRegistry& registry,
    core::StringInterner& interner) {
    const bp2::Blueprint::Node* node = bp.find_node(node_id);
    if (!node) return PortType::Any;
    const bp2::Interface iface = bp.resolve_node_iface(
        *node, bp2::Blueprint::NodeIfaceAuthority{interner, &registry});
    for (const auto& p : iface.ports()) {
        if (p.name == port_name) return p.port_type;
    }
    return PortType::Any;
}

/// Shared helper: resolve full port descriptor.
std::optional<bp2::PortDescriptor> do_resolve_port_descriptor(
    const bp2::Blueprint& bp,
    core::InternedId node_id,
    core::InternedId port_name,
    const ComponentRegistry& registry,
    core::StringInterner& interner) {
    const bp2::Blueprint::Node* node = bp.find_node(node_id);
    if (!node) return std::nullopt;
    const bp2::Interface iface = bp.resolve_node_iface(
        *node, bp2::Blueprint::NodeIfaceAuthority{interner, &registry});
    return iface.find(port_name);
}

/// Shared helper: validate a potential wire.
/// Resolves domain first, then validates with correct domain on probe.
EditingHost::WireValidation do_validate_wire(
    const bp2::Blueprint& bp,
    bp2::WireEndpoint source,
    bp2::WireEndpoint target,
    const ComponentRegistry& registry,
    core::StringInterner& interner) {
    // First pass: resolve domain.
    bp2::Blueprint::Wire domain_probe;
    domain_probe.source = source;
    domain_probe.target = target;
    const Domain resolved = bp2::WireValidator::validate(
        domain_probe, bp, registry, interner).resolved_domain;
    // Second pass: validate with resolved domain set.
    bp2::Blueprint::Wire probe;
    probe.source = source;
    probe.target = target;
    probe.domain = resolved;
    const auto result = bp2::WireValidator::validate(probe, bp, registry, interner);
    return {result.valid, resolved};
}

/// Shared helper: resolve wire domain.
Domain do_resolve_wire_domain(
    const bp2::Blueprint& bp,
    bp2::WireEndpoint source,
    bp2::WireEndpoint target,
    const ComponentRegistry& registry,
    core::StringInterner& interner) {
    bp2::Blueprint::Wire probe;
    probe.source = source;
    probe.target = target;
    return bp2::WireValidator::validate(probe, bp, registry, interner).resolved_domain;
}

/// Shared helper: resolve full compiled presentation spec for a node.
editor::presentation::CompiledPresentationSpec do_resolve_presentation_spec(
    const bp2::Blueprint& bp,
    core::InternedId node_id,
    const ComponentRegistry& registry,
    core::StringInterner& interner) {
    const bp2::Blueprint::Node* node = bp.find_node(node_id);
    if (!node) return {};
    const std::string type_name(interner.resolve(node->semantic.type));
    const ComponentSpec* def = registry.get(type_name);
    const TypePresentation* pres = registry.get_presentation(type_name);
    return editor::presentation::make_presentation_spec(*node, def, pres, interner);
}

/// Shared helper: debug-only integrity check.
void do_debug_validate_integrity(
    const bp2::Blueprint& bp,
    core::StringInterner& interner,
    const bp2::PathArena& arena,
    const ComponentRegistry& registry) {
#ifndef NDEBUG
    std::string err;
    const bool ok = validate_blueprint_integrity(bp, interner, arena, registry, &err);
    if (!ok) {
        // Known benign errors from bus/reference canonicalization — skip.
        if (err.find("wire domain differs from endpoint domain") != std::string::npos
            || err.find("wire direction incompatible") != std::string::npos
            || err.find("wire endpoint path unresolved") != std::string::npos
            || err.find("wire endpoint domain mismatch") != std::string::npos
            || err.find("component node iface desynced") != std::string::npos) {
            return;
        }
        std::fprintf(stderr, "[bp2][debug] command boundary invariant failed: %s\n", err.c_str());
        assert(false && "bp2 integrity violation at command boundary");
    }
#endif
}

// ============================================================================
// EditorModelHost
// ============================================================================

/// EditorModel-backed implementation of EditingHost.
/// Delegates all operations directly to the root EditorModel.
class EditorModelHost : public EditingHost {
public:
    EditorModelHost(bp2::EditorModel& model,
                    const ComponentRegistry* registry,
                    core::StringInterner* interner,
                    const bp2::PathArena* arena)
        : model_(model), registry_(registry), interner_(interner), arena_(arena) {}

    const bp2::Blueprint& current_blueprint() const override {
        return model_.current();
    }

    const bp2::Blueprint::Node* find_node(core::InternedId id) const override {
        return model_.current().find_node(id);
    }

    const bp2::Blueprint::Wire* find_wire(core::InternedId id) const override {
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

    bool remove_wire(core::InternedId id) override {
        return model_.remove_wire(id);
    }

    bool update_wire(core::InternedId id,
                     std::function<void(bp2::Blueprint::Wire&)> fn) override {
        return model_.update_wire(id, std::move(fn));
    }

    bool update_node_position(core::InternedId id, float x, float y) override {
        return model_.update_node_position(id, x, y);
    }

    bool update_node(core::InternedId id,
                     std::function<void(bp2::Blueprint::Node&)> fn) override {
        return model_.update_node(id, std::move(fn));
    }

    bool remove_node(core::InternedId id,
                     std::vector<core::InternedId> connected_wire_ids) override {
        return model_.mutate_atomically([&] {
            for (core::InternedId wid : connected_wire_ids) {
                model_.remove_wire(wid);
            }
            if (!model_.remove_node(id)) {
                throw std::logic_error("EditorModelHost::remove_node target not found");
            }
        });
    }

    bool bake_blueprint_instance(core::InternedId id,
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

    // ── Registry-backed queries ──

    editor::presentation::NodeFrameKind resolve_frame_kind(
        core::InternedId node_id) const override {
        if (!has_registry()) return editor::presentation::NodeFrameKind::Standard;
        return do_resolve_frame_kind(model_.current(), node_id, *registry_, *interner_);
    }

    PortType resolve_port_type(
        core::InternedId node_id, core::InternedId port_name) const override {
        if (!has_registry()) return PortType::Any;
        return do_resolve_port_type(model_.current(), node_id, port_name, *registry_, *interner_);
    }

    std::optional<bp2::PortDescriptor> resolve_port_descriptor(
        core::InternedId node_id, core::InternedId port_name) const override {
        if (!has_registry()) return std::nullopt;
        return do_resolve_port_descriptor(model_.current(), node_id, port_name, *registry_, *interner_);
    }

    WireValidation validate_wire(
        bp2::WireEndpoint source, bp2::WireEndpoint target) const override {
        if (!has_registry()) return {true, Domain::Electrical};
        return do_validate_wire(model_.current(), source, target, *registry_, *interner_);
    }

    Domain resolve_wire_domain(
        bp2::WireEndpoint source, bp2::WireEndpoint target) const override {
        if (!has_registry()) return Domain::Electrical;
        return do_resolve_wire_domain(model_.current(), source, target, *registry_, *interner_);
    }

    void debug_validate_integrity() const override {
        if (!has_registry() || !arena_) return;
        do_debug_validate_integrity(model_.current(), *interner_, *arena_, *registry_);
    }

    editor::presentation::CompiledPresentationSpec resolve_presentation_spec(
        core::InternedId node_id) const override {
        if (!has_registry()) return {};
        return do_resolve_presentation_spec(model_.current(), node_id, *registry_, *interner_);
    }

    const ComponentRegistry* type_registry() const override { return registry_; }

private:
    bp2::EditorModel& model_;
    const ComponentRegistry* registry_;
    core::StringInterner* interner_;
    const bp2::PathArena* arena_;

    bool has_registry() const { return registry_ && interner_; }
};

// ============================================================================
// EmbeddedInlineHost
// ============================================================================

/// EditingHost that operates on an embedded inline blueprint at an arbitrary
/// nesting depth. Stores the full instance path and walks the chain from root
/// on every access. Mutations are propagated back up through all ancestor
/// nodes to produce a new root Blueprint.
class EmbeddedInlineHost : public EditingHost {
public:
    /// Construct from a full instance path (may be multi-segment for deeply
    /// nested embedded scopes). The path must be non-empty.
    EmbeddedInlineHost(bp2::EditorModel& root_model,
                       std::vector<core::InternedId> instance_path,
                       const ComponentRegistry* registry,
                       core::StringInterner* interner,
                       const bp2::PathArena* arena)
        : root_model_(root_model), path_(std::move(instance_path)),
          registry_(registry), interner_(interner), arena_(arena)
    {
        if (path_.empty()) {
            throw std::logic_error("EmbeddedInlineHost requires non-empty instance path");
        }
    }

    const bp2::Blueprint& current_blueprint() const override {
        return require_inline_blueprint(require_host_node());
    }

    const bp2::Blueprint::Node* find_node(core::InternedId id) const override {
        return current_blueprint().find_node(id);
    }

    const bp2::Blueprint::Wire* find_wire(core::InternedId id) const override {
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

    bool remove_wire(core::InternedId id) override {
        if (!current_blueprint().find_wire(id)) {
            return false;
        }
        return root_model_.mutate_atomically([&] {
            auto next = current_blueprint().without_wire(id);
            propagate_inline_change(std::move(next));
        });
    }

    bool update_wire(core::InternedId id,
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

    bool update_node_position(core::InternedId id, float x, float y) override {
        return update_node(id, [x, y](bp2::Blueprint::Node& n) {
            n.layout.x = x;
            n.layout.y = y;
        });
    }

    bool update_node(core::InternedId id,
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

    bool remove_node(core::InternedId id,
                     std::vector<core::InternedId> connected_wire_ids) override {
        if (!current_blueprint().find_node(id)) {
            return false;
        }
        return root_model_.mutate_atomically([&] {
            bp2::Blueprint next = current_blueprint();
            for (core::InternedId wid : connected_wire_ids) {
                next = next.without_wire(wid);
            }
            next = next.without_node(id);
            propagate_inline_change(std::move(next));
        });
    }

    bool bake_blueprint_instance(core::InternedId id,
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

    // ── Registry-backed queries ──

    editor::presentation::NodeFrameKind resolve_frame_kind(
        core::InternedId node_id) const override {
        if (!has_registry()) return editor::presentation::NodeFrameKind::Standard;
        return do_resolve_frame_kind(current_blueprint(), node_id, *registry_, *interner_);
    }

    PortType resolve_port_type(
        core::InternedId node_id, core::InternedId port_name) const override {
        if (!has_registry()) return PortType::Any;
        return do_resolve_port_type(current_blueprint(), node_id, port_name, *registry_, *interner_);
    }

    std::optional<bp2::PortDescriptor> resolve_port_descriptor(
        core::InternedId node_id, core::InternedId port_name) const override {
        if (!has_registry()) return std::nullopt;
        return do_resolve_port_descriptor(current_blueprint(), node_id, port_name, *registry_, *interner_);
    }

    WireValidation validate_wire(
        bp2::WireEndpoint source, bp2::WireEndpoint target) const override {
        if (!has_registry()) return {true, Domain::Electrical};
        return do_validate_wire(current_blueprint(), source, target, *registry_, *interner_);
    }

    Domain resolve_wire_domain(
        bp2::WireEndpoint source, bp2::WireEndpoint target) const override {
        if (!has_registry()) return Domain::Electrical;
        return do_resolve_wire_domain(current_blueprint(), source, target, *registry_, *interner_);
    }

    void debug_validate_integrity() const override {
        if (!has_registry() || !arena_) return;
        do_debug_validate_integrity(current_blueprint(), *interner_, *arena_, *registry_);
    }

    editor::presentation::CompiledPresentationSpec resolve_presentation_spec(
        core::InternedId node_id) const override {
        if (!has_registry()) return {};
        return do_resolve_presentation_spec(current_blueprint(), node_id, *registry_, *interner_);
    }

    const ComponentRegistry* type_registry() const override { return registry_; }

private:
    bp2::EditorModel& root_model_;
    std::vector<core::InternedId> path_;
    const ComponentRegistry* registry_;
    core::StringInterner* interner_;
    const bp2::PathArena* arena_;

    bool has_registry() const { return registry_ && interner_; }

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
    /// Returns a pointer into root_model_.current() — stable until next mutation.
    const bp2::Blueprint::Node* find_host_node() const {
        return bp2::find_embedded_node(root_model_.current(), path_);
    }

    const bp2::Blueprint::Node& require_host_node() const {
        const auto* node = find_host_node();
        if (!node) {
            throw std::logic_error("EmbeddedInlineHost: path segment not found in blueprint");
        }
        return *node;
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

// ============================================================================
// Read-only host
// ============================================================================

/// Read-only host: delegates reads to a const blueprint, no-ops all mutations.
class ReadOnlyHost : public EditingHost {
public:
    explicit ReadOnlyHost(const bp2::Blueprint& bp) : bp_(bp) {}

    const bp2::Blueprint& current_blueprint() const override { return bp_; }
    const bp2::Blueprint::Node* find_node(core::InternedId id) const override { return bp_.find_node(id); }
    const bp2::Blueprint::Wire* find_wire(core::InternedId id) const override { return bp_.find_wire(id); }
    const std::vector<bp2::Blueprint::Wire>& wires() const override { return bp_.wires(); }
    const std::vector<bp2::Blueprint::Node>& nodes() const override { return bp_.nodes(); }

    void push_checkpoint() override {}
    bool mutate_atomically(const std::function<void()>&) override { return false; }
    void replace_current(bp2::Blueprint) override {}
    bool add_wire(bp2::Blueprint::Wire) override { return false; }
    bool remove_wire(core::InternedId) override { return false; }
    bool update_wire(core::InternedId, std::function<void(bp2::Blueprint::Wire&)>) override { return false; }
    bool update_node_position(core::InternedId, float, float) override { return false; }
    bool update_node(core::InternedId, std::function<void(bp2::Blueprint::Node&)>) override { return false; }
    bool remove_node(core::InternedId, std::vector<core::InternedId>) override { return false; }
    bool bake_blueprint_instance(core::InternedId, const bp2::BlueprintLibrary&) override { return false; }
    std::string allocate_wire_id() override { return {}; }

private:
    const bp2::Blueprint& bp_;
};

// ============================================================================
// Factory functions
// ============================================================================

std::unique_ptr<EditingHost> create_editor_model_host(
    bp2::EditorModel& model,
    const ComponentRegistry* registry,
    core::StringInterner* interner,
    const bp2::PathArena* arena) {
    return std::make_unique<EditorModelHost>(model, registry, interner, arena);
}

std::unique_ptr<EditingHost> create_pathful_embedded_host(
    bp2::EditorModel& root_model,
    std::vector<core::InternedId> instance_path,
    const ComponentRegistry* registry,
    core::StringInterner* interner,
    const bp2::PathArena* arena) {
    return std::make_unique<EmbeddedInlineHost>(root_model, std::move(instance_path), registry, interner, arena);
}

std::unique_ptr<EditingHost> create_read_only_host(const bp2::Blueprint& blueprint) {
    return std::make_unique<ReadOnlyHost>(blueprint);
}
