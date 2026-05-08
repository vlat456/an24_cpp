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
class Widget;
class Wire;
struct BusWireRef;
} // namespace visual

namespace visual::mutations {

// ============================================================================
// Incremental widget creation helpers
// ============================================================================

/// Build the list of bus-wire references from a blueprint's wires.
/// Used by BusNodeWidget alias port construction.
std::vector<BusWireRef> build_bus_wires(const bp2::Blueprint& bp,
                                         const bp2::PathArena& arena);

/// Create a single node widget from bp2::Blueprint::Node data.
/// Does NOT add to scene — caller must call scene.add().
std::unique_ptr<Widget> create_node_widget(const bp2::Blueprint::Node& n,
                                            const bp2::Blueprint& bp,
                                            core::StringInterner& interner,
                                            bp2::PathArena& arena,
                                            std::span<const core::InternedId> instance_path,
                                            const ComponentRegistry& registry,
                                            const editor::RuntimeNodeStateStore* runtime_state_store,
                                            const editor::IconFont* icon_font,
                                            const std::vector<BusWireRef>& bus_wires);

/// Create a single wire widget from bp2::Blueprint::Wire data.
/// Resolves ports in the existing scene. Does NOT add to scene — caller must call scene.add().
std::unique_ptr<Wire> create_wire_widget(const bp2::Blueprint::Wire& w,
                                          const bp2::PathArena& arena,
                                          const core::StringInterner& interner,
                                          const Scene& scene);

/// Orient single-port ref/value nodes toward their connected node.
/// Call after adding/removing nodes or wires that affect ref node connectivity.
void orient_ref_node_ports(Scene& scene,
                            const bp2::Blueprint& bp,
                            const bp2::PathArena& arena,
                            const core::StringInterner& interner,
                            std::span<const core::InternedId> instance_path,
                            const ComponentRegistry& registry);

// ============================================================================
// Full rebuild
// ============================================================================

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
