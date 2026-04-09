#include "document.h"

#include "commands/commands.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "common/port_type_utils.h"
#include "blueprint_v2/interface/type_definition_interface.h"
#include "core/solvers/common/signal_key.h"
#include "data/node_content.h"
#include "debug.h"
#include "visual/persist.h"
#include "parse_number.h"
#include "visual/snap.h"

#include <algorithm>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace {

PortType parse_exposed_port_type(const std::string& s) {
    if (s == "V") return PortType::V;
    if (s == "I") return PortType::I;
    if (s == "Bool") return PortType::Bool;
    if (s == "RPM") return PortType::RPM;
    if (s == "Temperature") return PortType::Temperature;
    if (s == "Pressure") return PortType::Pressure;
    if (s == "Position") return PortType::Position;
    return PortType::V;
}

/// Update the embedded nested interface to include a new bridge port, then derive
/// the collapsed node interface from that authoritative nested iface.
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

    const auto* nested = bp.find_nested(group_iid);
    if (!nested || !nested->is_embedded() || !nested->inline_def()) {
        spdlog::warn("[editor] add_bridge_port: embedded nested '{}' not found", scope_id);
        return;
    }

    bp2::Blueprint::Nested n = *nested;
    std::vector<bp2::PortDescriptor> ports = n.inline_def()->iface().ports();
    ports.push_back(pd);
    bp2::Blueprint updated_inline = n.inline_def()->with_interface(bp2::Interface(std::move(ports)));
    n.set_inline_def(std::make_unique<bp2::Blueprint>(std::move(updated_inline)));
    bp = bp2::replace_nested_preserve_order(bp, std::move(n));
    bp = bp2::sync_collapsed_node_iface_from_nested(bp, group_iid);

    if (!bp2::composite_iface_matches_nested(bp, group_iid)) {
        throw std::logic_error("add_bridge_port_to_composite: collapsed iface desynced from nested authority");
    }

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

    if (!def->cpp_class && !def->devices.empty()) {
        addBlueprint(classname, world_pos, scope_id, registry);
        return;
    }

    std::string unique_id = model_.generate_unique_node_id(classname, interner_);
    Pt snapped_pos = editor_math::snap_to_grid(world_pos, model_.current().grid_step());

    bp2::Blueprint::Node node;
    node.semantic.id = interner_.intern(unique_id);
    node.semantic.type = interner_.intern(classname);
    node.view.name = unique_id;
    node.layout.x = snapped_pos.x;
    node.layout.y = snapped_pos.y;
    node.semantic.owner_scope = scope_id;
    node.view.render_hint = def->render_hint;
    node.view.expandable = !def->cpp_class && !def->devices.empty();

    std::vector<bp2::PortDescriptor> iface_ports;
    iface_ports.reserve(def->ports.size());
    for (const auto& [port_name, port_def] : def->ports) {
        auto pid = interner_.intern(port_name);
        bp2::PortDescriptor pd;
        pd.name = pid;
        pd.domain = port_def.domain;
        if (port_def.direction == PortDirection::In) {
            pd.direction = bp2::Direction::Input;
        } else if (port_def.direction == PortDirection::Out) {
            pd.direction = bp2::Direction::Output;
        } else {
            pd.direction = bp2::Direction::InOut;
        }
        pd.port_type = port_def.type;
        iface_ports.push_back(std::move(pd));
    }
    node.semantic.iface = bp2::Interface(std::move(iface_ports));

    for (const auto& [k, v] : def->params) {
        float parsed = 0.0f;
        if (locale_safe::parse_float(v, parsed)) {
            node.semantic.params[interner_.intern(k)] = parsed;
        } else {
            node.semantic.string_params[k] = v;
        }
    }

    const bool is_bridge = (classname == "BlueprintInput" || classname == "BlueprintOutput");
    const bool bridge_in_group = is_bridge && !scope_id.empty()
        && model_.current().find_node(interner_.intern(scope_id)) != nullptr;

    if (bridge_in_group) {
        std::string canonical_id = signal_key::make_child_scope_key(scope_id, node.view.name);
        node.semantic.id = interner_.intern(canonical_id);
        unique_id = canonical_id;

        PortType pt = PortType::V;
        auto et_it = node.semantic.string_params.find("exposed_type");
        if (et_it != node.semantic.string_params.end()) {
            pt = parse_exposed_port_type(et_it->second);
        }
        std::vector<bp2::PortDescriptor> ports = node.semantic.iface.ports();
        for (auto& pd : ports) {
            pd.port_type = pt;
            pd.domain = editor::common::domain_for_port_type(pt);
        }
        node.semantic.iface = bp2::Interface(std::move(ports));
    }

    {
        NodeContent nc = create_node_content_from_def(def);
        node.view.content_type = nc.type;
        node.view.content_label = nc.label;
        node.view.content_value = nc.value;
        node.view.content_min = nc.min;
        node.view.content_max = nc.max;
        node.view.content_unit = nc.unit;
        node.view.content_state = nc.state;
        node.view.content_tripped = nc.tripped;
    }

    const std::string bridge_iface_name = bridge_in_group ? node.view.name : "";
    const bool bridge_is_input = (classname == "BlueprintInput");
    PortType bridge_port_type = PortType::V;
    if (bridge_in_group) {
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

    const std::string unique_id = model_.generate_unique_node_id(blueprint_name, interner_);
    const Pt snapped_pos = editor_math::snap_to_grid(world_pos, model_.current().grid_step());

    bp2::Blueprint::Node collapsed;
    collapsed.semantic.id = interner_.intern(unique_id);
    collapsed.semantic.type = interner_.intern(blueprint_name);
    collapsed.view.name = unique_id;
    collapsed.semantic.owner_scope = scope_id;
    collapsed.layout.x = snapped_pos.x;
    collapsed.layout.y = snapped_pos.y;
    collapsed.layout.width = 160.0f;
    collapsed.layout.height = 64.0f;
    collapsed.view.expandable = true;
    collapsed.layout.collapsed = true;
    collapsed.view.blueprint_path = blueprint_name;
    collapsed.view.render_hint = def->render_hint;

    std::vector<bp2::PortDescriptor> iface_ports;
    iface_ports.reserve(def->ports.size());

    for (const auto& [port_name, port_def] : def->ports) {
        const ui::InternedId pid = interner_.intern(port_name);
        bp2::PortDescriptor pd = bp2::port_descriptor_from_type_port(pid, port_def);
        pd.domain = port_def.domain;
        iface_ports.push_back(std::move(pd));
    }
    collapsed.semantic.iface = bp2::Interface(std::move(iface_ports));

    {
        NodeContent nc = create_node_content_from_def(def);
        collapsed.view.content_type = nc.type;
        collapsed.view.content_label = nc.label;
        collapsed.view.content_value = nc.value;
        collapsed.view.content_min = nc.min;
        collapsed.view.content_max = nc.max;
        collapsed.view.content_unit = nc.unit;
        collapsed.view.content_state = nc.state;
        collapsed.view.content_tripped = nc.tripped;
    }

    std::string library_path = "library/";
    {
        std::filesystem::path lp(library_path);
        if (!std::filesystem::exists(lp) && lp.is_relative()) {
            std::vector<std::filesystem::path> try_paths = {
                lp, "../" / lp, "../../" / lp, "../../../" / lp,
            };
            for (const auto& p : try_paths) {
                if (std::filesystem::exists(p)) {
                    library_path = p.string();
                    break;
                }
            }
        }
    }

    std::string category = registry.categories.count(blueprint_name)
        ? registry.categories.at(blueprint_name) : "";
    std::filesystem::path blueprint_file = std::filesystem::path(library_path);
    if (!category.empty()) {
        blueprint_file /= category;
    }
    blueprint_file /= (blueprint_name + ".blueprint");

    auto loaded_opt = load_blueprint_from_file_validated(
        blueprint_file.string().c_str(), interner_, arena_, registry);

    if (!loaded_opt) {
        spdlog::error("[editor] addBlueprint('{}'): could not load '{}'",
            blueprint_name, blueprint_file.string());
        return;
    }

    spdlog::debug("[editor] addBlueprint('{}'): loaded {} nodes, {} wires from '{}'",
        blueprint_name,
        loaded_opt->nodes().size(),
        loaded_opt->wires().size(),
        blueprint_file.string());

    bp2::Blueprint loaded = std::move(*loaded_opt);
    bp2::Blueprint inline_bp = loaded.with_interface(collapsed.semantic.iface);
    inline_bp = inline_bp.with_id(interner_.intern(blueprint_name));
    inline_bp = inline_bp.with_display_name(def->classname);
    inline_bp = inline_bp.with_viewport(
        inline_bp.pan_x(), inline_bp.pan_y(), inline_bp.zoom(), inline_bp.grid_step());

     const bp2::Blueprint before_add = model_.current();
    try {
        model_.mutate_atomically([&] {
            auto nested = bp2::Blueprint::Nested::make_embedded(
                collapsed.semantic.id,
                interner_.intern(blueprint_name),
                std::make_unique<bp2::Blueprint>(std::move(inline_bp)),
                collapsed.layout.x, collapsed.layout.y);

            execute(model_, interner_, cmd_add_nested(std::move(nested)));
            execute(model_, interner_, cmd_add_node(std::move(collapsed)));
            model_.replace_current(
                bp2::sync_collapsed_node_iface_from_nested(model_.current(), interner_.lookup(unique_id)));
        });

        if (!bp2::composite_iface_matches_nested(model_.current(), interner_.lookup(unique_id))) {
            throw std::logic_error("addBlueprint: collapsed iface desynced from nested authority");
        }

        rebuildAllWindows();
        spdlog::info("[editor] Added blueprint: {} (id={}) at ({:.1f}, {:.1f}) group={}",
            blueprint_name, unique_id, snapped_pos.x, snapped_pos.y,
            scope_id.empty() ? "root" : scope_id);
    } catch (const std::exception& e) {
        model_.replace_current(before_add);
        spdlog::error("[editor] addBlueprint('{}') failed safely: {}", blueprint_name, e.what());
    }
}
