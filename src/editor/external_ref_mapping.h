#pragma once

#include "core/solvers/common/signal_key.h"

#include <string>
#include <string_view>

namespace editor {

/// Map a child blueprint's signal key to the parent simulation's signal key.
///
/// When a composite node (e.g. "firstorderlag_1") is expanded by the parser,
/// all child devices are prefixed: "firstorderlag_1:in", "firstorderlag_1:multiply", etc.
/// Port signal keys become "firstorderlag_1:in.ext", "firstorderlag_1:multiply.A", etc.
///
/// This function takes the child-local signal key (e.g. "in.ext") and the
/// parent instance id (e.g. "firstorderlag_1") and returns the full key
/// (e.g. "firstorderlag_1:in.ext") that the parent simulator uses.
///
/// @param parent_instance_id  The instance name in the parent blueprint (e.g. "firstorderlag_1")
/// @param child_signal_key    The signal key local to the child blueprint (e.g. "in.port", "multiply.A")
/// @return                    The mapped key for the parent simulator (e.g. "firstorderlag_1:in.port")
inline std::string resolve_external_ref_signal_key(
    std::string_view parent_instance_id,
    std::string_view child_signal_key) {
    return signal_key::make_child_scope_key(parent_instance_id, child_signal_key);
}

/// Build a child-local signal key from node_id and port_name.
/// Returns "node_id.port_name".
inline std::string build_signal_key(
    std::string_view node_id,
    std::string_view port_name) {
    return signal_key::make_node_port_key(node_id, port_name);
}

/// Map a root-level composite node's port to canonical runtime key.
///
/// When the parser expands a composite blueprint instance (e.g. "firstorderlag_1"),
/// parent-facing connections may be rewritten internally during expansion,
/// but public runtime lookup uses canonical "node.port" identity.
///
/// @param node_id   The expandable composite node id (e.g. "firstorderlag_1")
/// @param port_name The port name on the composite (e.g. "out")
/// @return          The canonical runtime signal key (e.g. "firstorderlag_1.out")
inline std::string map_composite_port_key(
    std::string_view node_id,
    std::string_view port_name) {
    return signal_key::make_node_port_key(node_id, port_name);
}

} // namespace editor
