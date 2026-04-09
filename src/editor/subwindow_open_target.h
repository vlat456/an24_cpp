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

SubWindowOpenTarget resolve_subwindow_open_target(const bp2::Blueprint& bp,
                                                  ui::StringInterner& interner,
                                                  const TypeRegistry& registry,
                                                  const std::string& sub_blueprint_id);

} // namespace editor
