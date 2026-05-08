#include "invariant_checker.h"

#include "blueprint_v2/interface/bridge_port_interface.h"
#include "blueprint_v2/interface/type_definition_interface.h"
#include "blueprint_v2/library/library_path.h"
#include "core/model/component_registry.h"
#include <unordered_set>

namespace {

static std::string iid_to_string(core::InternedId id) {
    return std::to_string(id.raw());
}

} // namespace

namespace bp2 {

InvariantChecker::Result InvariantChecker::validate(Blueprint const& bp,
                                                    PathArena const& arena,
                                                    const ::ComponentRegistry& parser_registry,
                                                    core::StringInterner& interner) {
    Result out;
    out.valid = false;

    std::unordered_set<core::InternedId> node_ids;
    for (auto const& n : bp.nodes()) {
        if (!node_ids.insert(n.semantic.id).second) {
            out.error = "duplicate node ID: " + iid_to_string(n.semantic.id);
            return out;
        }
    }

    std::unordered_set<core::InternedId> wire_ids;
    for (auto const& w : bp.wires()) {
        if (!wire_ids.insert(w.id).second) {
            out.error = "duplicate wire ID: " + iid_to_string(w.id);
            return out;
        }
    }

    // Blueprint instances are now node-owned; no separate nested collection.
    // Validate blueprint-instance nodes inline during node iteration below.

    for (auto const& node : bp.nodes()) {
        if (!parser_registry.has(std::string(interner.resolve(node.semantic.type)))) {
            // Blueprint-instance nodes can carry a user-given type name
            // that won't be in the library registry; authority is the source blueprint_id.
            if (node.is_blueprint_instance() || node.is_bridge_port()) {
                continue;
            }
            out.error = "unknown node type at node id=" + iid_to_string(node.semantic.id)
                + " type=" + iid_to_string(node.semantic.type);
            return out;
        }
    }

    for (auto const& node : bp.nodes()) {
        // Validate blueprint-instance nodes (node-owned sources)
        if (node.is_blueprint_instance()) {
            const auto& source = node.blueprint_instance().source;

            if (source.is_reference()) {
                const auto* def = parser_registry.get(std::string(interner.resolve(source.blueprint_id())));
                if (!def) {
                    out.error = "unknown referenced blueprint id=" + iid_to_string(node.semantic.id)
                        + " blueprint_id=" + iid_to_string(source.blueprint_id());
                    return out;
                }
            }

            // Issue #88 Gap #4: Validate embedded blueprints recursively
            if (source.is_embedded()) {
                const Blueprint* embedded_bp = source.inline_def();
                if (!embedded_bp) {
                    out.error = "blueprint-instance node embedded blueprint is null at node id="
                        + iid_to_string(node.semantic.id);
                    return out;
                }
                // Recursively validate embedded blueprint
                auto nested_result = InvariantChecker::validate(*embedded_bp, arena, parser_registry, interner);
                if (!nested_result.valid) {
                    out.error = "embedded blueprint at node id=" + iid_to_string(node.semantic.id)
                        + " contains invalid content: " + nested_result.error;
                    return out;
                }
            }

            continue;
        }
    }

    for (auto const& node : bp.nodes()) {
        if (node.is_bridge_port()) {
            const auto& bridge = node.bridge_port();
            if (bridge.exposed_port.empty()) {
                out.error = "bridge node missing exposed port at node id=" + iid_to_string(node.semantic.id);
                return out;
            }
            const auto exposed = bp.iface().find(bridge.exposed_port);
            if (!exposed.has_value()) {
                out.error = "bridge node exposed_port '" + iid_to_string(bridge.exposed_port)
                    + "' not found in blueprint interface at node id=" + iid_to_string(node.semantic.id);
                return out;
            }

            size_t matching_bridge_count = 0;
            for (auto const& candidate : bp.nodes()) {
                if (candidate.is_bridge_port()
                    && candidate.bridge_port().exposed_port == bridge.exposed_port) {
                    ++matching_bridge_count;
                }
            }
            if (matching_bridge_count != 1) {
                out.error = "duplicate bridge node exposed_port '" + iid_to_string(bridge.exposed_port)
                    + "' at node id=" + iid_to_string(node.semantic.id);
                return out;
            }

            const Interface bridge_iface = interface_from_bridge_port(
                bridge.direction, bridge.port_type, interner);
            const core::InternedId ext_id = interner.intern("ext");
            const core::InternedId port_id = interner.intern("port");
            const auto ext = bridge_iface.at(ext_id);
            const auto port = bridge_iface.at(port_id);
            const Domain expected_domain = domain_for_port_type(bridge.port_type);
            if (ext.domain != expected_domain || port.domain != expected_domain
                || ext.port_type != bridge.port_type || port.port_type != bridge.port_type) {
                out.error = "bridge node iface/type mismatch at node id=" + iid_to_string(node.semantic.id);
                return out;
            }
            const bool expected_input = bridge.direction == bp2::BridgeDirection::Input;
            if (ext.direction != (expected_input ? Direction::Input : Direction::Output)
                || port.direction != (expected_input ? Direction::Output : Direction::Input)) {
                out.error = "bridge node iface/direction mismatch at node id=" + iid_to_string(node.semantic.id);
                return out;
            }
            if (exposed->port_type != bridge.port_type
                || exposed->domain != expected_domain
                || exposed->direction != (expected_input ? Direction::Input : Direction::Output)) {
                out.error = "bridge node exposed_port authority mismatch at node id=" + iid_to_string(node.semantic.id);
                return out;
            }
            continue;
        }

        // Issue #88 Gap #5: Validate component node interfaces match registry.
        if (node.is_component()) {
            const std::string type_name(interner.resolve(node.semantic.type));
            const auto* def = parser_registry.get(type_name);
            if (!def) {
                // Type not found (already caught by earlier type check)
                continue;
            }
            Interface const expected = interface_from_type_definition(*def, interner);
            if (node.component().iface != expected) {
                out.error = "component node iface desynced from registry at node id="
                    + iid_to_string(node.semantic.id) + " type=" + iid_to_string(node.semantic.type);
                return out;
            }
        }
    }

    for (auto const& w : bp.wires()) {
        auto wr = WireValidator::validate(w, bp, parser_registry, interner);
        if (!wr.valid) {
            out.error = "wire id=" + iid_to_string(w.id) + ": " + wr.error;
            return out;
        }
    }

    out.valid = true;
    return out;
}

} // namespace bp2
