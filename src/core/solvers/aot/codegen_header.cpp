#include "codegen_internal.h"
#include "../parse_number.h"

#include <vector>

namespace {

void emit_electrical_plan_debug(
    std::ostringstream& oss,
    const ElectricalPlanCodegen& electrical_plan
) {
    if (electrical_plan.islands.empty()) {
        oss << "constexpr uint32_t ELECTRICAL_ISLAND_COUNT = 0;\n\n";
        oss << "constexpr uint32_t ELECTRICAL_MAX_ISLAND_NODES = 0;\n";
        oss << "constexpr uint32_t ELECTRICAL_MAX_ISLAND_ELEMENTS = 0;\n";
        oss << "constexpr uint32_t ELECTRICAL_MAX_ELEMENT_ID = 0;\n\n";
        codegen_detail::emit_electrical_debug_entry_struct(oss);
        oss << "constexpr ElectricalDebugEntry ELECTRICAL_DEBUG_MAP[] = {};\n";
        oss << "constexpr uint32_t ELECTRICAL_DEBUG_COUNT = 0;\n\n";
        return;
    }

    oss << "constexpr ElectricalDebugEntry ELECTRICAL_DEBUG_MAP[] = {\n";
    for (const auto& dbg : electrical_plan.component_debug) {
        oss << "    { " << dbg.element_id << ", " << dbg.island_index << ", "
            << dbg.element_index << ", \"" << dbg.device_name
            << "\", \"" << dbg.device_classname << "\", \"" << dbg.role
            << "\", " << dbg.node_a << ", " << dbg.node_b << " },\n";
    }
    oss << "};\n";
    oss << "constexpr uint32_t ELECTRICAL_DEBUG_COUNT = "
        << electrical_plan.component_debug.size() << ";\n\n";

    uint32_t max_island_nodes = 0;
    uint32_t max_island_elements = 0;
    uint32_t max_element_id = 0;
    for (const auto& island : electrical_plan.islands) {
        max_island_nodes = std::max(max_island_nodes, static_cast<uint32_t>(island.signal_indices.size()));
        max_island_elements = std::max(max_island_elements, static_cast<uint32_t>(island.elements.size()));
        for (const auto& elem : island.elements) {
            max_element_id = std::max(max_element_id, elem.element_id);
        }
    }
    oss << "/// Pre-allocated scratch buffer sizes for electrical solve\n";
    oss << "constexpr uint32_t ELECTRICAL_MAX_ISLAND_NODES = " << max_island_nodes << ";\n";
    oss << "constexpr uint32_t ELECTRICAL_MAX_ISLAND_ELEMENTS = " << max_island_elements << ";\n";
    oss << "constexpr uint32_t ELECTRICAL_MAX_ELEMENT_ID = " << max_element_id << ";\n\n";
}

void emit_systems_class_declaration(
    std::ostringstream& oss,
    const std::string& class_name,
    const std::vector<ResolvedDevice>& devices,
    const std::unordered_map<std::string, uint32_t>& port_to_signal,
    uint32_t signal_count,
    const ElectricalPlanCodegen& electrical_plan
) {
    oss << "// ==============================================================================\n";
    oss << "// SYSTEMS CLASS (ECS-like: direct field access, no virtual calls)\n";
    oss << "// Components are NON-VIRTUAL for AOT - no vtable overhead\n";
    oss << "// ==============================================================================\n\n";

    oss << "class " << class_name << " {\n";
    oss << "public:\n";

    for (const auto& dev : devices) {
        std::string aot_type = codegen_detail::generate_aot_provider_type(dev, port_to_signal, signal_count);
        oss << "    " << dev.classname << "<" << aot_type << "> " << codegen_detail::sanitize_name(dev.name) << ";\n";
    }
    oss << "\n";

    oss << "    // Port indices - stored separately for O(1) access\n";
    for (const auto& dev : devices) {
        for (const auto& port : dev.ports) {
            const std::string& port_name = port.first;
            if (port.second.alias.has_value() && !port.second.alias.value().empty()) {
                continue;
            }
            std::string port_key = signal_key::make_node_port_key(dev.name, port_name);
            uint32_t sig = port_to_signal.count(port_key) ? port_to_signal.at(port_key) : signal_count;
            oss << "    static constexpr uint32_t " << codegen_detail::sanitize_name(dev.name) << "_" << port_name << "_idx = " << sig << ";\n";
        }
    }
    oss << "\n";

    oss << "    uint32_t step_counter_ = 0;\n\n";

    if (!electrical_plan.islands.empty()) {
        oss << "    ElectricalBuildPlan electrical_plan_;\n";
        oss << "    ElectricalRuntimeState electrical_rt_;\n";
        oss << "    static constexpr float ELECTRICAL_DIAG_RESIDUAL_WARN = 1e-4f;\n";
        oss << "    static constexpr uint32_t ELECTRICAL_COUNTER_LOG_PERIOD = 600;\n";
        oss << "\n";
        oss << "    struct ElectricalBindings {\n";
        for (const auto& binding : electrical_plan.device_bindings) {
            oss << "        static constexpr uint32_t " << binding.device_field_name
                << "_island = " << binding.island_index << ";\n";
            oss << "        static constexpr uint32_t " << binding.device_field_name
                << "_element = " << binding.element_index << ";\n";
            oss << "        static constexpr uint32_t " << binding.device_field_name
                << "_element_id = " << binding.element_id << ";\n";
        }
        oss << "    };\n";
        oss << "\n";
        oss << "    static void dump_island_debug(uint32_t island_idx);\n";
    }
    oss << "\n";

    oss << "    " << class_name << "();\n";
    oss << "    ~" << class_name << "();\n\n";
    oss << "    " << class_name << "(const " << class_name << "&) = delete;\n";
    oss << "    " << class_name << "& operator=(const " << class_name << "&) = delete;\n\n";
    oss << "    void pre_load();\n";
    oss << "    void solve_step(void* state, uint32_t step, double dt);\n\n";

    for (int step = 0; step < 60; ++step) {
        oss << "    AOT_INLINE void step_" << step << "(void* state, double dt);\n";
    }
    oss << "\n";

    oss << "    AOT_INLINE bool check_convergence(void* state, float tolerance) const;\n\n";
    oss << "    uint32_t component_count() const { return " << devices.size() << "; }\n";
    oss << "};\n\n";
}

} // namespace

std::string CodeGen::generate_header(
    const std::string& source_file,
    const std::vector<ResolvedDevice>& devices_unfiltered,
    const std::vector<Connection>& connections,
    const std::unordered_map<std::string, uint32_t>& port_to_signal,
    uint32_t signal_count,
    const std::string& class_name,
    const ElectricalPlanCodegen& electrical_plan
) {
    auto devices = codegen_detail::filter_simulation_devices(devices_unfiltered);
    (void)connections;

    std::ostringstream oss;

    std::string guard = "GENERATED_" + codegen_detail::sanitize_name(source_file);
    std::replace(guard.begin(), guard.end(), '/', '_');
    std::replace(guard.begin(), guard.end(), '\\', '_');
    guard += "_H";

    oss << "// Auto-generated by codegen from " << source_file << "\n";
    oss << "// DO NOT EDIT - this will be overwritten on next build\n\n";
    oss << "#pragma once\n\n";
    oss << "#include <cstdint>\n";
    oss << "#include <string>\n";
    oss << "#include <array>\n";
    oss << "#include <vector>\n";
    oss << "#include <cmath>\n";
    oss << "#include \"core/solvers/jit/state.h\"\n";
    oss << "#include \"core/solvers/jit/components/all.h\"\n";
    oss << "#include \"core/solvers/jit/components/port_registry.h\"\n\n";
    oss << "#ifdef __GNUC__\n";
    oss << "#define AOT_INLINE __attribute__((always_inline)) inline\n";
    oss << "#define AOT_LIKELY(x) __builtin_expect(!!(x), 1)\n";
    oss << "#define AOT_UNLIKELY(x) __builtin_expect(!!(x), 0)\n";
    oss << "#else\n";
    oss << "#define AOT_INLINE inline\n";
    oss << "#define AOT_LIKELY(x) (x)\n";
    oss << "#define AOT_UNLIKELY(x) (x)\n";
    oss << "#endif\n\n";

    for (const auto& [port, sig] : port_to_signal) {
        std::string const_name = "SIG_" + codegen_detail::sanitize_name(codegen_detail::to_upper(port));
        oss << "constexpr uint32_t " << const_name << " = " << sig << ";\n";
    }
    oss << "\n";

    oss << "constexpr uint32_t FIXED_SIGNALS[] = {";
    bool first = true;
    for (const auto& dev : devices) {
        if (dev.classname == "RefNode") {
            std::string port_key = dev.name + ".v";
            if (port_to_signal.count(port_key)) {
                if (!first) {
                    oss << ", ";
                }
                oss << port_to_signal.at(port_key);
                first = false;
            }
        }
    }
    oss << "};\n\n";

    oss << "constexpr uint32_t SIGNAL_COUNT = " << signal_count << ";\n";
    oss << "constexpr uint32_t DEVICE_COUNT = " << devices.size() << ";\n\n";

    if (!electrical_plan.islands.empty()) {
        oss << "constexpr uint32_t ELECTRICAL_ISLAND_COUNT = " << electrical_plan.islands.size() << ";\n\n";

        for (size_t island_idx = 0; island_idx < electrical_plan.islands.size(); ++island_idx) {
            const auto& island = electrical_plan.islands[island_idx];
            oss << "constexpr uint32_t island_" << island_idx << "_nodes[] = {";
            for (size_t i = 0; i < island.signal_indices.size(); ++i) {
                if (i > 0) {
                    oss << ", ";
                }
                oss << island.signal_indices[i];
            }
            oss << "};\n";

            oss << "constexpr uint32_t island_" << island_idx << "_element_count = " << island.elements.size() << ";\n";
            oss << "constexpr ElectricalElement island_" << island_idx << "_elements[] = {\n";
            for (const auto& elem : island.elements) {
                const char* kind_str = "ElectricalElementKind::FixedVoltageNode";
                if (elem.kind == ElectricalElementKindCodegen::TheveninSource) {
                    kind_str = "ElectricalElementKind::TheveninSource";
                } else if (elem.kind == ElectricalElementKindCodegen::ConductanceBranch) {
                    kind_str = "ElectricalElementKind::ConductanceBranch";
                }
                oss << "    { " << kind_str << ", " << elem.node_a << ", " << elem.node_b
                    << ", " << locale_safe::format_float(elem.value_a) << "f, "
                    << locale_safe::format_float(elem.value_b) << "f, " << elem.element_id << " },\n";
            }
            oss << "};\n\n";
        }

        oss << "struct AotElectricalPlan {\n";
        oss << "    std::vector<ElectricalIslandPlan> islands;\n";
        oss << "    AotElectricalPlan() {\n";
        for (size_t island_idx = 0; island_idx < electrical_plan.islands.size(); ++island_idx) {
            const auto& island = electrical_plan.islands[island_idx];
            oss << "        ElectricalIslandPlan isl;\n";
            oss << "        isl.signal_indices.assign(island_" << island_idx << "_nodes, island_" << island_idx
                << "_nodes + " << island.signal_indices.size() << ");\n";
            oss << "        isl.elements.assign(island_" << island_idx << "_elements, island_" << island_idx
                << "_elements + " << island.elements.size() << ");\n";
            oss << "        islands.push_back(std::move(isl));\n";
        }
        oss << "    }\n";
        oss << "};\n\n";

        codegen_detail::emit_electrical_debug_entry_struct(oss);

        emit_electrical_plan_debug(oss, electrical_plan);
    } else {
        emit_electrical_plan_debug(oss, electrical_plan);
    }

    oss << "extern SimulationState* g_state;\n\n";
    emit_systems_class_declaration(oss, class_name, devices, port_to_signal, signal_count, electrical_plan);
    return oss.str();
}
