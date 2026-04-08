#include "document.h"

#include "commands/commands.h"
#include "common/port_type_utils.h"
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

/// Update the collapsed node's and nested record's interface to include a new bridge port.
/// This is the single mutation path: both the collapsed node's visual/semantic iface
/// and the nested record's iface are updated from the same port descriptor.
void add_bridge_port_to_composite(
    bp2::EditorModel& model,
    ui::StringInterner& interner,
    const std::string& group_id,
    const std::string& iface_name,
    bool is_input_bridge,
    PortType port_type)
{
    const ui::InternedId group_iid = interner.intern(group_id);
    const ui::InternedId iface_iid = interner.intern(iface_name);
    const Domain domain = editor::common::domain_for_port_type(port_type);

    bp2::PortDescriptor pd;
    pd.name = iface_iid;
    pd.domain = domain;
    pd.direction = is_input_bridge ? bp2::Direction::Input : bp2::Direction::Output;

    bp2::Blueprint bp = model.current();

    // Update collapsed node interface
    const auto* collapsed = bp.find_node(group_iid);
    if (!collapsed) {
        spdlog::warn("[editor] add_bridge_port: collapsed node '{}' not found", group_id);
        return;
    }

    bp2::Blueprint::Node cn = *collapsed;
    if (is_input_bridge) {
        cn.view.inputs.emplace_back(iface_iid, bp2::PortSide::Input, port_type);
    } else {
        cn.view.outputs.emplace_back(iface_iid, bp2::PortSide::Output, port_type);
    }
    {
        std::vector<bp2::PortDescriptor> ports = cn.semantic.iface.ports();
        ports.push_back(pd);
        cn.semantic.iface = bp2::Interface(std::move(ports));
    }
    bp = bp2::replace_node_preserve_order(bp, std::move(cn));

    // Update nested record interface (same port descriptor — single source)
    const auto* nested = bp.find_nested(group_iid);
    if (nested) {
        bp2::Blueprint::Nested n = *nested;
        std::vector<bp2::PortDescriptor> ports = n.iface.ports();
        ports.push_back(pd);
        n.iface = bp2::Interface(std::move(ports));
        bp = bp2::replace_nested_preserve_order(bp, std::move(n));
    }

    model.replace_current(std::move(bp));
}

} // namespace

void Document::addComponent(const std::string& classname, Pt world_pos,
                            const std::string& group_id,
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
        addBlueprint(classname, world_pos, group_id, registry);
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
    node.layout.group_id = group_id;
    node.view.render_hint = def->render_hint;
    node.view.expandable = !def->cpp_class && !def->devices.empty();

    for (const auto& [port_name, port_def] : def->ports) {
        auto pid = interner_.intern(port_name);
        if (port_def.direction == PortDirection::In) {
            node.view.inputs.emplace_back(pid, bp2::PortSide::Input, port_def.type);
        } else if (port_def.direction == PortDirection::Out) {
            node.view.outputs.emplace_back(pid, bp2::PortSide::Output, port_def.type);
        } else if (port_def.direction == PortDirection::InOut) {
            node.view.inputs.emplace_back(pid, bp2::PortSide::InOut, port_def.type);
            node.view.outputs.emplace_back(pid, bp2::PortSide::InOut, port_def.type);
        }
    }

    for (const auto& [k, v] : def->params) {
        float parsed = 0.0f;
        if (locale_safe::parse_float(v, parsed)) {
            node.semantic.params[interner_.intern(k)] = parsed;
        } else {
            node.semantic.string_params[k] = v;
        }
    }

    const bool is_bridge = (classname == "BlueprintInput" || classname == "BlueprintOutput");
    const bool bridge_in_group = is_bridge && !group_id.empty()
        && model_.current().find_node(interner_.intern(group_id)) != nullptr;

    if (bridge_in_group) {
        std::string canonical_id = group_id + ":" + node.view.name;
        node.semantic.id = interner_.intern(canonical_id);
        unique_id = canonical_id;

        PortType pt = PortType::V;
        auto et_it = node.semantic.string_params.find("exposed_type");
        if (et_it != node.semantic.string_params.end()) {
            pt = parse_exposed_port_type(et_it->second);
        }
        for (auto& p : node.view.inputs) p.type = pt;
        for (auto& p : node.view.outputs) p.type = pt;
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
    bool checkpoint_pushed = false;
    try {
        model_.push_checkpoint();
        checkpoint_pushed = true;
        execute(model_, interner_, cmd_add_node(std::move(node)));

        if (bridge_in_group) {
            add_bridge_port_to_composite(
                model_, interner_, group_id,
                bridge_iface_name, bridge_is_input, bridge_port_type);
        }

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
                    if (checkpoint_pushed) {
                        model_.discard_last_checkpoint();
                    }
                    spdlog::error("[editor] addComponent('{}') rolled back due to integrity failure: {}", classname, err);
                    return;
                }
            }
        }
#endif

        rebuildAllWindows();

        spdlog::info("[editor] Added component: {} (id={}) at ({:.1f}, {:.1f}) group={}",
            classname, unique_id, snapped_pos.x, snapped_pos.y,
            group_id.empty() ? "root" : group_id);
    } catch (const std::exception& e) {
        model_.replace_current(before_add);
        if (checkpoint_pushed) {
            model_.discard_last_checkpoint();
        }
        spdlog::error("[editor] addComponent('{}') failed safely: {}", classname, e.what());
        return;
    }
}

void Document::addBlueprint(const std::string& blueprint_name, Pt world_pos,
                            const std::string& group_id,
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
    collapsed.layout.group_id = group_id;
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
        if (port_def.direction == PortDirection::In) {
            collapsed.view.inputs.emplace_back(pid, bp2::PortSide::Input, port_def.type);
            bp2::PortDescriptor pd;
            pd.name = pid;
            pd.domain = port_def.domain;
            pd.direction = bp2::Direction::Input;
            iface_ports.push_back(std::move(pd));
        } else if (port_def.direction == PortDirection::Out) {
            collapsed.view.outputs.emplace_back(pid, bp2::PortSide::Output, port_def.type);
            bp2::PortDescriptor pd;
            pd.name = pid;
            pd.domain = port_def.domain;
            pd.direction = bp2::Direction::Output;
            iface_ports.push_back(std::move(pd));
        } else {
            collapsed.view.inputs.emplace_back(pid, bp2::PortSide::InOut, port_def.type);
            collapsed.view.outputs.emplace_back(pid, bp2::PortSide::InOut, port_def.type);
            bp2::PortDescriptor pd;
            pd.name = pid;
            pd.domain = port_def.domain;
            pd.direction = bp2::Direction::InOut;
            iface_ports.push_back(std::move(pd));
        }
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
    bool checkpoint_pushed = false;
    try {
        model_.push_checkpoint();
        checkpoint_pushed = true;

        bp2::Blueprint::Nested nested;
        nested.id = collapsed.semantic.id;
        nested.blueprint_id = interner_.intern(blueprint_name);
        nested.embedded = true;
        nested.inline_def = std::make_unique<bp2::Blueprint>(std::move(inline_bp));
        nested.iface = collapsed.semantic.iface;
        nested.x = collapsed.layout.x;
        nested.y = collapsed.layout.y;

        execute(model_, interner_, cmd_add_nested(std::move(nested)));
        execute(model_, interner_, cmd_add_node(std::move(collapsed)));

        rebuildAllWindows();
        spdlog::info("[editor] Added blueprint: {} (id={}) at ({:.1f}, {:.1f}) group={}",
            blueprint_name, unique_id, snapped_pos.x, snapped_pos.y,
            group_id.empty() ? "root" : group_id);
    } catch (const std::exception& e) {
        model_.replace_current(before_add);
        if (checkpoint_pushed) {
            model_.discard_last_checkpoint();
        }
        spdlog::error("[editor] addBlueprint('{}') failed safely: {}", blueprint_name, e.what());
    }
}
