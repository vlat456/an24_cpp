#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/interface/node_port_projection.h"
#include "blueprint_v2/path/path.h"
#include "editor/input/editing_host.h"
#include "editor/visual/presentation/semantic_canvas_controller.h"
#include "ui/core/interned_id.h"
#include "ui/math/pt.h"
#include "debug.h"
#include "visual/persist.h"
#include "visual/snap.h"
#include "visual/widget.h"
#include <cstdio>
#include <cassert>
#include <string_view>

namespace canvas_input_impl {

// ============================================================================
// Semantic snapshot bridge
// ============================================================================

/// Build a single-object SemanticSceneSnapshot for the given interaction
/// region. Used by CanvasInput to feed the semantic canvas controller during
/// toggle/slider/knob interactions.
inline editor::presentation::SemanticSceneSnapshot build_interaction_snapshot(
    ui::InternedId node_id,
    const editor::presentation::Rect& bounds,
    const visual::InteractionTarget& target) {
    using namespace editor::presentation;

    SceneHitObject hit_object;
    hit_object.node_id    = node_id;
    hit_object.element_id = node_id;
    hit_object.region_id  = node_id;
    hit_object.kind       = SceneHitObjectKind::ContentRegion;
    hit_object.bounds     = bounds;

    InteractionBinding binding;
    binding.region_id = node_id;
    binding.action_id = node_id;

    switch (target.role) {
        case visual::InteractionRole::ContinuousScalar:
            binding.kind      = InteractionKind::DragScalar;
            binding.min_value = target.primary_min;
            binding.max_value = target.primary_max;
            break;
        case visual::InteractionRole::DiscreteSelector:
            binding.kind      = InteractionKind::DragDiscrete;
            binding.min_value = target.primary_min;
            binding.max_value = target.primary_max;
            binding.step      = static_cast<float>(target.steps);
            break;
        case visual::InteractionRole::Toggle:
            binding.kind = InteractionKind::Click;
            break;
    }

    hit_object.interactions.push_back(binding);

    SemanticSceneSnapshot snapshot;
    snapshot.hit_objects.push_back(std::move(hit_object));
    return snapshot;
}

// ============================================================================
// Blueprint helpers
// ============================================================================

inline bool is_bus_node(const bp2::Blueprint& bp, ui::InternedId node_id) {
    const bp2::Blueprint::Node* node = bp.find_node(node_id);
    if (!node) return false;
    return node->view.render_hint == "bus";
}

inline bool is_wire_alias_port_name(std::string_view port_name) {
    return !port_name.empty() && port_name != "v";
}

inline PortType resolve_port_type_from_model(const bp2::Blueprint& bp,
                                             ui::InternedId node_id,
                                             ui::InternedId port_name) {
    const bp2::Blueprint::Node* node = bp.find_node(node_id);
    if (!node) return PortType::Any;
    for (const auto& p : bp.effective_node_iface(*node).ports()) {
        if (p.name == port_name) return p.port_type;
    }
    return PortType::Any;
}

inline void debug_validate_command_boundary(const bp2::Blueprint& bp,
                                            ui::StringInterner& interner,
                                            bp2::PathArena const& arena,
                                            const TypeRegistry* parser_registry = nullptr) {
#ifndef NDEBUG
    if (!parser_registry) {
        return;
    }

    std::string err;
    const bool ok = validate_blueprint_integrity(bp, interner, arena, *parser_registry, &err);
    if (!ok) {
        if (err.find("wire domain differs from endpoint domain") != std::string::npos
            || err.find("wire direction incompatible") != std::string::npos
            || err.find("wire endpoint path unresolved") != std::string::npos
            || err.find("wire endpoint domain mismatch") != std::string::npos) {
            return;
        }
        std::fprintf(stderr, "[bp2][debug] command boundary invariant failed: %s\n", err.c_str());
        assert(false && "bp2 integrity violation at command boundary");
    }
#else
    (void)bp;
    (void)interner;
    (void)arena;
#endif
}

} // namespace canvas_input_impl
