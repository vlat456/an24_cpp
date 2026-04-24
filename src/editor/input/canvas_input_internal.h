#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/interface/node_port_projection.h"
#include "blueprint_v2/path/path.h"
#include "editor/input/editing_host.h"
#include "editor/visual/presentation/node_presentation.h"
#include "core/model/presentation_spec.h"
#include "core/model/component_spec.h"
#include "ui/core/interned_id.h"
#include "ui/math/pt.h"
#include "debug.h"
#include "visual/persist.h"
#include "visual/snap.h"
#include <cstdio>
#include <cassert>
#include <string_view>

namespace canvas_input_impl {

// ============================================================================
// Blueprint helpers
// ============================================================================

inline bool is_bus_node(const bp2::Blueprint& bp, ui::InternedId node_id,
                        const ComponentRegistry& registry, const ui::StringInterner& interner) {
    const bp2::Blueprint::Node* node = bp.find_node(node_id);
    if (!node) return false;
    const std::string type_name(interner.resolve(node->semantic.type));
    const ComponentSpec* def = registry.get(type_name);
    const TypePresentation* pres = registry.get_presentation(type_name);
    return editor::presentation::resolve_frame_kind(def, pres)
        == editor::presentation::NodeFrameKind::Bus;
}

inline bool is_ref_node(const bp2::Blueprint::Node& node,
                        const ComponentRegistry& registry, const ui::StringInterner& interner) {
    const std::string type_name(interner.resolve(node.semantic.type));
    const ComponentSpec* def = registry.get(type_name);
    const TypePresentation* pres = registry.get_presentation(type_name);
    return editor::presentation::resolve_frame_kind(def, pres)
        == editor::presentation::NodeFrameKind::Reference;
}

inline bool is_wire_alias_port_name(std::string_view port_name) {
    return !port_name.empty() && port_name != "v";
}

inline PortType resolve_port_type_from_model(const bp2::Blueprint& bp,
                                             ui::InternedId node_id,
                                             ui::InternedId port_name,
                                             const ComponentRegistry& registry,
                                             ui::StringInterner& interner) {
    const bp2::Blueprint::Node* node = bp.find_node(node_id);
    if (!node) return PortType::Any;
    for (const auto& p : bp.resolve_node_iface(*node, bp2::Blueprint::NodeIfaceAuthority{interner, &registry}).ports()) {
        if (p.name == port_name) return p.port_type;
    }
    return PortType::Any;
}

inline void debug_validate_command_boundary(const bp2::Blueprint& bp,
                                            ui::StringInterner& interner,
                                            bp2::PathArena const& arena,
                                            const ComponentRegistry* parser_registry = nullptr) {
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
            || err.find("wire endpoint domain mismatch") != std::string::npos
            || err.find("component node iface desynced") != std::string::npos) {
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
