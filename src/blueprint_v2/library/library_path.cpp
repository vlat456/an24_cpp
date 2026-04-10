#include "library_path.h"

#include "library_index.h"

namespace bp2 {

std::optional<std::string> resolve_library_blueprint_path(const LibraryIndex& index,
                                                          const std::string& blueprint_id) {
    return index.resolve(blueprint_id);
}

} // namespace bp2
