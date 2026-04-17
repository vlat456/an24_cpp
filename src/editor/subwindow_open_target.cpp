#include "subwindow_open_target.h"

#include "blueprint_v2/library/library_path.h"

namespace editor {

const char* to_string(SubWindowOpenTargetFailure failure) {
    switch (failure) {
        case SubWindowOpenTargetFailure::None:
            return "none";
        case SubWindowOpenTargetFailure::UnknownNodeId:
            return "unknown node id";
        case SubWindowOpenTargetFailure::NotBlueprintInstance:
            return "node is not a blueprint instance";
        case SubWindowOpenTargetFailure::MissingBlueprintSource:
            return "blueprint instance missing source";
        case SubWindowOpenTargetFailure::MissingLibraryIndexEntry:
            return "referenced blueprint id missing from library index";
    }
    return "unknown";
}

SubWindowOpenTargetResult resolve_subwindow_open_target(const bp2::Blueprint& bp,
                                                        ui::StringInterner& interner,
                                                        const bp2::LibraryIndex& library_index,
                                                        const std::string& sub_blueprint_id) {
    const auto lookup_id = interner.lookup(sub_blueprint_id);
    if (lookup_id.empty()) {
        return {{}, SubWindowOpenTargetFailure::UnknownNodeId};
    }

    const bp2::Blueprint::Node* node = bp.find_node(lookup_id);
    if (!node) {
        return {{}, SubWindowOpenTargetFailure::UnknownNodeId};
    }
    if (!node->is_blueprint_instance()) {
        return {{}, SubWindowOpenTargetFailure::NotBlueprintInstance};
    }
    if (!node->source.has_value()) {
        return {{}, SubWindowOpenTargetFailure::MissingBlueprintSource};
    }
    if (node->source->is_embedded()) {
        return {{SubWindowOpenTargetKind::EmbeddedNested, {}}, SubWindowOpenTargetFailure::None};
    }
    if (node->source->is_reference()) {
        auto path = bp2::resolve_library_blueprint_path(
            library_index,
            std::string(interner.resolve(node->source->blueprint_id())));
        if (!path.has_value()) {
            return {{}, SubWindowOpenTargetFailure::MissingLibraryIndexEntry};
        }
        return {{SubWindowOpenTargetKind::ReferencedNested, std::move(*path)},
                SubWindowOpenTargetFailure::None};
    }

    return {{}, SubWindowOpenTargetFailure::MissingBlueprintSource};
}

} // namespace editor
