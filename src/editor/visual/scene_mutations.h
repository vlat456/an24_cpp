#pragma once

/// Free functions for mutating Blueprint data and keeping visual::Scene in sync.
/// This is the only translation unit that includes concrete widget headers
/// (NodeFactory, Wire). All other code stays type-agnostic.

#include "data/blueprint.h"
#include <string>
#include <vector>
#include <cstddef>

namespace visual {
class Scene;
class Widget;
} // namespace visual

namespace visual::mutations {

/// Full rebuild: clear the scene and recreate all widgets from Blueprint data.
/// Only nodes/wires belonging to group_id are created.
void rebuild(Scene& scene, const Blueprint& bp, const std::string& group_id);

/// Add a node to Blueprint and create its widget in the scene.
/// Returns the index of the new node in bp.nodes.
size_t add_node(Scene& scene, Blueprint& bp, Node node,
                const std::string& group_id);

/// Remove nodes by index. Also removes connected wires and sub_blueprint_instances.
/// Removes corresponding widgets from the scene.
void remove_nodes(Scene& scene, Blueprint& bp,
                  const std::vector<size_t>& indices);

/// Add a wire to Blueprint (validated) and create its widget in the scene.
/// Returns true if the wire was accepted.
bool add_wire(Scene& scene, Blueprint& bp, ::Wire wire,
              const std::string& group_id);

/// Remove a wire by index. Removes the wire widget from the scene.
void remove_wire(Scene& scene, Blueprint& bp, size_t index);

/// Reconnect one end of a wire to a new port. Clears routing points.
/// Rebuilds the wire's widget.
void reconnect_wire(Scene& scene, Blueprint& bp,
                    size_t wire_idx, bool reconnect_start, WireEnd new_end,
                    const std::string& group_id);

/// Swap port connections for two wires on the same bus node.
/// Returns false if the swap was not possible.
bool swap_wire_ports_on_bus(Scene& scene, Blueprint& bp,
                            ui::InternedId bus_node_id,
                            ui::InternedId wire_id_a,
                            ui::InternedId wire_id_b);

/// Allocate a unique wire ID and intern it in the blueprint.
inline ui::InternedId next_wire_id(Blueprint& bp) {
    std::string id_str = "wire_" + std::to_string(bp.next_wire_id++);
    return bp.interner().intern(id_str);
}

} // namespace visual::mutations
