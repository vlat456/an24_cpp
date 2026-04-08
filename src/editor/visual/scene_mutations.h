#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/path/path.h"
#include "ui/core/interned_id.h"
#include <string>
#include <string_view>
#include <vector>
#include <cstddef>

namespace visual {
class Scene;
} // namespace visual

namespace visual::mutations {

/// Full rebuild: clear scene and recreate all widgets from bp2::Blueprint data.
void rebuild(Scene& scene, const bp2::Blueprint& bp,
             ui::StringInterner& interner,
             bp2::PathArena& arena,
             std::string_view scope_id);

} // namespace visual::mutations
