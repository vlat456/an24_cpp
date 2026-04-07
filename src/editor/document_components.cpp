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

void sync_bridge_to_collapsed_and_nested(
    bp2::EditorModel& model,
    ui::StringInterner& interner,
    const std::string& group_id,
    const std::string& iface_name,
    bool is_input_bridge,
    PortType port_type)
{
    const ui::InternedId group_iid = interner.intern(group_id);
    const ui::InternedId iface_iid = interner.intern(iface_name);

    bp2::Blueprint bp = model.current();

    const auto* collapsed = bp.find_node(group_iid);
    if (!collapsed) {
        spdlog::warn("[editor] sync_bridge: collapsed node '{}' not found", group_id);
        return;
    }

    bp2::Blueprint::Node cn = *collapsed;
    if (is_input_bridge) {
        cn.inputs.emplace_back(iface_iid, PortSide::Input, port_type);
    } else {
        cn.outputs.emplace_back(iface_iid, PortSide::Output, port_type);
    }
    {
        std::vector<bp2::PortDescriptor> ports = cn.iface.ports();
        const Domain d = editor::common::domain_for_port_type(port_type);
        ports.push_back({iface_iid, d,
                         is_input_bridge ? bp2::Direction::Input : bp2::Direction::Output});
        cn.iface = bp2::Interface(std::move(ports));
    }
    bp = bp2::replace_node_preserve_order(bp, std::move(cn));

    const auto* nested = bp.find_nested(group_iid);
    if (nested) {
        bp2::Blueprint::Nested n = *nested;
        std::vector<bp2::PortDescriptor> ports = n.iface.ports();
        const Domain d = editor::common::domain_for_port_type(port_type);
        ports.push_back({iface_iid, d,
                         is_input_bridge ? bp2::Direction::Input : bp2::Direction::Output});
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
    node.id = interner_.intern(unique_id);
    node.type = interner_.intern(classname);
    node.name = unique_id;
    node.x = snapped_pos.x;
    node.y = snapped_pos.y;
    node.group_id = group_id;
    node.render_hint = def->render_hint;
    node.expandable = !def->cpp_class && !def->devices.empty();

    for (const auto& [port_name, port_def] : def->ports) {
        auto pid = interner_.intern(port_name);
        if (port_def.direction == PortDirection::In) {
            node.inputs.emplace_back(pid, PortSide::Input, port_def.type);
        } else if (port_def.direction == PortDirection::Out) {
            node.outputs.emplace_back(pid, PortSide::Output, port_def.type);
        } else if (port_def.direction == PortDirection::InOut) {
            node.inputs.emplace_back(pid, PortSide::InOut, port_def.type);
            node.outputs.emplace_back(pid, PortSide::InOut, port_def.type);
        }
    }

    for (const auto& [k, v] : def->params) {
        float parsed = 0.0f;
        if (locale_safe::parse_float(v, parsed)) {
            node.params[interner_.intern(k)] = parsed;
        } else {
            node.string_params[k] = v;
        }
    }

    const bool is_bridge = (classname == "BlueprintInput" || classname == "BlueprintOutput");
    const bool bridge_in_group = is_bridge && !group_id.empty()
        && model_.current().find_node(interner_.intern(group_id)) != nullptr;

    if (bridge_in_group) {
        std::string canonical_id = group_id + ":" + node.name;
        node.id = interner_.intern(canonical_id);
        unique_id = canonical_id;

        PortType pt = PortType::V;
        auto et_it = node.string_params.find("exposed_type");
        if (et_it != node.string_params.end()) {
            pt = parse_exposed_port_type(et_it->second);
        }
        for (auto& p : node.inputs) p.type = pt;
        for (auto& p : node.outputs) p.type = pt;
    }

    {
        NodeContent nc = create_node_content_from_def(def);
        node.content_type = nc.type;
        node.content_label = nc.label;
        node.content_value = nc.value;
        node.content_min = nc.min;
        node.content_max = nc.max;
        node.content_unit = nc.unit;
        node.content_state = nc.state;
        node.content_tripped = nc.tripped;
    }

    const std::string bridge_iface_name = bridge_in_group ? node.name : "";
    const bool bridge_is_input = (classname == "BlueprintInput");
    PortType bridge_port_type = PortType::V;
    if (bridge_in_group) {
        auto et_it = node.string_params.find("exposed_type");
        if (et_it != node.string_params.end()) {
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
            sync_bridge_to_collapsed_and_nested(
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
    collapsed.id = interner_.intern(unique_id);
    collapsed.type = interner_.intern(blueprint_name);
    collapsed.name = unique_id;
    collapsed.group_id = group_id;
    collapsed.x = snapped_pos.x;
    collapsed.y = snapped_pos.y;
    collapsed.width = 160.0f;
    collapsed.height = 64.0f;
    collapsed.expandable = true;
    collapsed.collapsed = true;
    collapsed.blueprint_path = blueprint_name;
    collapsed.render_hint = def->render_hint;

    std::vector<bp2::PortDescriptor> iface_ports;
    iface_ports.reserve(def->ports.size());

    for (const auto& [port_name, port_def] : def->ports) {
        const ui::InternedId pid = interner_.intern(port_name);
        if (port_def.direction == PortDirection::In) {
            collapsed.inputs.emplace_back(pid, PortSide::Input, port_def.type);
            iface_ports.push_back({pid, port_def.domain, bp2::Direction::Input});
        } else if (port_def.direction == PortDirection::Out) {
            collapsed.outputs.emplace_back(pid, PortSide::Output, port_def.type);
            iface_ports.push_back({pid, port_def.domain, bp2::Direction::Output});
        } else {
            collapsed.inputs.emplace_back(pid, PortSide::InOut, port_def.type);
            collapsed.outputs.emplace_back(pid, PortSide::InOut, port_def.type);
            iface_ports.push_back({pid, port_def.domain, bp2::Direction::InOut});
        }
    }
    collapsed.iface = bp2::Interface(std::move(iface_ports));

    {
        NodeContent nc = create_node_content_from_def(def);
        collapsed.content_type = nc.type;
        collapsed.content_label = nc.label;
        collapsed.content_value = nc.value;
        collapsed.content_min = nc.min;
        collapsed.content_max = nc.max;
        collapsed.content_unit = nc.unit;
        collapsed.content_state = nc.state;
        collapsed.content_tripped = nc.tripped;
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
    bp2::Blueprint inline_bp = loaded.with_interface(collapsed.iface);
    inline_bp = inline_bp.with_id(interner_.intern(blueprint_name));
    inline_bp = inline_bp.with_display_name(def->classname);

    bp2::Blueprint remapped_bp;
    remapped_bp = remapped_bp.with_id(inline_bp.id());
    remapped_bp = remapped_bp.with_display_name(inline_bp.display_name());
    remapped_bp = remapped_bp.with_interface(collapsed.iface);
    remapped_bp = remapped_bp.with_viewport(
        inline_bp.pan_x(), inline_bp.pan_y(), inline_bp.zoom(), inline_bp.grid_step());

    std::vector<bp2::Blueprint::Node> root_internal_nodes;
    std::vector<bp2::Blueprint::Wire> root_internal_wires;

    const auto bp_input_iid = interner_.intern("BlueprintInput");
    const auto bp_output_iid = interner_.intern("BlueprintOutput");
    std::unordered_map<ui::InternedId, ui::InternedId> id_remap;

    for (bp2::Blueprint::Node n : inline_bp.nodes()) {
        std::string original_name(interner_.resolve(n.id));
        const bool is_bridge = (n.type == bp_input_iid || n.type == bp_output_iid);
        std::string ns_id = is_bridge
            ? (unique_id + ":" + original_name)
            : (unique_id + "_" + original_name);
        ui::InternedId old_id = n.id;
        n.id = interner_.intern(ns_id);
        n.group_id = unique_id;
        id_remap[old_id] = n.id;

        bp2::Blueprint::Node n_remapped = n;
        remapped_bp = remapped_bp.with_node(std::move(n_remapped));
        root_internal_nodes.push_back(std::move(n));
    }

    for (bp2::Blueprint::Wire w : inline_bp.wires()) {
        ui::InternedId src_node_id = arena_.parent(w.source).segment();
        ui::InternedId src_port_id = w.source.segment();
        ui::InternedId tgt_node_id = arena_.parent(w.target).segment();
        ui::InternedId tgt_port_id = w.target.segment();

        auto src_it = id_remap.find(src_node_id);
        auto tgt_it = id_remap.find(tgt_node_id);
        ui::InternedId ns_src = (src_it != id_remap.end()) ? src_it->second : interner_.intern(unique_id + "_" + std::string(interner_.resolve(src_node_id)));
        ui::InternedId ns_tgt = (tgt_it != id_remap.end()) ? tgt_it->second : interner_.intern(unique_id + "_" + std::string(interner_.resolve(tgt_node_id)));

        bp2::Blueprint::Wire w_remapped;
        w_remapped.id = interner_.intern("wire_" + unique_id + "_" + std::string(interner_.resolve(w.id)));
        w_remapped.source = arena_.make_port(
            arena_.make_node(arena_.root(), ns_src), src_port_id);
        w_remapped.target = arena_.make_port(
            arena_.make_node(arena_.root(), ns_tgt), tgt_port_id);
        w_remapped.domain = w.domain;
        w_remapped.routing_points = std::move(w.routing_points);

        bp2::Blueprint::Wire w_for_doc = w_remapped;
        remapped_bp = remapped_bp.with_wire(std::move(w_remapped));
        root_internal_wires.push_back(std::move(w_for_doc));
    }

    inline_bp = std::move(remapped_bp);

    const bp2::Blueprint before_add = model_.current();
    bool checkpoint_pushed = false;
    try {
        model_.push_checkpoint();
        checkpoint_pushed = true;
        for (auto& n : root_internal_nodes) {
            execute(model_, interner_, cmd_add_node(std::move(n)));
        }
        for (auto& w : root_internal_wires) {
            execute(model_, interner_, cmd_add_wire(std::move(w)));
        }

        bp2::Blueprint::Nested nested;
        nested.id = collapsed.id;
        nested.blueprint_id = interner_.intern(blueprint_name);
        nested.embedded = true;
        nested.inline_def = std::make_unique<bp2::Blueprint>(std::move(inline_bp));
        nested.iface = collapsed.iface;
        nested.x = collapsed.x;
        nested.y = collapsed.y;

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
