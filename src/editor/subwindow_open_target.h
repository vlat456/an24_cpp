#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "ui/core/interned_id.h"
#include <string>

namespace bp2 {
struct LibraryIndex;
}

namespace editor {

enum class SubWindowOpenTargetKind {
    Missing,
    EmbeddedNested,
    ReferencedNested,
    ExternalReference,
};

enum class SubWindowOpenTargetFailure {
    None,
    UnknownNodeId,
    NotBlueprintInstance,
    MissingBlueprintSource,
    MissingLibraryIndexEntry,
};

struct SubWindowOpenTarget {
    SubWindowOpenTargetKind kind = SubWindowOpenTargetKind::Missing;
    std::string path;
};

struct SubWindowOpenTargetResult {
    SubWindowOpenTarget target;
    SubWindowOpenTargetFailure failure = SubWindowOpenTargetFailure::None;

    bool ok() const {
        return failure == SubWindowOpenTargetFailure::None
            && target.kind != SubWindowOpenTargetKind::Missing;
    }
};

SubWindowOpenTargetResult resolve_subwindow_open_target(const bp2::Blueprint& bp,
                                                        ui::StringInterner& interner,
                                                        const bp2::LibraryIndex& library_index,
                                                        const std::string& sub_blueprint_id);

const char* to_string(SubWindowOpenTargetFailure failure);

} // namespace editor
