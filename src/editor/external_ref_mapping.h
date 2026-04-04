#pragma once

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
    std::string result;
    result.reserve(parent_instance_id.size() + 1 + child_signal_key.size());
    result.append(parent_instance_id);
    result.push_back(':');
    result.append(child_signal_key);
    return result;
}

/// Build a child-local signal key from node_id and port_name.
/// Returns "node_id.port_name".
inline std::string build_signal_key(
    std::string_view node_id,
    std::string_view port_name) {
    std::string result;
    result.reserve(node_id.size() + 1 + port_name.size());
    result.append(node_id);
    result.push_back('.');
    result.append(port_name);
    return result;
}

/// Map a root-level composite node's port signal key to the expanded runtime key.
///
/// When the parser expands a composite blueprint instance (e.g. "firstorderlag_1"),
/// parent-facing connections are rewritten:
///   "firstorderlag_1.out" → "firstorderlag_1:out.ext"
///
/// The root-level UI still sees node_id="firstorderlag_1" and port="out",
/// building key "firstorderlag_1.out" — which doesn't exist at runtime.
/// This function rewrites to the correct expanded key.
///
/// @param node_id   The expandable composite node id (e.g. "firstorderlag_1")
/// @param port_name The port name on the composite (e.g. "out")
/// @return          The runtime signal key (e.g. "firstorderlag_1:out.ext")
inline std::string map_composite_port_key(
    std::string_view node_id,
    std::string_view port_name) {
    // Pattern: "node_id:port_name.ext"
    std::string result;
    result.reserve(node_id.size() + 1 + port_name.size() + 4);
    result.append(node_id);
    result.push_back(':');
    result.append(port_name);
    result.append(".ext");
    return result;
}

} // namespace editor
