#include "subwindow_open_target.h"

#include "blueprint_v2/library/library_path.h"

namespace editor {

SubWindowOpenTarget resolve_subwindow_open_target(const bp2::Blueprint& bp,
                                                   ui::StringInterner& interner,
                                                   const bp2::LibraryIndex& library_index,
                                                   const std::string& sub_blueprint_id) {
     const auto lookup_id = interner.lookup(sub_blueprint_id);
     if (lookup_id.empty()) {
         return {};
     }

     const bp2::Blueprint::Node* node = bp.find_node(lookup_id);
     if (node && node->is_blueprint_instance()) {
         if (node->source && node->source->is_embedded()) {
             return {SubWindowOpenTargetKind::EmbeddedNested, {}};
         }

         if (node->source && node->source->is_reference()) {
             auto path = bp2::resolve_library_blueprint_path(
                 library_index,
                 std::string(interner.resolve(node->source->blueprint_id())));
             if (!path.has_value()) {
                 return {};
             }
             return {SubWindowOpenTargetKind::ReferencedNested, std::move(*path)};
         }
     }

     return {};
 }

} // namespace editor
