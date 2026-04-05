#include "codegen.h"
#include "codegen_utils.h"
#include "../parse_number.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <unordered_set>
#include <charconv>
#include <climits>
#include <cstdlib>
#include <cstdio>


// ===== Section 1: Helper Functions =====
// These are used across multiple codegen phases for string formatting,
// type inference, and port resolution. Low complexity, ~15 LOC each.

namespace {

std::string to_upper(const std::string& s) {
    std::string result = s;
    for (char& c : result) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return result;
}

std::string sanitize_name(const std::string& s) {
    return sanitize_codegen_name(s);
}

// Infer C++ type from parameter value (locale-independent)
std::string infer_type(const std::string& value) {
    if (value.empty()) return "float";

    long long ival;
    if (locale_safe::parse_int64(value, ival)) {
        if (ival > INT32_MAX || ival < INT32_MIN) return "int64_t";
        return "int32_t";
    }

    if (locale_safe::is_float_literal(value)) {
        return "float";
    }

    if (value == "true" || value == "false") return "bool";

    return "std::string";
}

// Format value for C++ code (locale-independent)
std::string format_value(const std::string& value, const std::string& type) {
    if (type == "float") {
        float f;
        if (locale_safe::parse_float(value, f)) {
            return locale_safe::format_float(f) + "f";
        }
        // Fallback: ensure it has a decimal point
        std::string v = value;
        if (v.find('.') == std::string::npos) {
            v += ".0";
        }
        return v;
    } else if (type == "bool") {
        return value;
    } else if (type == "int32_t" || type == "int64_t") {
        return value;
    } else if (type == "std::string") {
        return "std::string(\"" + value + "\")";
    } else {
        return "\"" + value + "\"";
    }
}

// Generate AotProvider<Binding<...>, ...> type string for a device
std::string generate_aot_provider_type(
    const DeviceInstance& dev,
    const std::unordered_map<std::string, uint32_t>& port_to_signal,
    uint32_t signal_count
) {
    std::ostringstream oss;
    oss << "AotProvider<";
    bool first = true;
    for (const auto& port : dev.ports) {
        const std::string& port_name = port.first;
        if (port.second.alias.has_value() && !port.second.alias.value().empty()) {
            continue;
        }
        std::string port_key = dev.name + "." + port_name;
        uint32_t sig = port_to_signal.count(port_key) ? port_to_signal.at(port_key) : signal_count;
        if (!first) oss << ", ";
        oss << "Binding<PortNames::" << port_name << ", " << sig << ">";
        first = false;
    }
    oss << ">";
    return oss.str();
}

// Get port name from device port definitions - NO FALLBACKS, fail hard
std::string get_port_name(const std::unordered_map<std::string, Port>& ports, const std::string& required_port) {
    if (ports.count(required_port)) {
        return required_port;
    }

    // Build error message listing available ports
    std::string available = "{";
    for (const auto& [name, port] : ports) {
        available += name + " ";
    }
    available += "}";

    throw std::runtime_error("_codegen error: required port '" + required_port + "' not found. Available ports: " + available);
}

} // anonymous namespace

// ===== Section 2: generate_header() and helper functions =====

// ===== Helper 2a: Emit electrical plan debug and limits (if islands present) =====
// LOC: ~50 (debug map emission + max sizes computation)
void emit_electrical_plan_debug(
    std::ostringstream& oss,
    const ElectricalPlanCodegen& electrical_plan
) {
    if (electrical_plan.islands.empty()) {
        oss << "constexpr uint32_t ELECTRICAL_ISLAND_COUNT = 0;\n\n";
        oss << "constexpr uint32_t ELECTRICAL_MAX_ISLAND_NODES = 0;\n";
        oss << "constexpr uint32_t ELECTRICAL_MAX_ISLAND_ELEMENTS = 0;\n";
        oss << "constexpr uint32_t ELECTRICAL_MAX_COMPONENT_INDEX = 0;\n\n";
        oss << "struct ElectricalDebugEntry {\n";
        oss << "    uint32_t component_index;\n";
        oss << "    uint32_t island_index;\n";
        oss << "    uint32_t element_index;\n";
        oss << "    const char* device_name;\n";
        oss << "    const char* classname;\n";
        oss << "    const char* role;\n";
        oss << "    uint32_t node_a;\n";
        oss << "    uint32_t node_b;\n";
        oss << "};\n\n";
        oss << "constexpr ElectricalDebugEntry ELECTRICAL_DEBUG_MAP[] = {};\n";
        oss << "constexpr uint32_t ELECTRICAL_DEBUG_COUNT = 0;\n\n";
        return;
    }

    oss << "constexpr ElectricalDebugEntry ELECTRICAL_DEBUG_MAP[] = {\n";
    for (const auto& dbg : electrical_plan.component_debug) {
        oss << "    { " << dbg.component_index << ", " << dbg.island_index << ", "
            << dbg.element_index << ", \"" << dbg.device_name
            << "\", \"" << dbg.device_classname << "\", \"" << dbg.role
            << "\", " << dbg.node_a << ", " << dbg.node_b << " },\n";
    }
    oss << "};\n";
    oss << "constexpr uint32_t ELECTRICAL_DEBUG_COUNT = "
        << electrical_plan.component_debug.size() << ";\n\n";

    // Compute max island sizes
    uint32_t max_island_nodes = 0;
    uint32_t max_island_elements = 0;
    uint32_t max_component_index = 0;
    for (const auto& island : electrical_plan.islands) {
        max_island_nodes = std::max(max_island_nodes, (uint32_t)island.signal_indices.size());
        max_island_elements = std::max(max_island_elements, (uint32_t)island.elements.size());
        for (const auto& elem : island.elements) {
            max_component_index = std::max(max_component_index, elem.component_index);
        }
    }
    oss << "/// Pre-allocated scratch buffer sizes for electrical solve\n";
    oss << "constexpr uint32_t ELECTRICAL_MAX_ISLAND_NODES = " << max_island_nodes << ";\n";
    oss << "constexpr uint32_t ELECTRICAL_MAX_ISLAND_ELEMENTS = " << max_island_elements << ";\n";
    oss << "constexpr uint32_t ELECTRICAL_MAX_COMPONENT_INDEX = " << max_component_index << ";\n\n";
}

// ===== Helper 2b: Emit Systems class declaration =====
// LOC: ~80 (device members, port indices, electrical bindings, method signatures)
void emit_systems_class_declaration(
    std::ostringstream& oss,
    const std::string& class_name,
    const std::vector<DeviceInstance>& devices,
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

    // Device member variables
    for (const auto& dev : devices) {
        std::string aot_type = generate_aot_provider_type(dev, port_to_signal, signal_count);
        oss << "    " << dev.classname << "<" << aot_type << "> " << sanitize_name(dev.name) << ";\n";
    }
    oss << "\n";

    // Port index constants (data-oriented layout)
    oss << "    // Port indices - stored separately for O(1) access\n";
    for (const auto& dev : devices) {
        for (const auto& port : dev.ports) {
            const std::string& port_name = port.first;
            if (port.second.alias.has_value() && !port.second.alias.value().empty()) {
                continue;
            }
            std::string port_key = dev.name + "." + port_name;
            uint32_t sig = port_to_signal.count(port_key) ? port_to_signal.at(port_key) : signal_count;
            oss << "    static constexpr uint32_t " << sanitize_name(dev.name) << "_" << port_name << "_idx = " << sig << ";\n";
        }
    }
    oss << "\n";

    // Step counter
    oss << "    uint32_t step_counter_ = 0;\n\n";

    // Electrical plan members and bindings
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
                << "_component = " << binding.component_index << ";\n";
        }
        oss << "    };\n";
        oss << "\n";
        oss << "    static void dump_island_debug(uint32_t island_idx);\n";
    }
    oss << "\n";

    // Constructor/Destructor
    oss << "    " << class_name << "();\n";
    oss << "    ~" << class_name << "();\n\n";
    oss << "    " << class_name << "(const " << class_name << "&) = delete;\n";
    oss << "    " << class_name << "& operator=(const " << class_name << "&) = delete;\n\n";

    // Methods
    oss << "    /// Pre-load initialization\n";
    oss << "    void pre_load();\n\n";
    oss << "    /// Main solve step with jump table dispatch\n";
    oss << "    void solve_step(void* state, uint32_t step, double dt);\n\n";

    for (int step = 0; step < 60; ++step) {
        oss << "    AOT_INLINE void step_" << step << "(void* state, double dt);\n";
    }
    oss << "\n";

    oss << "    /// Convergence check\n";
    oss << "    AOT_INLINE bool check_convergence(void* state, float tolerance) const;\n\n";
    oss << "    uint32_t component_count() const { return " << devices.size() << "; }\n";
    oss << "};\n\n";
}

// ===== Main generate_header() - refactored orchestrator =====
// LOC: ~50 (clean 4-phase delegation)
std::string CodeGen::generate_header(
    const std::string& source_file,
    const std::vector<DeviceInstance>& devices_unfiltered,
    const std::vector<Connection>& connections,
    const std::unordered_map<std::string, uint32_t>& port_to_signal,
    uint32_t signal_count,
    const std::string& class_name,
    const ElectricalPlanCodegen& electrical_plan
) {
    // Filter out visual-only devices
    std::vector<DeviceInstance> devices;
    devices.reserve(devices_unfiltered.size());
    for (const auto& d : devices_unfiltered)
        if (!d.visual_only) devices.push_back(d);

    std::ostringstream oss;

    // ===== Phase 1: Header prelude (guard, includes, pragmas) =====
    std::string guard = "GENERATED_" + sanitize_name(source_file);
    std::replace(guard.begin(), guard.end(), '/', '_');
    std::replace(guard.begin(), guard.end(), '\\', '_');
    guard += "_H";

    oss << "// Auto-generated by codegen from " << source_file << "\n";
    oss << "// DO NOT EDIT - this will be overwritten on next build\n";
    oss << "// ECS-like optimized: jump table, __restrict, no aliasing\n\n";

    oss << "#pragma once\n\n";
    oss << "#include <cstdint>\n";
    oss << "#include <string>\n";
    oss << "#include <array>\n";
    oss << "#include <vector>\n";
    oss << "#include <cmath>\n";
    oss << "#include \"core/solvers/jit/state.h\"\n";
    oss << "#include \"core/solvers/jit/components/all.h\"\n";
    oss << "#include \"core/solvers/jit/components/port_registry.h\"\n\n";
    oss << "// Compiler hints for optimization\n";
    oss << "#ifdef __GNUC__\n";
    oss << "#define AOT_INLINE __attribute__((always_inline)) inline\n";
    oss << "#define AOT_LIKELY(x) __builtin_expect(!!(x), 1)\n";
    oss << "#define AOT_UNLIKELY(x) __builtin_expect(!!(x), 0)\n";
    oss << "#else\n";
    oss << "#define AOT_INLINE inline\n";
    oss << "#define AOT_LIKELY(x) (x)\n";
    oss << "#define AOT_UNLIKELY(x) (x)\n";
    oss << "#endif\n\n";

    // ===== Phase 2: Signal constants =====
    oss << "// ==============================================================================\n";
    oss << "// SIGNAL INDICES (ECS-like: direct array access, no lookups)\n";
    oss << "// ==============================================================================\n\n";

    for (const auto& [port, sig] : port_to_signal) {
        std::string const_name = "SIG_" + sanitize_name(to_upper(port));
        oss << "constexpr uint32_t " << const_name << " = " << sig << ";\n";
    }
    oss << "\n";

    // Fixed signals
    oss << "/// Fixed signal indices (RefNode bus voltages)\n";
    oss << "constexpr uint32_t FIXED_SIGNALS[] = {";
    bool first = true;
    for (const auto& dev : devices) {
        if (dev.classname == "RefNode") {
            std::string port_key = dev.name + ".v";
            if (port_to_signal.count(port_key)) {
                if (!first) oss << ", ";
                oss << port_to_signal.at(port_key);
                first = false;
            }
        }
    }
    oss << "};\n\n";

    // Signal count and device count
    oss << "/// Total number of unique signals (for memory allocation)\n";
    oss << "constexpr uint32_t SIGNAL_COUNT = " << signal_count << ";\n\n";
    oss << "/// Number of devices in this system\n";
    oss << "constexpr uint32_t DEVICE_COUNT = " << devices.size() << ";\n\n";

    // ===== Phase 3: Electrical island plan (if present) =====
    if (!electrical_plan.islands.empty()) {
        oss << "// ==============================================================================\n";
        oss << "// ELECTRICAL ISLAND PLAN (AOT static arrays)\n";
        oss << "// ==============================================================================\n\n";

        oss << "constexpr uint32_t ELECTRICAL_ISLAND_COUNT = "
            << electrical_plan.islands.size() << ";\n\n";

        // Emit island arrays
        for (size_t island_idx = 0; island_idx < electrical_plan.islands.size(); ++island_idx) {
            const auto& island = electrical_plan.islands[island_idx];
            oss << "// -- Island " << island_idx << " --\n";

            // Signal indices for this island
            oss << "constexpr uint32_t island_" << island_idx << "_nodes[] = {";
            for (size_t i = 0; i < island.signal_indices.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << island.signal_indices[i];
            }
            oss << "};\n";

            // Elements for this island
            oss << "constexpr uint32_t island_" << island_idx << "_element_count = "
                << island.elements.size() << ";\n";

            oss << "constexpr ElectricalElement island_" << island_idx << "_elements[] = {\n";
            for (const auto& elem : island.elements) {
                const char* kind_str = "ElectricalElementKind::FixedVoltageNode";
                if (elem.kind == ElectricalElementKindCodegen::TheveninSource) kind_str = "ElectricalElementKind::TheveninSource";
                else if (elem.kind == ElectricalElementKindCodegen::ConductanceBranch) kind_str = "ElectricalElementKind::ConductanceBranch";
                oss << "    { " << kind_str << ", " << elem.node_a << ", " << elem.node_b
                    << ", " << locale_safe::format_float(elem.value_a) << "f, "
                    << locale_safe::format_float(elem.value_b) << "f, " << elem.component_index << " },\n";
            }
            oss << "};\n\n";
        }

        // AotElectricalPlan struct adapter
        oss << "// ElectricalBuildPlan adapter: uses static constexpr arrays\n";
        oss << "struct AotElectricalPlan {\n";
        oss << "    std::vector<ElectricalIslandPlan> islands;\n";
        oss << "    AotElectricalPlan() {\n";
        for (size_t island_idx = 0; island_idx < electrical_plan.islands.size(); ++island_idx) {
            const auto& island = electrical_plan.islands[island_idx];
            oss << "        // Island " << island_idx << "\n";
            oss << "        ElectricalIslandPlan isl;\n";
            oss << "        isl.signal_indices.assign(island_" << island_idx << "_nodes, "
                << "island_" << island_idx << "_nodes + " << island.signal_indices.size() << ");\n";
            oss << "        isl.elements.assign(island_" << island_idx << "_elements, "
                << "island_" << island_idx << "_elements + " << island.elements.size() << ");\n";
            oss << "        islands.push_back(std::move(isl));\n";
        }
        oss << "    }\n";
        oss << "};\n\n";

        // Debug entry struct and map
        oss << "struct ElectricalDebugEntry {\n";
        oss << "    uint32_t component_index;\n";
        oss << "    uint32_t island_index;\n";
        oss << "    uint32_t element_index;\n";
        oss << "    const char* device_name;\n";
        oss << "    const char* classname;\n";
        oss << "    const char* role;\n";
        oss << "    uint32_t node_a;\n";
        oss << "    uint32_t node_b;\n";
        oss << "};\n\n";
        oss << "constexpr ElectricalDebugEntry ELECTRICAL_DEBUG_MAP[] = {\n";
        for (const auto& dbg : electrical_plan.component_debug) {
            oss << "    { " << dbg.component_index << ", " << dbg.island_index << ", "
                << dbg.element_index << ", \"" << dbg.device_name
                << "\", \"" << dbg.device_classname << "\", \"" << dbg.role
                << "\", " << dbg.node_a << ", " << dbg.node_b << " },\n";
        }
        oss << "};\n\n";

        // Debug entry struct and map (now uses helper)
        oss << "struct ElectricalDebugEntry {\n";
        oss << "    uint32_t component_index;\n";
        oss << "    uint32_t island_index;\n";
        oss << "    uint32_t element_index;\n";
        oss << "    const char* device_name;\n";
        oss << "    const char* classname;\n";
        oss << "    const char* role;\n";
        oss << "    uint32_t node_a;\n";
        oss << "    uint32_t node_b;\n";
        oss << "};\n\n";

        // Phase 4: Electrical debug + limits (delegated to helper)
        emit_electrical_plan_debug(oss, electrical_plan);
    } else {
        // Empty electrical plan
        emit_electrical_plan_debug(oss, electrical_plan);
    }

    // Phase 5: Global state + Systems class (delegated to helper)
    oss << "/// Global simulation state pointer (set once, used by all components)\n";
    oss << "extern SimulationState* g_state;\n\n";
    
    emit_systems_class_declaration(oss, class_name, devices, port_to_signal, signal_count, electrical_plan);

    return oss.str();
}

// ===== Section 3: generate_source() and helper functions =====
// Generate C++ source file for AOT compiled system.
// Splits large function into focused helpers for maintainability.

// ===== Helper 3a: Emit source prelude (includes, pragmas, template instantiations) =====
// LOC: ~40
void emit_source_prelude(
    std::ostringstream& oss,
    const std::string& header_name,
    const std::vector<DeviceInstance>& devices,
    const std::unordered_map<std::string, uint32_t>& port_to_signal,
    uint32_t signal_count,
    const std::string& class_name
) {
    oss << "#include \"" << header_name << "\"\n";
    oss << "#include \"core/solvers/jit/components/all.cpp\"\n";
    oss << "#include \"core/solvers/jit/subsolvers/electrical_subsolver.h\"\n";
    oss << "#include <spdlog/spdlog.h>\n";
    oss << "#include <cstring>  // memcpy\n\n";
    oss << "#ifdef __GNUC__\n";
    oss << "#pragma GCC optimize(\"fast-math,unroll-loops\")\n";
    oss << "#endif\n\n";

    oss << "// Explicit template instantiations for AOT\n";
    for (const auto& dev : devices) {
        std::string aot_type = generate_aot_provider_type(dev, port_to_signal, signal_count);
        oss << "template class " << dev.classname << "<" << aot_type << ">;\n";
    }
    oss << "\n";
}

// ===== Helper 3b: Parse LUT table parameter into LUT entries =====
// LOC: ~50
struct LutEntry { std::string dev_name; std::vector<float> keys; std::vector<float> values; };

std::optional<LutEntry> parse_lut_table(const DeviceInstance& dev) {
    auto it = dev.params.find("table");
    if (it == dev.params.end()) {
        return std::nullopt;
    }

    LutEntry entry;
    entry.dev_name = sanitize_name(dev.name);
    std::string tbl = it->second;
    size_t pos = 0;

    while (pos < tbl.size()) {
        while (pos < tbl.size() && (tbl[pos] == ' ' || tbl[pos] == ';')) ++pos;
        if (pos >= tbl.size()) break;
        size_t colon = tbl.find(':', pos);
        if (colon == std::string::npos) break;
        size_t end = tbl.find(';', colon + 1);
        if (end == std::string::npos) end = tbl.size();
        float key_f, val_f;
        if (locale_safe::parse_float(tbl.substr(pos, colon - pos), key_f) &&
            locale_safe::parse_float(tbl.substr(colon + 1, end - colon - 1), val_f)) {
            entry.keys.push_back(key_f);
            entry.values.push_back(val_f);
        } else {
            return std::nullopt;
        }
        pos = end;
    }

    return entry;
}

// ===== Helper 3c: Emit constructor parameter initialization =====
// LOC: ~55
void emit_constructor_params(
    std::ostringstream& oss,
    const std::vector<DeviceInstance>& devices,
    const ElectricalPlanCodegen& electrical_plan,
    std::vector<LutEntry>& out_lut_entries,
    uint32_t& out_lut_offset
) {
    out_lut_offset = 0;
    for (const auto& dev : devices) {
        if (dev.classname == "LUT") {
            auto lut_opt = parse_lut_table(dev);
            if (lut_opt.has_value()) {
                auto& entry = *lut_opt;
                oss << "    " << entry.dev_name << ".table_offset = " << out_lut_offset << ";\n";
                oss << "    " << entry.dev_name << ".table_size = " << entry.keys.size() << ";\n";
                out_lut_offset += static_cast<uint32_t>(entry.keys.size());
                out_lut_entries.push_back(std::move(entry));
            }
            continue;
        }

        for (const auto& param : dev.params) {
            if (param.first == "inv_internal_r" || param.first == "inv_capacity") {
                continue;
            }
            std::string type = infer_type(param.second);
            oss << "    " << sanitize_name(dev.name) << "." << param.first << " = "
                << format_value(param.second, type) << ";\n";
        }
    }

    if (!electrical_plan.islands.empty()) {
        oss << "    { AotElectricalPlan aot_plan;\n";
        oss << "      electrical_plan_.islands = std::move(aot_plan.islands); }\n";
        oss << "    electrical_rt_.reserve(ELECTRICAL_MAX_ISLAND_NODES, "
            << "ELECTRICAL_MAX_ISLAND_ELEMENTS, ELECTRICAL_MAX_COMPONENT_INDEX);\n";
        for (const auto& binding : electrical_plan.device_bindings) {
            oss << "    " << binding.device_field_name << ".electrical_handle.island_index = "
                << "ElectricalBindings::" << binding.device_field_name << "_island;\n";
            oss << "    " << binding.device_field_name << ".electrical_handle.element_index = "
                << "ElectricalBindings::" << binding.device_field_name << "_element;\n";
            oss << "    " << binding.device_field_name << ".electrical_handle.component_index = "
                << "ElectricalBindings::" << binding.device_field_name << "_component;\n";
        }
    }
}

// ===== Helper 3d: Emit pre_load and LUT arena initialization =====
// LOC: ~55
void emit_preload_method(
    std::ostringstream& oss,
    const std::string& class_name,
    const std::vector<DeviceInstance>& devices,
    const std::vector<LutEntry>& lut_entries
) {
    oss << "void " << class_name << "::pre_load() {\n";
    for (const auto& dev : devices) {
        oss << "    " << sanitize_name(dev.name) << ".pre_load();\n";
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
                if (!first_k) oss << ", ";
                oss << locale_safe::format_float(k) << "f";
                first_k = false;
            }
        }
        oss << "};\n";
        oss << "    static const float lut_vals_data[] = {";
        bool first_v = true;
        for (const auto& e : lut_entries) {
            for (float v : e.values) {
                if (!first_v) oss << ", ";
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

// ===== Helper 3e: Emit dispatch table and step switch =====
// LOC: ~50 (core dispatch logic)
void emit_solve_step_dispatch(std::ostringstream& oss, const std::string& class_name) {
    oss << "void " << class_name << "::solve_step(void* state, uint32_t step, double dt) {\n";
    oss << "    if (dt <= 0.0) return;\n\n";
    oss << "#ifndef _MSC_VER\n";
    oss << "    static const void* dispatch_table[" << 60 << "] = {\n";
    for (int i = 0; i < 60; ++i) {
        oss << "        &&step_" << i << (i < 60 - 1 ? ",\n" : "\n");
    }
    oss << "    };\n";
    oss << "    goto *dispatch_table[step % " << 60 << "];\n\n";
    for (int i = 0; i < 60; ++i) {
        oss << "    step_" << i << ":\n";
        oss << "        step_" << i << "(state, dt);\n";
        oss << "        ++step_counter_;\n";
        oss << "        return;\n\n";
    }
    oss << "#else\n";
    oss << "    switch (step % " << 60 << ") {\n";
    for (int i = 0; i < 60; ++i) {
        oss << "        case " << i << ": step_" << i << "(state, dt); ++step_counter_; return;\n";
    }
    oss << "    }\n";
    oss << "#endif\n";
    oss << "}\n\n";
}

// ===== Helper 3f: Emit electrical diagnostics for one step =====
// LOC: ~30 (tightly coupled electrical logging logic)
void emit_step_electrical_diagnostics(std::ostringstream& oss) {
    oss << "    st->electrical_rt = &electrical_rt_;\n";
    oss << "    solve_electrical(electrical_plan_, *st, electrical_rt_, dt);\n";
    oss << "    if (step_counter_ > 0 && (step_counter_ % ELECTRICAL_COUNTER_LOG_PERIOD) == 0) {\n";
    oss << "        const auto& c = electrical_rt_.counters;\n";
    oss << "        spdlog::info(\"[aot-elec] solve counters: islands={} n0={} n1={} n2={} dense={} singular={}\",\n";
    oss << "            c.islands_total, c.solves_n0, c.solves_n1, c.solves_n2, c.solves_dense, c.singular_fallbacks);\n";
    oss << "    }\n";
    oss << "    for (const auto& diag : electrical_rt_.island_diagnostics) {\n";
    oss << "        if (!diag.solve_ok || diag.max_abs_kcl_residual > ELECTRICAL_DIAG_RESIDUAL_WARN) {\n";
    oss << "            spdlog::warn(\"[aot-elec] island={} solve_ok={} unknowns={} max_abs_kcl={} worst_signal={} worst_v={} worst_branch_comp={}\",\n";
    oss << "                diag.island_index, diag.solve_ok, diag.unknown_count, diag.max_abs_kcl_residual,\n";
    oss << "                diag.worst_node_signal, diag.worst_node_voltage, diag.worst_branch_component_index);\n";
    oss << "            for (uint32_t i = 0; i < ELECTRICAL_DEBUG_COUNT; ++i) {\n";
    oss << "                const auto& e = ELECTRICAL_DEBUG_MAP[i];\n";
    oss << "                if (e.component_index == diag.worst_branch_component_index) {\n";
    oss << "                    spdlog::warn(\"[aot-elec] branch component={} device={} class={} role={} nodes=({},{})\",\n";
    oss << "                        e.component_index, e.device_name, e.classname, e.role, e.node_a, e.node_b);\n";
    oss << "                    break;\n";
    oss << "                }\n";
    oss << "            }\n";
    oss << "            dump_island_debug(diag.island_index);\n";
    oss << "        }\n";
    oss << "    }\n";
}

// ===== Main generate_source() - refactored orchestrator =====
// LOC: ~80 (clean delegation, low complexity)
std::string CodeGen::generate_source(
    const std::string& header_name,
    const std::vector<DeviceInstance>& devices_unfiltered,
    const std::vector<Connection>& connections,
    const std::unordered_map<std::string, uint32_t>& port_to_signal,
    uint32_t signal_count,
    const std::string& class_name,
    const ElectricalPlanCodegen& electrical_plan
) {
    // Filter out visual-only devices
    std::vector<DeviceInstance> devices;
    devices.reserve(devices_unfiltered.size());
    for (const auto& d : devices_unfiltered)
        if (!d.visual_only) devices.push_back(d);

    std::ostringstream oss;

    // Phase 1: Emit source prelude (includes, instantiations)
    emit_source_prelude(oss, header_name, devices, port_to_signal, signal_count, class_name);

    // Phase 2: Emit constructor
    oss << class_name << "::" << class_name << "()\n{\n";
    std::vector<LutEntry> lut_entries;
    uint32_t lut_arena_offset = 0;
    emit_constructor_params(oss, devices, electrical_plan, lut_entries, lut_arena_offset);
    oss << "}\n\n";

    // Phase 3: Destructor
    oss << class_name << "::~" << class_name << "() {}\n\n";

    // Phase 4: Pre-load
    emit_preload_method(oss, class_name, devices, lut_entries);

    // Phase 5: Solve step dispatch
    emit_solve_step_dispatch(oss, class_name);

    // Phase 6: Emit 60 step methods (electrical solve + execute + commit)
    for (int step = 0; step < 60; ++step) {
        oss << "AOT_INLINE void " << class_name << "::step_" << step << "(void* state, double dt) {\n";
        oss << "    auto* st = static_cast<SimulationState*>(state);\n";
        
        if (!electrical_plan.islands.empty()) {
            emit_step_electrical_diagnostics(oss);
        }
        
        for (const auto& dev : devices) {
            oss << "    " << sanitize_name(dev.name) << ".execute(*st, dt);\n";
        }
        for (const auto& dev : devices) {
            oss << "    " << sanitize_name(dev.name) << ".commit(*st, dt);\n";
        }
        
        if (!electrical_plan.islands.empty()) {
            oss << "    st->electrical_rt = nullptr;\n";
        }
        oss << "}\n\n";
    }

    // Phase 7: Convergence check (push model: always converged)
    oss << "AOT_INLINE bool " << class_name << "::check_convergence(void* state, float tolerance) const {\n";
    oss << "    (void)state;\n";
    oss << "    (void)tolerance;\n";
    oss << "    return true;\n";
    oss << "}\n\n";

    // Phase 8: Optional debug dump for electrical islands
    if (!electrical_plan.islands.empty()) {
        oss << "void " << class_name << "::dump_island_debug(uint32_t island_idx) {\n";
        oss << "    spdlog::warn(\"[aot-elec] dump island={} entries={}\", island_idx, ELECTRICAL_DEBUG_COUNT);\n";
        oss << "    for (uint32_t i = 0; i < ELECTRICAL_DEBUG_COUNT; ++i) {\n";
        oss << "        const auto& e = ELECTRICAL_DEBUG_MAP[i];\n";
        oss << "        if (e.island_index != island_idx) continue;\n";
        oss << "        spdlog::warn(\"[aot-elec]   elem={} comp={} device={} class={} role={} nodes=({},{})\",\n";
        oss << "            e.element_index, e.component_index, e.device_name, e.classname, e.role, e.node_a, e.node_b);\n";
        oss << "    }\n";
        oss << "}\n\n";
    }

    return oss.str();
}

// ===== Section 4: write_files() =====
// Write generated header and source files to disk.
// Emits: .h and .cpp files with content from generate_header() and generate_source().
// Complexity: Low (simple file I/O, ~65 LOC).

void CodeGen::write_files(
    const std::string& out_dir,
    const std::string& source_file,
    const std::vector<DeviceInstance>& devices,
    const std::vector<Connection>& connections,
    const std::unordered_map<std::string, uint32_t>& port_to_signal,
    uint32_t signal_count,
    const ElectricalPlanCodegen& electrical_plan_arg
) {
    // Extract electrical plan if not provided
    ElectricalPlanCodegen electrical_plan = electrical_plan_arg;
    if (electrical_plan.islands.empty() && !devices.empty()) {
        electrical_plan = extract_electrical_plan(devices, port_to_signal);
    }

    // Generate header name from source file
    std::string base_name = source_file;
    size_t pos = base_name.find_last_of("/\\");
    if (pos != std::string::npos) {
        base_name = base_name.substr(pos + 1);
    }
    pos = base_name.find_last_of('.');
    if (pos != std::string::npos) {
        base_name = base_name.substr(0, pos);
    }
    std::string header_name = "generated_" + base_name + ".h";

    std::string header_path = out_dir + "/" + header_name;
    std::string source_name = "generated_" + base_name + ".cpp";
    std::string source_path = out_dir + "/" + source_name;

    std::cerr << "[codegen] Writing optimized ECS-like header to: " << header_path << "\n";
    std::cerr << "[codegen] Writing optimized ECS-like source to: " << source_path << "\n";

    // Generate header
    std::string header = generate_header(source_file, devices, connections, port_to_signal, signal_count, "Systems", electrical_plan);
    std::ofstream hfile(header_path);
    if (!hfile.is_open()) {
        std::cerr << "Failed to open: " << header_path << "\n";
        return;
    }
    hfile << header;
    hfile.close();

    // Generate source
    std::string source = generate_source(header_name, devices, connections, port_to_signal, signal_count, "Systems", electrical_plan);
    std::ofstream sfile(source_path);
    if (!sfile.is_open()) {
        std::cerr << "Failed to open: " << source_path << "\n";
        return;
    }
    sfile << source;
    sfile.close();

    std::cerr << "[codegen] Done! Generated ECS-like code with:\n";
    std::cerr << "[codegen]   - Jump table dispatch (" << 60 << " cases)\n";
    std::cerr << "[codegen]   - Domain scheduling (" << 60 << " step methods)\n";
    std::cerr << "[codegen]   - __restrict pointers (no aliasing)\n";
    std::cerr << "[codegen]   - AOT_INLINE + AOT_LIKELY/AOT_UNLIKELY\n";
    std::cerr << "[codegen]   - Push single-pass step execution\n";
    if (!electrical_plan.islands.empty()) {
        std::cerr << "[codegen]   - Electrical plan: " << electrical_plan.islands.size() << " island(s)\n";
    }
}

// ===== Section 5: generate_port_registry() and helper functions =====
// Generate a header file containing the complete port registry and ComponentVariant.
// This supports both JIT (dynamic component storage) and AOT (static analysis).
// Refactored to extract metadata arrays and lookup functions into helpers.

// ===== Helper 5a: Build component metadata from registry =====
// LOC: ~30 (extract ports and metadata from TypeRegistry)
struct PortMeta {
    std::string name;
    PortDirection direction = PortDirection::Out;
    Domain domain = Domain::Electrical;
    bool source_writer = false;
};

struct ComponentPorts {
    std::string classname;
    std::vector<PortMeta> ports;
    bool scheduler_source = false;
};

std::vector<ComponentPorts> build_component_metadata(const TypeRegistry& registry) {
    std::vector<ComponentPorts> all_components;

    for (const auto& [name, def] : registry.types) {
        if (!def.cpp_class) continue;

        ComponentPorts comp;
        comp.classname = def.classname;
        comp.scheduler_source = def.scheduler_source;
        for (const auto& [port_name, port] : def.ports) {
            PortMeta meta;
            meta.name = port_name;
            meta.direction = port.direction;
            meta.domain = port.domain;
            meta.source_writer = port.source_writer;
            comp.ports.push_back(std::move(meta));
        }
        std::sort(comp.ports.begin(), comp.ports.end(),
            [](const PortMeta& a, const PortMeta& b) {
                return a.name < b.name;
            });
        all_components.push_back(std::move(comp));
    }

    // Sort components by classname
    std::sort(all_components.begin(), all_components.end(),
        [](const ComponentPorts& a, const ComponentPorts& b) {
            return a.classname < b.classname;
        });

    return all_components;
}

// ===== Helper 5b: Emit port registry header prelude (includes, enums, constants) =====
// LOC: ~50 (header guard, includes, ComponentType enum, port count constants)
void emit_port_registry_prelude(
    std::ostringstream& oss,
    const std::vector<ComponentPorts>& all_components
) {
    oss << "// Auto-generated by codegen from library/*.blueprint\n";
    oss << "// DO NOT EDIT - changes will be overwritten\n";
    oss << "// \n";
    oss << "// This file contains port definitions for all components.\n";
    oss << "// Port offsets are calculated to ensure C++ fields match JSON registry.\n";
    oss << "\n";
    oss << "#pragma once\n";
    oss << "\n";
    oss << "#include <cstdint>\n";
    oss << "#include <string>\n";
    oss << "#include <unordered_map>\n";
    oss << "#include <unordered_set>\n";
    oss << "#include <optional>\n";
    oss << "#include <vector>\n";
    oss << "#include <variant>\n";
    oss << "\n";
    // Include Provider pattern and component definitions
    oss << "#include \"provider.h\"\n";
    oss << "#include \"all.h\"\n";
    oss << "\n";
    oss << "#include \"port_names.h\"\n";
    oss << "\n";

    // Generate enum for component types
    oss << "// Component type enumeration\n";
    oss << "enum class ComponentType {\n";
    for (size_t i = 0; i < all_components.size(); ++i) {
        oss << "    " << all_components[i].classname << ",\n";
    }
    oss << "    _COUNT  // sentinel — must be last\n";
    oss << "};\n";
    oss << "\n";

    // Generate port count constants
    oss << "// Port count for each component\n";
    for (const auto& comp : all_components) {
        oss << "constexpr size_t " << comp.classname << "_PORT_COUNT = " << comp.ports.size() << ";\n";
    }
    oss << "\n";
}

// ===== Helper 5c: Emit port names and metadata arrays =====
// LOC: ~70 (port name lists, direction/domain/source_writer arrays)
void emit_port_registry_metadata(
    std::ostringstream& oss,
    const std::vector<ComponentPorts>& all_components
) {
    // Generate port name lists
    oss << "// Port names for each component (in field declaration order)\n";
    for (const auto& comp : all_components) {
        oss << "constexpr const char* " << comp.classname << "_PORTS[] = {\n";
        for (size_t i = 0; i < comp.ports.size(); ++i) {
            oss << "    \"" << comp.ports[i].name << "\"";
            if (i < comp.ports.size() - 1) oss << ",\n";
            else oss << "\n";
        }
        oss << "};\n";
    }
    oss << "\n";

    // Generate strict metadata arrays (direction/domain/source_writer/scheduler_source)
    oss << "enum class RegistryPortDirection : uint8_t { In = 0, Out = 1, InOut = 2 };\n\n";
    for (const auto& comp : all_components) {
        oss << "constexpr RegistryPortDirection " << comp.classname << "_PORT_DIRECTIONS[] = {\n";
        for (size_t i = 0; i < comp.ports.size(); ++i) {
            const char* dir_name = "Out";
            if (comp.ports[i].direction == PortDirection::In) dir_name = "In";
            else if (comp.ports[i].direction == PortDirection::InOut) dir_name = "InOut";
            oss << "    RegistryPortDirection::" << dir_name;
            if (i < comp.ports.size() - 1) oss << ",\n";
            else oss << "\n";
        }
        oss << "};\n";

        oss << "constexpr uint8_t " << comp.classname << "_PORT_DOMAINS[] = {\n";
        for (size_t i = 0; i < comp.ports.size(); ++i) {
            oss << "    " << static_cast<int>(static_cast<uint8_t>(comp.ports[i].domain));
            if (i < comp.ports.size() - 1) oss << ",\n";
            else oss << "\n";
        }
        oss << "};\n";

        oss << "constexpr bool " << comp.classname << "_PORT_SOURCE_WRITER[] = {\n";
        for (size_t i = 0; i < comp.ports.size(); ++i) {
            oss << "    " << (comp.ports[i].source_writer ? "true" : "false");
            if (i < comp.ports.size() - 1) oss << ",\n";
            else oss << "\n";
        }
        oss << "};\n";

        oss << "constexpr bool " << comp.classname << "_SCHEDULER_SOURCE = "
            << (comp.scheduler_source ? "true" : "false") << ";\n\n";
    }
}

// ===== Helper 5d: Emit port registry lookup functions (string_to_port_name, etc.) =====
// LOC: ~60 (lookup functions: string_to_port_name, get_component_ports, etc.)
void emit_port_registry_lookups(
    std::ostringstream& oss,
    const std::vector<ComponentPorts>& all_components,
    const std::set<std::string>& all_port_names
) {
    // Generate string_to_port_name lookup
    oss << "// Convert port name string to PortNames enum\n";
    oss << "// Auto-generated from components/*.blueprint — never maintain by hand!\n";
    oss << "inline std::optional<PortNames> string_to_port_name(const std::string& name) {\n";
    oss << "    static const std::unordered_map<std::string, PortNames> map = {\n";
    for (const auto& port_name : all_port_names) {
        oss << "        {\"" << port_name << "\", PortNames::" << port_name << "},\n";
    }
    oss << "    };\n";
    oss << "    auto it = map.find(name);\n";
    oss << "    if (it != map.end()) return it->second;\n";
    oss << "    return std::nullopt;\n";
    oss << "}\n\n";

    oss << "// Get port names for a component type\n";
    oss << "inline std::vector<std::string> get_component_ports(const std::string& classname) {\n";
    oss << "    static const std::unordered_map<std::string, std::vector<std::string>> registry = {\n";
    for (const auto& comp : all_components) {
        oss << "        {\"" << comp.classname << "\", {";
        for (size_t i = 0; i < comp.ports.size(); ++i) {
            oss << "\"" << comp.ports[i].name << "\"";
            if (i < comp.ports.size() - 1) oss << ", ";
        }
        oss << "}},\n";
    }
    oss << "    };\n";
    oss << "\n";
    oss << "    auto it = registry.find(classname);\n";
    oss << "    if (it != registry.end()) {\n";
    oss << "        return it->second;\n";
    oss << "    }\n";
    oss << "    return {};\n";
    oss << "}\n";
    oss << "\n";

    oss << "inline bool has_component_metadata(const std::string& classname) {\n";
    oss << "    static const std::unordered_set<std::string> known = {\n";
    for (const auto& comp : all_components) {
        oss << "        \"" << comp.classname << "\",\n";
    }
    oss << "    };\n";
    oss << "    return known.count(classname) > 0;\n";
    oss << "}\n\n";

    oss << "inline bool is_scheduler_source_component(const std::string& classname) {\n";
    oss << "    static const std::unordered_map<std::string, bool> registry = {\n";
    for (const auto& comp : all_components) {
        oss << "        {\"" << comp.classname << "\", " << (comp.scheduler_source ? "true" : "false") << "},\n";
    }
    oss << "    };\n";
    oss << "    auto it = registry.find(classname);\n";
    oss << "    if (it == registry.end()) return false;\n";
    oss << "    return it->second;\n";
    oss << "}\n\n";

    oss << "inline std::vector<std::string> get_output_ports(const std::string& classname) {\n";
    oss << "    std::vector<std::string> result;\n";
    for (const auto& comp : all_components) {
        oss << "    if (classname == \"" << comp.classname << "\") {\n";
        oss << "        for (size_t i = 0; i < " << comp.classname << "_PORT_COUNT; ++i) {\n";
        oss << "            if (" << comp.classname << "_PORT_DIRECTIONS[i] == RegistryPortDirection::Out || "
            << comp.classname << "_PORT_DIRECTIONS[i] == RegistryPortDirection::InOut) {\n";
        oss << "                result.push_back(" << comp.classname << "_PORTS[i]);\n";
        oss << "            }\n";
        oss << "        }\n";
        oss << "        return result;\n";
        oss << "    }\n";
    }
    oss << "    return result;\n";
    oss << "}\n\n";

    oss << "inline std::vector<std::string> get_source_writer_ports(const std::string& classname, uint8_t domain_mask) {\n";
    oss << "    std::vector<std::string> result;\n";
    for (const auto& comp : all_components) {
        oss << "    if (classname == \"" << comp.classname << "\") {\n";
        oss << "        for (size_t i = 0; i < " << comp.classname << "_PORT_COUNT; ++i) {\n";
        oss << "            if (" << comp.classname << "_PORT_SOURCE_WRITER[i] && ((" << comp.classname << "_PORT_DOMAINS[i] & domain_mask) != 0)) {\n";
        oss << "                result.push_back(" << comp.classname << "_PORTS[i]);\n";
        oss << "            }\n";
        oss << "        }\n";
        oss << "        return result;\n";
        oss << "    }\n";
    }
    oss << "    return result;\n";
    oss << "}\n\n";
}

// ===== Helper 5e: Emit component variant and compile-time guard =====
// LOC: ~20 (ComponentVariant definition and static_assert)
void emit_port_registry_variant(
    std::ostringstream& oss,
    const std::vector<ComponentPorts>& all_components
) {
    // Generate ComponentVariant for dynamic component containers (Editor JIT)
    oss << "// Component variant for dynamic component storage (Editor JIT mode)\n";
    oss << "// Enables type-safe storage of any component type without virtual calls\n";
    oss << "using ComponentVariant = std::variant<\n";
    for (size_t i = 0; i < all_components.size(); ++i) {
        oss << "    " << all_components[i].classname << "<JitProvider>";
        if (i < all_components.size() - 1) oss << ",\n";
        else oss << "\n";
    }
    oss << ">;\n\n";

    // Compile-time guard: ComponentType enum and ComponentVariant must stay in sync
    oss << "// Compile-time guard: ComponentType and ComponentVariant must stay in sync\n";
    oss << "static_assert(\n";
    oss << "    std::variant_size_v<ComponentVariant> == static_cast<size_t>(ComponentType::_COUNT),\n";
    oss << "    \"ComponentType enum and ComponentVariant are out of sync — regenerate port_registry.h\"\n";
    oss << ");\n\n";
}

// ===== Helper 5f: Generate port_names.h file (breakout from circular dependency) =====
// LOC: ~40 (generates separate lightweight header with PortNames enum)
void generate_port_names_header(
    const std::string& output_path,
    const std::set<std::string>& all_port_names
) {
    std::string dir = output_path;
    auto slash = dir.find_last_of("/\\");
    if (slash != std::string::npos) {
        dir = dir.substr(0, slash + 1);
    } else {
        dir = "";
    }
    std::string port_names_path = dir + "port_names.h";

    std::ostringstream pn;
    pn << "// Auto-generated by codegen from library/*.blueprint\n";
    pn << "// DO NOT EDIT - changes will be overwritten\n";
    pn << "//\n";
    pn << "// Lightweight header containing only the PortNames enum.\n";
    pn << "// Split from port_registry.h to avoid circular includes:\n";
    pn << "//   component.h → port_registry.h → all.h → component.h\n";
    pn << "// This file has NO dependency on component headers.\n";
    pn << "\n";
    pn << "#pragma once\n";
    pn << "\n";
    pn << "#include <cstdint>\n";
    pn << "\n";
    pn << "// Port names enum (for constexpr Provider pattern)\n";
    pn << "// Used by AOT components to get compile-time port indices\n";
    pn << "enum class PortNames : uint32_t {\n";
    for (const auto& port_name : all_port_names) {
        pn << "    " << port_name << ",\n";
    }
    pn << "    _COUNT  // Sentinel: must always be last. Used to size flat arrays indexed by PortNames.\n";
    pn << "};\n";

    std::ofstream pn_out(port_names_path);
    if (pn_out.is_open()) {
        pn_out << pn.str();
        pn_out.close();
        std::cerr << "[codegen] Generated port names: " << port_names_path << "\n";
    } else {
        std::cerr << "[codegen] Warning: could not write " << port_names_path << "\n";
    }
}

// ===== Main generate_port_registry() - refactored orchestrator =====
// LOC: ~40 (clean 5-phase delegation)
void CodeGen::generate_port_registry(const TypeRegistry& registry, const std::string& output_path) {
    std::cerr << "[codegen] Generating port registry from TypeRegistry (" << registry.types.size() << " types)\n";

    // 1. Build component metadata from registry
    auto all_components = build_component_metadata(registry);

    // 2. Collect all unique port names for Param enum
    std::set<std::string> all_port_names;
    for (const auto& comp : all_components) {
        for (const auto& port : comp.ports) {
            all_port_names.insert(port.name);
        }
    }

    // 3. Generate header file
    std::ostringstream oss;
    emit_port_registry_prelude(oss, all_components);
    emit_port_registry_metadata(oss, all_components);
    emit_port_registry_lookups(oss, all_components, all_port_names);
    emit_port_registry_variant(oss, all_components);

    // 4. Write header to file
    std::ofstream out(output_path);
    if (!out.is_open()) {
        std::cerr << "[codegen] Error: could not write to " << output_path << "\n";
        return;
    }
    out << oss.str();
    out.close();
    std::cerr << "[codegen] Generated port registry: " << output_path << "\n";

    // 5. Generate port_names.h (separate file to break circular dependency)
    generate_port_names_header(output_path, all_port_names);

    // Log summary statistics
    size_t total_ports = 0;
    for (const auto& comp : all_components) {
        total_ports += comp.ports.size();
    }
    std::cerr << "[codegen]   - " << all_components.size() << " components\n";
    std::cerr << "[codegen]   - " << total_ports << " total ports\n";
}

// ===== Section 6: generate_composite_systems() and helper functions =====
// Generate header+source for composite (blueprint) systems.
// Expanded composite: sub-blueprints flattened, signals allocated via union-find.
// Refactored to extract signal allocation logic into helpers.

// ===== Helper 6a: Build initial port index map =====
// LOC: ~20 (Create port→index mapping from expanded devices)
void build_port_index_map(
    const std::vector<DeviceInstance>& expanded_devices,
    std::vector<std::string>& out_all_ports,
    std::unordered_map<std::string, uint32_t>& out_port_to_idx
) {
    for (const auto& dev : expanded_devices) {
        for (const auto& [port_name, port] : dev.ports) {
            std::string full_port = dev.name + "." + port_name;
            uint32_t idx = static_cast<uint32_t>(out_all_ports.size());
            out_all_ports.push_back(full_port);
            out_port_to_idx[full_port] = idx;
        }
    }
}

// ===== Helper 6b: UnionFind struct for signal allocation =====
// LOC: ~20 (Encapsulate UF logic for clarity)
struct UnionFind {
    mutable std::vector<uint32_t> parent;
    std::vector<uint32_t> rank;

    UnionFind(size_t size) : parent(size), rank(size, 0) {
        for (uint32_t i = 0; i < static_cast<uint32_t>(size); ++i) {
            parent[i] = i;
        }
    }

    uint32_t find(uint32_t x) const {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    }

    void unite(uint32_t a, uint32_t b) {
        uint32_t ra = find(a), rb = find(b);
        if (ra == rb) return;
        if (rank[ra] < rank[rb]) std::swap(ra, rb);
        parent[rb] = ra;
        if (rank[ra] == rank[rb]) rank[ra]++;
    }
};

// ===== Helper 6c: Apply union rules for signal allocation =====
// LOC: ~50 (BlueprintInput/Output, connections, aliases unification)
void apply_signal_allocation_rules(
    UnionFind& uf,
    const std::vector<DeviceInstance>& expanded_devices,
    const std::vector<Connection>& expanded_connections,
    const std::unordered_map<std::string, uint32_t>& port_to_idx
) {
    // === PARITY GUARD: BlueprintInput/Output Bridge Union ===
    // INVARIANT: ext↔port union MUST match JIT solver's logic.
    // [CRITICAL] This code must remain in sync with jit_solver.cpp bridge unification.
    for (const auto& dev : expanded_devices) {
        if (dev.classname == "BlueprintInput" || dev.classname == "BlueprintOutput") {
            std::string ext_key  = dev.name + ".ext";
            std::string port_key = dev.name + ".port";
            auto it_ext  = port_to_idx.find(ext_key);
            auto it_port = port_to_idx.find(port_key);
            if (it_ext != port_to_idx.end() && it_port != port_to_idx.end()) {
                uf.unite(it_ext->second, it_port->second);
            }
        }
    }

    // Union connected ports
    for (const auto& conn : expanded_connections) {
        auto it_from = port_to_idx.find(conn.from);
        auto it_to = port_to_idx.find(conn.to);
        if (it_from != port_to_idx.end() && it_to != port_to_idx.end()) {
            uf.unite(it_from->second, it_to->second);
        }
    }

    // Union alias ports within same device
    for (const auto& dev : expanded_devices) {
        for (const auto& [port_name, port] : dev.ports) {
            if (port.alias.has_value() && !port.alias->empty()) {
                std::string full_port = dev.name + "." + port_name;
                std::string full_alias = dev.name + "." + *port.alias;
                auto it_port = port_to_idx.find(full_port);
                auto it_alias = port_to_idx.find(full_alias);
                if (it_port != port_to_idx.end() && it_alias != port_to_idx.end()) {
                    uf.unite(it_port->second, it_alias->second);
                }
            }
        }
    }
}

// ===== Helper 6d: Remap UF roots to sequential signal indices =====
// LOC: ~25 (Map root→signal, then finalize port_to_signal)
std::unordered_map<std::string, uint32_t> finalize_signal_indices(
    const UnionFind& uf,
    const std::vector<std::string>& all_ports,
    const std::unordered_map<std::string, uint32_t>& port_to_idx,
    uint32_t& out_signal_count
) {
    std::unordered_map<std::string, uint32_t> port_to_signal;
    for (const auto& port : all_ports) {
        port_to_signal[port] = uf.find(port_to_idx.at(port));
    }

    std::map<uint32_t, uint32_t> root_to_signal;
    std::vector<uint32_t> unique_roots;
    for (const auto& [port, root] : port_to_signal) {
        unique_roots.push_back(root);
    }
    std::sort(unique_roots.begin(), unique_roots.end());
    unique_roots.erase(std::unique(unique_roots.begin(), unique_roots.end()), unique_roots.end());

    uint32_t next_signal = 0;
    for (uint32_t root : unique_roots) {
        root_to_signal[root] = next_signal++;
    }
    for (auto& [port, sig] : port_to_signal) {
        sig = root_to_signal[sig];
    }
    out_signal_count = next_signal;

    return port_to_signal;
}

// ===== Main generate_composite_systems() - refactored orchestrator =====
// LOC: ~50 (clean 5-phase delegation)
CompositeCodegenResult CodeGen::generate_composite_systems(
    const TypeDefinition& td,
    const TypeRegistry& registry)
{
    // 1. Expand sub-blueprint references into flat devices + connections
    std::set<std::string> loading_stack;
    auto expanded = expand_sub_blueprint_references(td, registry, loading_stack);

    // 2. Merge each device with its type definition (ports, params, domains)
    for (auto& dev : expanded.devices) {
        const auto* type_def = registry.get(dev.classname);
        if (type_def) {
            dev = merge_device_instance(dev, *type_def);
        }
    }

    // 3. Signal allocation (union-find) — same algorithm as build_systems_dev
    std::vector<std::string> all_ports;
    std::unordered_map<std::string, uint32_t> port_to_idx;
    build_port_index_map(expanded.devices, all_ports, port_to_idx);

    UnionFind uf(all_ports.size());
    apply_signal_allocation_rules(uf, expanded.devices, expanded.connections, port_to_idx);

    uint32_t signal_count = 0;
    auto port_to_signal = finalize_signal_indices(uf, all_ports, port_to_idx, signal_count);

    // 4. Extract electrical island plan (for AOT solve_electrical)
    ElectricalPlanCodegen electrical_plan = extract_electrical_plan(expanded.devices, port_to_signal);

    // 5. Delegate to existing generate_header/generate_source
    std::string class_name = sanitize_name(td.classname) + "_Systems";
    std::string source_file = td.classname + ".blueprint";
    std::string header_name = "generated_" + sanitize_name(td.classname) + ".h";

    CompositeCodegenResult result;
    result.class_name = class_name;
    result.header = generate_header(source_file, expanded.devices, expanded.connections,
                                    port_to_signal, signal_count, class_name, electrical_plan);
    result.source = generate_source(header_name, expanded.devices, expanded.connections,
                                    port_to_signal, signal_count, class_name, electrical_plan);
    return result;
}

std::map<std::string, CompositeCodegenResult> CodeGen::generate_all_composites(
    const TypeRegistry& registry)
{
    std::map<std::string, CompositeCodegenResult> results;

    auto order = registry.get_composites_topo_sorted();

    for (const auto& name : order) {
        const auto* td = registry.get(name);
        if (td && !td->cpp_class) {
            results[name] = generate_composite_systems(*td, registry);
        }
    }

    return results;
}
