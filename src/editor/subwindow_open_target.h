#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "ui/core/interned_id.h"
#include <string>

namespace editor {

enum class SubWindowOpenTargetKind {
    Missing,
    EmbeddedNested,
    ReferencedNested,
    ExternalReference,
};

struct SubWindowOpenTarget {
    SubWindowOpenTargetKind kind = SubWindowOpenTargetKind::Missing;
    std::string path;
};

inline SubWindowOpenTarget resolve_subwindow_open_target(const bp2::Blueprint& bp,
                                                         ui::StringInterner& interner,
                                                         const std::string& sub_blueprint_id) {
    const auto lookup_id = interner.lookup(sub_blueprint_id);
    if (lookup_id.empty()) {
        return {};
    }

    if (const auto* nested = bp.find_nested(lookup_id)) {
        return {nested->embedded ? SubWindowOpenTargetKind::EmbeddedNested
                                 : SubWindowOpenTargetKind::ReferencedNested,
                {}};
    }

    const bp2::Blueprint::Node* node = bp.find_node(lookup_id);
    if (node && node->expandable && !node->blueprint_path.empty()) {
        return {SubWindowOpenTargetKind::ExternalReference,
                "library/" + node->blueprint_path + ".blueprint"};
    }

    return {};
}

} // namespace editor
