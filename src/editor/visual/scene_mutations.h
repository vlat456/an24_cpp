#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/path/path.h"
#include "editor/data/node_state.h"
#include "editor/visual/presentation/node_badge.h"
#include "core/strings/interned_id.h"
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
/// @param icon_font  Icon font for node badge rendering (may be nullptr if FA not loaded).
void rebuild(Scene& scene, const bp2::Blueprint& bp,
             core::StringInterner& interner,
             bp2::PathArena& arena,
             std::span<const core::InternedId> instance_path,
             const ComponentRegistry& registry,
             const editor::RuntimeNodeStateStore* runtime_state_store = nullptr,
             const editor::IconFont* icon_font = nullptr);

} // namespace visual::mutations
