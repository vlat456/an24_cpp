#include "codegen_internal.h"

#include "parse_number.h"

#include <optional>
#include <set>

namespace {

std::string infer_type(const std::string& value) {
    if (value.empty()) {
        return "float";
    }

    long long ival;
    if (locale_safe::parse_int64(value, ival)) {
        if (ival > INT32_MAX || ival < INT32_MIN) {
            return "int64_t";
        }
        return "int32_t";
    }

    if (locale_safe::is_float_literal(value)) {
        return "float";
    }

    if (value == "true" || value == "false") {
        return "bool";
    }

    return "std::string";
}

std::string format_value(const std::string& value, const std::string& type) {
    if (type == "float") {
        float f;
        if (locale_safe::parse_float(value, f)) {
            return locale_safe::format_float(f) + "f";
        }
        std::string v = value;
        if (v.find('.') == std::string::npos) {
            v += ".0";
        }
        return v;
    }

    if (type == "bool" || type == "int32_t" || type == "int64_t") {
        return value;
    }

    if (type == "std::string") {
        return "std::string(\"" + value + "\")";
    }

    return "\"" + value + "\"";
}

void emit_source_prelude(
    std::ostringstream& oss,
    const std::string& header_name,
    const std::vector<ResolvedDevice>& devices,
    const std::unordered_map<std::string, uint32_t>& port_to_signal,
    uint32_t signal_count
) {
    oss << "#include \"" << header_name << "\"\n";
    oss << "#include \"core/solvers/jit/components/all.cpp\"\n";
    oss << "#include \"core/solvers/jit/subsolvers/nodal_subsolver.h\"\n";
    oss << "#include <spdlog/spdlog.h>\n";
    oss << "#include <algorithm>\n";
    oss << "#include <cstring>\n\n";
    oss << "#ifdef __GNUC__\n";
    oss << "#pragma GCC optimize(\"fast-math,unroll-loops\")\n";
    oss << "#endif\n\n";

    for (const auto& dev : devices) {
        std::string aot_type = codegen_detail::generate_aot_provider_type(dev, port_to_signal, signal_count);
        oss << "template class " << dev.classname << "<" << aot_type << ">;\n";
    }
    oss << "\n";
}

struct LutEntry {
    std::string dev_name;
    std::vector<float> keys;
    std::vector<float> values;
};

std::optional<LutEntry> parse_lut_table(const ResolvedDevice& dev) {
    auto it = dev.params.find("table");
    if (it == dev.params.end()) {
        return std::nullopt;
    }

    LutEntry entry;
    entry.dev_name = codegen_detail::sanitize_name(dev.name);
    std::string tbl = it->second;
    size_t pos = 0;

    while (pos < tbl.size()) {
        while (pos < tbl.size() && (tbl[pos] == ' ' || tbl[pos] == ';')) {
            ++pos;
        }
        if (pos >= tbl.size()) {
            break;
        }
        size_t colon = tbl.find(':', pos);
        if (colon == std::string::npos) {
            break;
        }
        size_t end = tbl.find(';', colon + 1);
        if (end == std::string::npos) {
            end = tbl.size();
        }

        float key_f;
        float val_f;
        if (locale_safe::parse_float(tbl.substr(pos, colon - pos), key_f)
            && locale_safe::parse_float(tbl.substr(colon + 1, end - colon - 1), val_f)) {
            entry.keys.push_back(key_f);
            entry.values.push_back(val_f);
        } else {
            return std::nullopt;
        }
        pos = end;
    }

    return entry;
}

void emit_constructor_params(
    std::ostringstream& oss,
    const std::vector<ResolvedDevice>& devices,
    const ElectricalPlanCodegen& electrical_plan,
    std::vector<LutEntry>& out_lut_entries
) {
    uint32_t lut_offset = 0;
    for (const auto& dev : devices) {
        if (dev.kind == ComponentKind::LUT) {
            auto lut_opt = parse_lut_table(dev);
            if (lut_opt.has_value()) {
                auto& entry = *lut_opt;
                oss << "    " << entry.dev_name << ".table_offset = " << lut_offset << ";\n";
                oss << "    " << entry.dev_name << ".table_size = " << entry.keys.size() << ";\n";
                lut_offset += static_cast<uint32_t>(entry.keys.size());
                out_lut_entries.push_back(std::move(entry));
            }
            continue;
        }

        for (const auto& param : dev.params) {
            if (param.first == "inv_internal_r" || param.first == "inv_capacity") {
                continue;
            }
            std::string type = infer_type(param.second);
            oss << "    " << codegen_detail::sanitize_name(dev.name) << "." << param.first << " = "
                << format_value(param.second, type) << ";\n";
        }
    }

    if (!electrical_plan.islands.empty()) {
        oss << "    { AotElectricalPlan aot_plan;\n";
        oss << "      electrical_plan_.islands = std::move(aot_plan.islands); }\n";
        oss << "    electrical_rt_.reserve(ELECTRICAL_MAX_ISLAND_NODES, "
            << "ELECTRICAL_MAX_ISLAND_ELEMENTS, ELECTRICAL_MAX_ELEMENT_ID);\n";

        // == KnobSwitch multi-handle binding setup ==
        // Identify KnobSwitch devices by their sanitized field names and group
        // indexed bindings (basename_0, basename_1, ...) per device.
        std::set<std::string> knob_field_names;
        for (const auto& dev : devices) {
            if (is_knob_switch_kind(dev.kind)) {
                knob_field_names.insert(codegen_detail::sanitize_name(dev.name));
            }
        }

        // Map: knob_field_name → sorted list of (index, binding*)
        std::map<std::string, std::vector<std::pair<int, const ElectricalPlanCodegen::DeviceBinding*>>> knob_indexed;
        for (const auto& binding : electrical_plan.device_bindings) {
            for (const auto& knob_name : knob_field_names) {
                const std::string& name = binding.device_field_name;
                // Match "knobname_N" where N is a non-negative integer
                if (name.size() > knob_name.size() + 1 &&
                    name.compare(0, knob_name.size(), knob_name) == 0 &&
                    name[knob_name.size()] == '_') {
                    std::string suffix = name.substr(knob_name.size() + 1);
                    if (!suffix.empty() &&
                        suffix.find_first_not_of("0123456789") == std::string::npos) {
                        knob_indexed[knob_name].push_back(
                            {std::stoi(suffix), &binding});
                    }
                }
            }
        }

        // Build set of binding field names that belong to KnobSwitch indexed bindings
        std::set<std::string> indexed_binding_names;
        for (const auto& [knob_name, bindings] : knob_indexed) {
            for (const auto& [idx, binding] : bindings) {
                indexed_binding_names.insert(binding->device_field_name);
            }
        }

        // == Single-handle init for non-KnobSwitch wrapper components ==
        for (const auto& binding : electrical_plan.device_bindings) {
            if (indexed_binding_names.count(binding.device_field_name)) {
                continue;  // KnobSwitch indexed — handled below
            }
            oss << "    " << binding.device_field_name << ".electrical_handle.island_index = "
                << "ElectricalBindings::" << binding.device_field_name << "_island;\n";
            oss << "    " << binding.device_field_name << ".electrical_handle.element_index = "
                << "ElectricalBindings::" << binding.device_field_name << "_element;\n";
            oss << "    " << binding.device_field_name << ".electrical_handle.element_id = "
                << "ElectricalBindings::" << binding.device_field_name << "_element_id;\n";
        }

        // == Multi-handle init for KnobSwitch devices ==
        for (auto& [knob_name, bindings] : knob_indexed) {
            std::sort(bindings.begin(), bindings.end(),
                [](const auto& a, const auto& b) { return a.first < b.first; });
            for (const auto& [idx, binding] : bindings) {
                oss << "    " << knob_name << ".electrical_handles[" << idx << "] = { "
                    << "ElectricalBindings::" << binding->device_field_name << "_island, "
                    << "ElectricalBindings::" << binding->device_field_name << "_element, "
                    << "ElectricalBindings::" << binding->device_field_name << "_element_id };\n";
            }
            oss << "    " << knob_name << ".num_handles = " << bindings.size() << ";\n";
        }
    }
}

void emit_preload_method(
    std::ostringstream& oss,
    const std::string& class_name,
    const std::vector<ResolvedDevice>& devices,
    const std::vector<LutEntry>& lut_entries
) {
    oss << "void " << class_name << "::pre_load() {\n";
    for (const auto& dev : devices) {
        const std::string name = codegen_detail::sanitize_name(dev.name);
        oss << "    if constexpr (requires { " << name << ".pre_load(); }) { " << name << ".pre_load(); }\n";
    }

    if (!lut_entries.empty()) {
        uint32_t lut_total = 0;
        for (const auto& e : lut_entries) {
            lut_total += static_cast<uint32_t>(e.keys.size());
        }

        oss << "    // LUT arena: all breakpoint tables (" << lut_total << " floats total)\n";
        oss << "    static const float lut_keys_data[] = {";
        bool first_k = true;
        for (const auto& e : lut_entries) {
            for (float k : e.keys) {
                if (!first_k) {
                    oss << ", ";
                }
                oss << locale_safe::format_float(k) << "f";
                first_k = false;
            }
        }
        oss << "};\n";

        oss << "    static const float lut_vals_data[] = {";
        bool first_v = true;
        for (const auto& e : lut_entries) {
            for (float v : e.values) {
                if (!first_v) {
                    oss << ", ";
                }
                oss << locale_safe::format_float(v) << "f";
                first_v = false;
            }
        }
        oss << "};\n";

        oss << "    g_state->lut_keys.assign(lut_keys_data, lut_keys_data + " << lut_total << ");\n";
        oss << "    g_state->lut_values.assign(lut_vals_data, lut_vals_data + " << lut_total << ");\n";
    }
    oss << "}\n\n";
}

void emit_solve_step_dispatch(std::ostringstream& oss, const std::string& class_name) {
    oss << "void " << class_name << "::solve_step(void* state, uint32_t step, double dt) {\n";
    oss << "    if (dt <= 0.0) return;\n\n";
    oss << "#ifndef _MSC_VER\n";
    oss << "    static const void* dispatch_table[60] = {\n";
    for (int i = 0; i < 60; ++i) {
        oss << "        &&step_" << i << (i < 59 ? ",\n" : "\n");
    }
    oss << "    };\n";
    oss << "    goto *dispatch_table[step % 60];\n\n";
    for (int i = 0; i < 60; ++i) {
        oss << "    step_" << i << ":\n";
        oss << "        step_" << i << "(state, dt);\n";
        oss << "        ++step_counter_;\n";
        oss << "        return;\n\n";
    }
    oss << "#else\n";
    oss << "    switch (step % 60) {\n";
    for (int i = 0; i < 60; ++i) {
        oss << "        case " << i << ": step_" << i << "(state, dt); ++step_counter_; return;\n";
    }
    oss << "    }\n";
    oss << "#endif\n";
    oss << "}\n\n";
}

void emit_step_electrical_diagnostics(std::ostringstream& oss) {
    oss << "    st->electrical_rt = &electrical_rt_;\n";
    oss << "    solve_nodal(electrical_plan_, *st, electrical_rt_, dt);\n";
    oss << "    if (step_counter_ > 0 && (step_counter_ % ELECTRICAL_COUNTER_LOG_PERIOD) == 0) {\n";
    oss << "        const auto& c = electrical_rt_.counters;\n";
    oss << "        spdlog::info(\"[aot-elec] solve counters: islands={} n0={} n1={} n2={} dense={} singular={}\",\n";
    oss << "            c.islands_total, c.solves_n0, c.solves_n1, c.solves_n2, c.solves_dense, c.singular_fallbacks);\n";
    oss << "    }\n";
    oss << "    for (const auto& diag : electrical_rt_.island_diagnostics) {\n";
    oss << "        if (!diag.solve_ok || diag.max_abs_kcl_residual > ELECTRICAL_DIAG_RESIDUAL_WARN) {\n";
    oss << "            spdlog::warn(\"[aot-elec] island={} solve_ok={} unknowns={} max_abs_kcl={} worst_signal={} worst_v={} worst_branch_comp={}\",\n";
    oss <<                 "                diag.island_index, diag.solve_ok, diag.unknown_count, diag.max_abs_kcl_residual,\n";
    oss << "                diag.worst_node_signal, diag.worst_node_potential, diag.worst_branch_element_id);\n";
    codegen_detail::emit_debug_map_lookup(oss,
        "element_id", "== diag.worst_branch_element_id",
        "[aot-elec] branch component={} device={} class={} role={} nodes=({},{})",
        "e.element_id, e.device_name, e.classname, e.role, e.node_a, e.node_b",
        true);
    oss << "            dump_island_debug(diag.island_index);\n";
    oss << "        }\n";
    oss << "    }\n";
}

void emit_dynamic_source_patching(
    std::ostringstream& oss,
    const std::vector<ResolvedDevice>& devices
) {
    oss << "    if (electrical_rt_.element_value_a.empty()) {\n";
    oss << "        uint32_t max_element_id = 0;\n";
    oss << "        bool has_elements = false;\n";
    oss << "        for (const auto& island : electrical_plan_.islands) {\n";
    oss << "            for (const auto& elem : island.elements) {\n";
    oss << "                has_elements = true;\n";
    oss << "                max_element_id = std::max(max_element_id, elem.element_id);\n";
    oss << "            }\n";
    oss << "        }\n";
    oss << "        if (has_elements) {\n";
    oss << "            electrical_rt_.element_value_a.resize(static_cast<size_t>(max_element_id) + 1, 0.0f);\n";
    oss << "            for (const auto& island : electrical_plan_.islands) {\n";
    oss << "                for (const auto& elem : island.elements) {\n";
    oss << "                    if (elem.element_id < electrical_rt_.element_value_a.size()) {\n";
    oss << "                        electrical_rt_.element_value_a[elem.element_id] = elem.value_a;\n";
    oss << "                    }\n";
    oss << "                }\n";
    oss << "            }\n";
    oss << "        }\n";
    oss << "    }\n";

    for (const auto& dev : devices) {
        const std::string field = codegen_detail::sanitize_name(dev.name);

        if (dev.kind == ComponentKind::ControlledVoltageSource) {
            oss << "    if (" << field << ".electrical_handle.element_id < electrical_rt_.element_value_a.size()) {\n";
            oss << "        float cmd = st->values[" << field << "_cmd_idx];\n";
            oss << "        float gain = st->values[" << field << "_gain_idx];\n";
            oss << "        float offset = st->values[" << field << "_offset_idx];\n";
            oss << "        float min_v = st->values[" << field << "_min_v_idx];\n";
            oss << "        float max_v = st->values[" << field << "_max_v_idx];\n";
            oss << "        electrical_rt_.element_value_a[" << field << ".electrical_handle.element_id] = "
                "std::clamp(cmd * gain + offset, min_v, max_v);\n";
            oss << "    }\n";
            continue;
        }

        if (dev.kind == ComponentKind::VariableConductance) {
            oss << "    if (" << field << ".electrical_handle.element_id < electrical_rt_.element_value_a.size()) {\n";
            oss << "        float cmd = st->values[" << field << "_cmd_idx];\n";
            oss << "        float g_min = st->values[" << field << "_g_min_idx];\n";
            oss << "        float g_max = st->values[" << field << "_g_max_idx];\n";
            oss << "        float t = std::clamp(cmd, 0.0f, 1.0f);\n";
            oss << "        electrical_rt_.element_value_a[" << field << ".electrical_handle.element_id] = g_min + (g_max - g_min) * t;\n";
            oss << "    }\n";
            continue;
        }

        if (dev.kind == ComponentKind::AZS) {
            oss << "    if (" << field << ".electrical_handle.element_id < electrical_rt_.element_value_a.size()) {\n";
            oss << "        electrical_rt_.element_value_a[" << field << ".electrical_handle.element_id] = "
                << field << ".closed ? " << field << ".g_closed : " << field << ".g_open;\n";
            oss << "    }\n";
            continue;
        }

        if (dev.kind == ComponentKind::HoldButton) {
            oss << "    if (" << field << ".electrical_handle.element_id < electrical_rt_.element_value_a.size()) {\n";
            oss << "        electrical_rt_.element_value_a[" << field << ".electrical_handle.element_id] = "
                << field << ".is_pressed ? " << field << ".g_closed : " << field << ".g_open;\n";
            oss << "    }\n";
            continue;
        }

        if (dev.kind == ComponentKind::Relay) {
            oss << "    if (" << field << ".electrical_handle.element_id < electrical_rt_.element_value_a.size()) {\n";
            oss << "        electrical_rt_.element_value_a[" << field << ".electrical_handle.element_id] = "
                << field << ".closed ? " << field << ".g_closed : " << field << ".g_open;\n";
            oss << "    }\n";
            continue;
        }

        if (is_knob_switch_kind(dev.kind)) {
            oss << "    for (int i = 0; i < " << field << ".num_handles; ++i) {\n";
            oss << "        if (" << field << ".electrical_handles[i].element_id < electrical_rt_.element_value_a.size()) {\n";
            oss << "            electrical_rt_.element_value_a[" << field << ".electrical_handles[i].element_id] = "
                << "(" << field << ".selected == i) ? " << field << ".g_closed : " << field << ".g_open;\n";
            oss << "        }\n";
            oss << "    }\n";
        }
    }
}

} // namespace

std::string CodeGen::generate_source(
    const std::string& header_name,
    const std::vector<ResolvedDevice>& devices,
    const std::unordered_map<std::string, uint32_t>& port_to_signal,
    uint32_t signal_count,
    const std::string& class_name,
    const ElectricalPlanCodegen& electrical_plan
) {

    std::ostringstream oss;

    emit_source_prelude(oss, header_name, devices, port_to_signal, signal_count);

    oss << class_name << "::" << class_name << "()\n{\n";
    std::vector<LutEntry> lut_entries;
    emit_constructor_params(oss, devices, electrical_plan, lut_entries);
    oss << "}\n\n";

    oss << class_name << "::~" << class_name << "() {}\n\n";
    emit_preload_method(oss, class_name, devices, lut_entries);
    emit_solve_step_dispatch(oss, class_name);

    for (int step = 0; step < 60; ++step) {
        oss << "AOT_INLINE void " << class_name << "::step_" << step << "(void* state, double dt) {\n";
        oss << "    auto* st = static_cast<SimulationState*>(state);\n";

        if (!electrical_plan.islands.empty()) {
            emit_dynamic_source_patching(oss, devices);
            emit_step_electrical_diagnostics(oss);
        }

        codegen_detail::emit_device_execute_commit(oss, devices);

        if (!electrical_plan.islands.empty()) {
            oss << "    st->electrical_rt = nullptr;\n";
        }
        oss << "}\n\n";
    }

    oss << "AOT_INLINE bool " << class_name << "::check_convergence(void* state, float tolerance) const {\n";
    oss << "    (void)state;\n";
    oss << "    (void)tolerance;\n";
    oss << "    return true;\n";
    oss << "}\n\n";

    if (!electrical_plan.islands.empty()) {
        oss << "void " << class_name << "::dump_island_debug(uint32_t island_idx) {\n";
        oss << "    spdlog::warn(\"[aot-elec] dump island={} entries={}\", island_idx, ELECTRICAL_DEBUG_COUNT);\n";
        codegen_detail::emit_debug_map_lookup(oss,
            "island_index", "== island_idx",
            "[aot-elec]   elem={} comp={} device={} class={} role={} nodes=({},{})",
            "e.element_index, e.element_id, e.device_name, e.classname, e.role, e.node_a, e.node_b",
            false);
        oss << "}\n\n";
    }

    return oss.str();
}
