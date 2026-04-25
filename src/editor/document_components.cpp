#include "document.h"

#include "core/model/presentation_spec.h"
#include "commands/commands.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "blueprint_v2/library/library_index.h"
#include "blueprint_v2/library/type_def_to_blueprint.h"
#include "common/port_type_utils.h"
#include "blueprint_v2/interface/type_definition_interface.h"
#include "core/solvers/common/signal_key.h"

#include "visual/persist.h"
#include "embedded_path_utils.h"
#include "parse_number.h"
#include "visual/snap.h"

#include <algorithm>
#include <functional>
#include <spdlog/spdlog.h>

namespace {

std::optional<bp2::BridgeDirection> bridge_direction_from_type_definition(
    const ComponentSpec& def) {
    if (!is_composite(def)) return std::nullopt;
    const auto* comp = as_composite(def);
    if (comp->ports.find("ext") == comp->ports.end() || comp->ports.find("port") == comp->ports.end()) {
        return std::nullopt;
    }

    auto it = comp->params.find("exposed_direction");
    if (it == comp->params.end()) {
        return std::nullopt;
    }
    if (it->second.default_value == "In") {
        return bp2::BridgeDirection::Input;
    }
    if (it->second.default_value == "Out") {
        return bp2::BridgeDirection::Output;
    }
    throw std::runtime_error("Invalid exposed_direction default for bridge component '" + spec_classname(def) + "'");
}

PortType parse_exposed_port_type(const std::string& s) {
    if (s == "V") return PortType::V;
    if (s == "I") return PortType::I;
    if (s == "Signal") return PortType::Signal;
    if (s == "Bool") return PortType::Bool;
    if (s == "RPM") return PortType::RPM;
    if (s == "Temperature") return PortType::Temperature;
    if (s == "Pressure") return PortType::Pressure;
    if (s == "Position") return PortType::Position;
    if (s == "Contextual") return PortType::Contextual;
    if (s == "Any") return PortType::Any;
    throw std::runtime_error("Invalid exposed_type bridge param '" + s + "'");
}

/// Update the embedded blueprint-instance's inline blueprint interface to include a new bridge port.
/// Single-pass: mutate_embedded walks the path once, the lambda receives the inline blueprint directly.
void add_bridge_port_to_composite(
    bp2::EditorModel& model,
    ui::StringInterner& interner,
    const WindowScopeId& scope_id,
    const std::string& iface_name,
    bool is_input_bridge,
    PortType port_type)
{
    assert(scope_id.is_embedded());
    const ui::InternedId iface_iid = interner.intern(iface_name);
    const Domain domain = editor::common::domain_for_port_type(port_type);

    bp2::PortDescriptor pd;
    pd.name = iface_iid;
    pd.domain = domain;
    pd.direction = is_input_bridge ? bp2::Direction::Input : bp2::Direction::Output;
    pd.port_type = port_type;

    // scope_id.path() already returns InternedId vector - use directly
    const bp2::MutationResult result = model.mutate_embedded(scope_id.path(),
        [&](const bp2::Blueprint& inner) -> bp2::Blueprint {
            auto ports = inner.iface().ports();
            ports.push_back(pd);
            return inner.with_interface(bp2::Interface(std::move(ports)));
        });

    if (result == bp2::MutationResult::NotFound) {
        spdlog::warn("[editor] add_bridge_port: embedded blueprint instance at '{}' not found",
                     editor::instance_path_to_scope_string(interner, scope_id.path()));
    }
}

} // namespace

void Document::addComponent(const std::string& classname, Pt world_pos,
                            const WindowScopeId& scope_id,
                            ComponentRegistry& registry)
{
    if (!registry.has(classname)) {
        spdlog::error("[editor] Unknown component classname '{}'", classname);
        return;
    }

    const auto* def = registry.get(classname);
    if (!def) {
        spdlog::error("[editor] Component definition not found for '{}'", classname);
        return;
    }
    if (const auto* comp = as_composite(*def)) {
        addBlueprint(classname, world_pos, scope_id, registry);
        return;
    }

    std::string unique_id = model_.generate_unique_node_id(classname, interner_);
    Pt snapped_pos = editor_math::snap_to_grid(world_pos, viewport().grid_step);

    bp2::Blueprint::Node node;
    node.content = bp2::Blueprint::Node::ComponentData{};
    node.semantic.id = interner_.intern(unique_id);
    node.semantic.type = interner_.intern(classname);
    node.view.name = unique_id;
    node.layout.x = snapped_pos.x;
    node.layout.y = snapped_pos.y;
    std::vector<bp2::PortDescriptor> iface_ports;
    const auto& def_ports = spec_ports(*def);
    iface_ports.reserve(def_ports.size());
    for (const auto& [port_name, port_def] : def_ports) {
        auto pid = interner_.intern(port_name);
        bp2::PortDescriptor pd;
        pd.name = pid;
        pd.domain = port_def.domain;
        if (port_def.direction == bp2::Direction::Input) {
            pd.direction = bp2::Direction::Input;
        } else if (port_def.direction == bp2::Direction::Output) {
            pd.direction = bp2::Direction::Output;
        } else {
            pd.direction = bp2::Direction::InOut;
        }
        pd.port_type = port_def.type;
        iface_ports.push_back(std::move(pd));
    }
    node.component().iface = bp2::Interface(std::move(iface_ports));

    for (const auto& [k, v] : spec_params(*def)) {
        float parsed = 0.0f;
        if (locale_safe::parse_float(v.default_value, parsed)) {
            node.semantic.params[interner_.intern(k)] = parsed;
        } else {
            node.semantic.string_params[k] = v.default_value;
        }
    }

    const auto bridge_direction = bridge_direction_from_type_definition(*def);
    const bool is_bridge = bridge_direction.has_value();
    const bool bridge_in_group = is_bridge && scope_id.is_embedded()
        && editor::embedded_path_exists(model_.current(), scope_id.path());
    const std::string bridge_iface_name = bridge_in_group ? node.view.name : "";
    PortType bridge_port_type = PortType::Contextual;

    if (is_bridge) {
        std::string canonical_id = signal_key::make_child_scope_key(
            editor::instance_path_to_scope_string(interner_, scope_id.path()), node.view.name);
        if (!scope_id.is_root()) {
            node.semantic.id = interner_.intern(canonical_id);
            unique_id = canonical_id;
        }

        PortType pt = PortType::Contextual;
        auto et_it = node.semantic.string_params.find("exposed_type");
        if (et_it != node.semantic.string_params.end()) {
            pt = parse_exposed_port_type(et_it->second);
        }
        bridge_port_type = pt;
        const bool is_input_bridge = *bridge_direction == bp2::BridgeDirection::Input;
        node.content = bp2::Blueprint::Node::BridgePortData{
            interner_.intern(node.view.name),
            *bridge_direction,
            pt,
        };
        node.semantic.type = interner_.intern("BridgePort");
        node.semantic.string_params.erase("exposed_direction");
        node.semantic.string_params.erase("exposed_type");
    }

    const bool bridge_is_input = bridge_direction == bp2::BridgeDirection::Input;

    const bp2::Blueprint before_add = model_.current();
#ifndef NDEBUG
    std::string before_integrity_err;
    if (!type_registry_) {
        spdlog::error("[editor] ComponentRegistry is not configured on Document::addComponent");
        return;
    }
    const ComponentRegistry& parser_registry = *type_registry_;
    const bool before_integrity_ok =
        validate_blueprint_integrity(before_add, interner_, arena_, parser_registry, &before_integrity_err);
#endif
    try {
        model_.mutate_atomically([&] {
            execute(model_, interner_, cmd_add_node(std::move(node)));

            if (bridge_in_group) {
                add_bridge_port_to_composite(
                    model_, interner_, scope_id,
                    bridge_iface_name, bridge_is_input, bridge_port_type);
            }
        });

#ifndef NDEBUG
        {
            std::string err;
            if (!validate_blueprint_integrity(model_.current(), interner_, arena_, parser_registry, &err)) {
#ifndef NDEBUG
                if (!before_integrity_ok) {
                    spdlog::warn(
                        "[editor] addComponent('{}') on pre-invalid blueprint: before='{}', after='{}'",
                        classname,
                        before_integrity_err,
                        err);
                } else
#endif
                {
                    model_.replace_current(before_add);
                    spdlog::error("[editor] addComponent('{}') rolled back due to integrity failure: {}", classname, err);
                    return;
                }
            }
        }
#endif

        resetNodeContent(registry);
        rebuildAllWindows();

        spdlog::info("[editor] Added component: {} (id={}) at ({:.1f}, {:.1f}) scope={}",
            classname, unique_id, snapped_pos.x, snapped_pos.y,
            scope_id.is_root() ? "root" : editor::instance_path_to_scope_string(interner_, scope_id.path()));
    } catch (const std::exception& e) {
        model_.replace_current(before_add);
        spdlog::error("[editor] addComponent('{}') failed safely: {}", classname, e.what());
        return;
    }
}

void Document::addBlueprint(const std::string& blueprint_name, Pt world_pos,
                            const WindowScopeId& scope_id,
                            ComponentRegistry& registry)
{
    if (!registry.has(blueprint_name)) {
        spdlog::error("[editor] Unknown blueprint '{}'", blueprint_name);
        return;
    }

    const auto* def = registry.get(blueprint_name);
    if (!def || is_primitive(*def)) {
        spdlog::error("[editor] '{}' is not a composite blueprint", blueprint_name);
        return;
    }
    const auto* comp_def = as_composite(*def);
    if (!comp_def || comp_def->devices.empty()) {
        spdlog::error("[editor] '{}' is not a composite blueprint", blueprint_name);
        return;
    }
    const std::string unique_id = model_.generate_unique_node_id(blueprint_name, interner_);
    const Pt snapped_pos = editor_math::snap_to_grid(world_pos, viewport().grid_step);

    bp2::Blueprint::Node collapsed;
    collapsed.semantic.id = interner_.intern(unique_id);
    collapsed.semantic.type = interner_.intern(blueprint_name);
    collapsed.view.name = unique_id;
    collapsed.layout.x = snapped_pos.x;
    collapsed.layout.y = snapped_pos.y;
    collapsed.layout.width = 160.0f;
    collapsed.layout.height = 64.0f;
    collapsed.layout.collapsed = true;

    std::vector<bp2::PortDescriptor> iface_ports;
    const auto& def_ports = spec_ports(*def);
    iface_ports.reserve(def_ports.size());

    for (const auto& [port_name, port_def] : def_ports) {
        const ui::InternedId pid = interner_.intern(port_name);
        bp2::PortDescriptor pd = bp2::port_descriptor_from_type_port(pid, port_def);
        pd.domain = port_def.domain;
        iface_ports.push_back(std::move(pd));
    }
    // Issue #91: Blueprint-instance interface derives from source authority only.
    // Store port descriptors temporarily to set on inline blueprint interface.
    bp2::Interface inline_bp_iface = bp2::Interface(std::move(iface_ports));

    bp2::Blueprint loaded;
    try {
        loaded = bp2::blueprint_from_type_definition(*def, interner_, registry);
    } catch (const std::exception& e) {
        spdlog::error("[editor] addBlueprint('{}'): failed to build from ComponentSpec: {}",
                      blueprint_name, e.what());
        return;
    }

    spdlog::debug("[editor] addBlueprint('{}'): built {} nodes, {} wires from ComponentSpec",
        blueprint_name,
        loaded.nodes().size(),
        loaded.wires().size());

    bp2::Blueprint inline_bp = loaded.with_interface(inline_bp_iface);
    inline_bp = inline_bp.with_id(interner_.intern(blueprint_name));
    inline_bp = inline_bp.with_name(spec_classname(*def));
    collapsed.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            std::make_unique<bp2::Blueprint>(std::move(inline_bp)))
    };

    const bp2::Blueprint before_add = model_.current();
#ifndef NDEBUG
    std::string before_integrity_err;
    if (!type_registry_) {
        spdlog::error("[editor] ComponentRegistry is not configured on Document::addBlueprint");
        return;
    }
    const ComponentRegistry& parser_registry = *type_registry_;
    const bool before_integrity_ok =
        validate_blueprint_integrity(before_add, interner_, arena_, parser_registry, &before_integrity_err);
#endif
    try {
        model_.mutate_atomically([&] {

            if (scope_id.is_root()) {
                execute(model_, interner_, cmd_add_node(std::move(collapsed)));
                return;
            }

            // Walk the full scope path to add the node inside the embedded blueprint.
            // scope_id.path() already returns InternedId vector - use directly
            const bp2::MutationResult mr = model_.mutate_embedded(scope_id.path(),
                [&](const bp2::Blueprint& inner) -> bp2::Blueprint {
                    return inner.with_node(collapsed);
                });
            if (mr == bp2::MutationResult::NotFound) {
                throw std::runtime_error("embedded blueprint instance at '" + editor::instance_path_to_scope_string(interner_, scope_id.path()) + "' not found");
            }
        });

#ifndef NDEBUG
        {
            std::string err;
            if (!validate_blueprint_integrity(model_.current(), interner_, arena_, parser_registry, &err)) {
#ifndef NDEBUG
                if (!before_integrity_ok) {
                    spdlog::warn(
                        "[editor] addBlueprint('{}') on pre-invalid blueprint: before='{}', after='{}'",
                        blueprint_name,
                        before_integrity_err,
                        err);
                } else
#endif
                {
                    model_.replace_current(before_add);
                    spdlog::error("[editor] addBlueprint('{}') rolled back due to integrity failure: {}",
                                  blueprint_name, err);
                    return;
                }
            }
        }
#endif

        resetNodeContent(registry);
        rebuildAllWindows();
        spdlog::info("[editor] Added blueprint: {} (id={}) at ({:.1f}, {:.1f}) scope={}",
            blueprint_name, unique_id, snapped_pos.x, snapped_pos.y,
            scope_id.is_root() ? "root" : editor::instance_path_to_scope_string(interner_, scope_id.path()));
    } catch (const std::exception& e) {
        model_.replace_current(before_add);
        spdlog::error("[editor] addBlueprint('{}') failed safely: {}", blueprint_name, e.what());
    }
}
