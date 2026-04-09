#include "subwindow_open_target.h"

#include "blueprint_v2/library/library_path.h"

namespace editor {

SubWindowOpenTarget resolve_subwindow_open_target(const bp2::Blueprint& bp,
                                                  ui::StringInterner& interner,
                                                  const TypeRegistry& registry,
                                                  const std::string& sub_blueprint_id) {
    const auto lookup_id = interner.lookup(sub_blueprint_id);
    if (lookup_id.empty()) {
        return {};
    }

    if (const auto* nested = bp.find_nested(lookup_id)) {
        if (nested->is_embedded()) {
            return {SubWindowOpenTargetKind::EmbeddedNested, {}};
        }

        auto path = bp2::resolve_library_blueprint_path(
            registry,
            std::string(interner.resolve(nested->blueprint_id())));
        if (!path.has_value()) {
            return {};
        }

        return {SubWindowOpenTargetKind::ReferencedNested, std::move(*path)};
    }

    const bp2::Blueprint::Node* node = bp.find_node(lookup_id);
    if (node && bp.is_external_reference_node(*node)) {
        return {
            SubWindowOpenTargetKind::ExternalReference,
            "library/" + bp.external_reference_path(*node) + ".blueprint"
        };
    }

    return {};
}

} // namespace editor
