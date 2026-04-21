#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/path/path.h"
#include "editor/data/node_state.h"
#include "ui/core/interned_id.h"
#include <string>
#include <string_view>
#include <vector>
#include <cstddef>

struct ComponentRegistry;

namespace visual {
class Scene;
} // namespace visual

namespace visual::mutations {

/// Full rebuild: clear scene and recreate all widgets from bp2::Blueprint data.
/// @param registry  ComponentRegistry for resolving frame kind and content from the
///                  canonical source of truth (ComponentSpec). Must not be null.
void rebuild(Scene& scene, const bp2::Blueprint& bp,
             ui::StringInterner& interner,
             bp2::PathArena& arena,
             std::string_view scope_id,
             const ComponentRegistry& registry,
             const editor::RuntimeNodeStateStore* runtime_state_store = nullptr,
             const editor::SessionNodeAppearanceStore* appearance_store = nullptr);

} // namespace visual::mutations
