#include "document.h"

#include "blueprint_view_hydration.h"
#include "presentation_spec.h"
#include "commands/commands.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "blueprint_v2/library/library_index.h"
#include "blueprint_v2/library/type_def_to_blueprint.h"
#include "common/port_type_utils.h"
#include "blueprint_v2/interface/type_definition_interface.h"
#include "core/solvers/common/signal_key.h"
#include "debug.h"
#include "visual/persist.h"
#include "parse_number.h"
#include "visual/snap.h"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace {

std::optional<bp2::Blueprint::Node::BridgePortSide> bridge_side_from_type_definition(
    const TypeDefinition& def) {
    if (def.ports.find("ext") == def.ports.end() || def.ports.find("port") == def.ports.end()) {
        return std::nullopt;
    }

    auto it = def.params.find("exposed_direction");
    if (it == def.params.end()) {
        return std::nullopt;
    }
    if (it->second.default_value == "In") {
        return bp2::Blueprint::Node::BridgePortSide::Input;
    }
    if (it->second.default_value == "Out") {
        return bp2::Blueprint::Node::BridgePortSide::Output;
    }
    return std::nullopt;
}

bp2::Interface make_bridge_iface(ui::StringInterner& interner,
                                 bool is_input_bridge,
                                 PortType port_type) {
    const Domain domain = editor::common::domain_for_port_type(port_type);
    if (is_input_bridge) {
        return bp2::Interface({
            bp2::PortDescriptor{interner.intern("ext"), domain, bp2::Direction::Input, port_type},
            bp2::PortDescriptor{interner.intern("port"), domain, bp2::Direction::Output, port_type},
        });
    }
    return bp2::Interface({
        bp2::PortDescriptor{interner.intern("port"), domain, bp2::Direction::Input, port_type},
        bp2::PortDescriptor{interner.intern("ext"), domain, bp2::Direction::Output, port_type},
    });
}

PortType parse_exposed_port_type(const std::string& s) {
    if (s == "V") return PortType::V;
    if (s == "I") return PortType::I;
    if (s == "Bool") return PortType::Bool;
    if (s == "RPM") return PortType::RPM;
    if (s == "Temperature") return PortType::Temperature;
    if (s == "Pressure") return PortType::Pressure;
    if (s == "Position") return PortType::Position;
    if (s == "Contextual") return PortType::Contextual;
    return PortType::Contextual;
}

/// Update the embedded blueprint-instance's inline blueprint interface to include a new bridge port.
void add_bridge_port_to_composite(
    bp2::EditorModel& model,
    ui::StringInterner& interner,
    const std::string& scope_id,
    const std::string& iface_name,
    bool is_input_bridge,
    PortType port_type)
{
    const ui::InternedId group_iid = interner.intern(scope_id);
    const ui::InternedId iface_iid = interner.intern(iface_name);
    const Domain domain = editor::common::domain_for_port_type(port_type);

    bp2::PortDescriptor pd;
    pd.name = iface_iid;
    pd.domain = domain;
    pd.direction = is_input_bridge ? bp2::Direction::Input : bp2::Direction::Output;
    pd.port_type = port_type;

    bp2::Blueprint bp = model.current();

    const auto* node = bp.find_node(group_iid);
    if (!node || !node->has_embedded_blueprint()) {
        spdlog::warn("[editor] add_bridge_port: embedded blueprint instance '{}' not found", scope_id);
        return;
    }

    // Extract, mutate, and restore the embedded blueprint's interface
    std::vector<bp2::PortDescriptor> ports = node->blueprint_instance().source.inline_def()->iface().ports();
    ports.push_back(pd);
    bp2::Blueprint updated_inline = node->blueprint_instance().source.inline_def()->with_interface(bp2::Interface(std::move(ports)));

    // Create updated node with mutated embedded blueprint
    bp2::Blueprint::Node updated_node = *node;
    updated_node.blueprint_instance().source.set_inline_def(std::make_unique<bp2::Blueprint>(std::move(updated_inline)));

    bp = bp2::replace_node_preserve_order(bp, std::move(updated_node));
    model.replace_current(std::move(bp));
}

} // namespace

void Document::addComponent(const std::string& classname, Pt world_pos,
                            const std::string& scope_id,
                            TypeRegistry& registry)
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
    const auto* pres = registry.presentation.get(classname);

    if (!def->cpp_class && !def->devices.empty()) {
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
    iface_ports.reserve(def->ports.size());
    for (const auto& [port_name, port_def] : def->ports) {
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

    for (const auto& [k, v] : def->params) {
        float parsed = 0.0f;
        if (locale_safe::parse_float(v.default_value, parsed)) {
            node.semantic.params[interner_.intern(k)] = parsed;
        } else {
            node.semantic.string_params[k] = v.default_value;
        }
    }

    const auto bridge_side = bridge_side_from_type_definition(*def);
    const bool is_bridge = bridge_side.has_value();
    const bool bridge_in_group = is_bridge && !scope_id.empty()
        && model_.current().find_node(interner_.intern(scope_id)) != nullptr;

    if (is_bridge) {
        std::string canonical_id = signal_key::make_child_scope_key(scope_id, node.view.name);
        if (!scope_id.empty()) {
            node.semantic.id = interner_.intern(canonical_id);
            unique_id = canonical_id;
        }

        PortType pt = PortType::Contextual;
        auto et_it = node.semantic.string_params.find("exposed_type");
        if (et_it != node.semantic.string_params.end()) {
            pt = parse_exposed_port_type(et_it->second);
        }
        const bool is_input_bridge = *bridge_side == bp2::Blueprint::Node::BridgePortSide::Input;
        node.content = bp2::Blueprint::Node::BridgePortData{
            interner_.intern(node.view.name),
            *bridge_side,
            pt,
            make_bridge_iface(interner_, is_input_bridge, pt),
        };
        node.semantic.type = interner_.intern("BridgePort");
        node.semantic.string_params.erase("exposed_direction");
        node.semantic.string_params.erase("exposed_type");
    }

    // Issue #105/#133: hydrate static semantics + initial dynamic defaults.
    editor::hydrate_node_view_full(node, def, pres, interner_);

    const std::string bridge_iface_name = bridge_in_group ? node.view.name : "";
    const bool bridge_is_input = bridge_side == bp2::Blueprint::Node::BridgePortSide::Input;
    PortType bridge_port_type = PortType::Contextual;
    if (is_bridge) {
        auto et_it = node.semantic.string_params.find("exposed_type");
        if (et_it != node.semantic.string_params.end()) {
            bridge_port_type = parse_exposed_port_type(et_it->second);
        }
    }

    const bp2::Blueprint before_add = model_.current();
#ifndef NDEBUG
    std::string before_integrity_err;
    if (!type_registry_) {
        spdlog::error("[editor] TypeRegistry is not configured on Document::addComponent");
        return;
    }
    const TypeRegistry& parser_registry = *type_registry_;
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

        rebuildAllWindows();

        spdlog::info("[editor] Added component: {} (id={}) at ({:.1f}, {:.1f}) group={}",
            classname, unique_id, snapped_pos.x, snapped_pos.y,
            scope_id.empty() ? "root" : scope_id);
    } catch (const std::exception& e) {
        model_.replace_current(before_add);
        spdlog::error("[editor] addComponent('{}') failed safely: {}", classname, e.what());
        return;
    }
}

void Document::addBlueprint(const std::string& blueprint_name, Pt world_pos,
                            const std::string& scope_id,
                            TypeRegistry& registry)
{
    if (!registry.has(blueprint_name)) {
        spdlog::error("[editor] Unknown blueprint '{}'", blueprint_name);
        return;
    }

    const auto* def = registry.get(blueprint_name);
    if (!def || def->cpp_class || def->devices.empty()) {
        spdlog::error("[editor] '{}' is not a composite blueprint", blueprint_name);
        return;
    }
    const auto* pres = registry.presentation.get(blueprint_name);

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
    iface_ports.reserve(def->ports.size());

    for (const auto& [port_name, port_def] : def->ports) {
        const ui::InternedId pid = interner_.intern(port_name);
        bp2::PortDescriptor pd = bp2::port_descriptor_from_type_port(pid, port_def);
        pd.domain = port_def.domain;
        iface_ports.push_back(std::move(pd));
    }
    // Issue #91: Blueprint-instance interface derives from source authority only.
    // Store port descriptors temporarily to set on inline blueprint interface.
    bp2::Interface inline_bp_iface = bp2::Interface(std::move(iface_ports));

    // Issue #105/#133: hydrate static semantics + initial dynamic defaults.
    editor::hydrate_node_view_full(collapsed, def, pres, interner_);

    bp2::Blueprint loaded;
    try {
        loaded = bp2::blueprint_from_type_definition(*def, interner_, registry);
    } catch (const std::exception& e) {
        spdlog::error("[editor] addBlueprint('{}'): failed to build from TypeDefinition: {}",
                      blueprint_name, e.what());
        return;
    }

    spdlog::debug("[editor] addBlueprint('{}'): built {} nodes, {} wires from TypeDefinition",
        blueprint_name,
        loaded.nodes().size(),
        loaded.wires().size());

    loaded = editor::hydrate_runtime_node_view_data(std::move(loaded), interner_, registry);

    bp2::Blueprint inline_bp = loaded.with_interface(inline_bp_iface);
    inline_bp = inline_bp.with_id(interner_.intern(blueprint_name));
    inline_bp = inline_bp.with_name(def->classname);
    collapsed.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            std::make_unique<bp2::Blueprint>(std::move(inline_bp)))
    };

    const bp2::Blueprint before_add = model_.current();
#ifndef NDEBUG
    std::string before_integrity_err;
    if (!type_registry_) {
        spdlog::error("[editor] TypeRegistry is not configured on Document::addBlueprint");
        return;
    }
    const TypeRegistry& parser_registry = *type_registry_;
    const bool before_integrity_ok =
        validate_blueprint_integrity(before_add, interner_, arena_, parser_registry, &before_integrity_err);
#endif
    try {
        model_.mutate_atomically([&] {

            if (scope_id.empty()) {
                execute(model_, interner_, cmd_add_node(std::move(collapsed)));
                return;
            }

            const ui::InternedId scope_iid = interner_.intern(scope_id);
            const auto* host_node = model_.current().find_node(scope_iid);
            if (!host_node || !host_node->has_embedded_blueprint() || !host_node->blueprint_instance().source.inline_def()) {
                throw std::runtime_error("embedded blueprint instance '" + scope_id + "' not found");
            }

            bp2::Blueprint next_inline = host_node->blueprint_instance().source.inline_def()->with_node(collapsed);
            bp2::Blueprint::Node updated_host = *host_node;
            updated_host.blueprint_instance().source.set_inline_def(std::make_unique<bp2::Blueprint>(std::move(next_inline)));
            model_.replace_current(bp2::replace_node_preserve_order(model_.current(), std::move(updated_host)));
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

        rebuildAllWindows();
        spdlog::info("[editor] Added blueprint: {} (id={}) at ({:.1f}, {:.1f}) group={}",
            blueprint_name, unique_id, snapped_pos.x, snapped_pos.y,
            scope_id.empty() ? "root" : scope_id);
    } catch (const std::exception& e) {
        model_.replace_current(before_add);
        spdlog::error("[editor] addBlueprint('{}') failed safely: {}", blueprint_name, e.what());
    }
}
