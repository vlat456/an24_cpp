#include "codegen.h"
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


namespace {

std::string to_upper(const std::string& s) {
    std::string result = s;
    for (char& c : result) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return result;
}

std::string sanitize_name(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '.': result += "_DOT_"; break;
            case '-': result += "_DASH_"; break;
            case ':': result += "_"; break;  // Colon is the hierarchical separator, stays as '_'
            default:  result += c; break;
        }
    }
    return result;
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

// == Electrical plan extraction for AOT codegen ==
// Mirrors the island extraction logic in jit_solver.cpp build_systems_dev().
// This runs at codegen time to produce static electrical plan arrays.

namespace {

float parse_float_codegen(const std::string& value, float default_val) {
    if (value.empty()) return default_val;
    char* end = nullptr;
    float f = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0') {
        return default_val;
    }
    return f;
}

} // anonymous namespace

ElectricalPlanCodegen extract_electrical_plan(
    const std::vector<DeviceInstance>& devices,
    const std::unordered_map<std::string, uint32_t>& port_to_signal
) {
    ElectricalPlanCodegen plan;

    struct RawElement {
        ElectricalElementKindCodegen kind;
        uint32_t node_a;
        uint32_t node_b;
        float value_a;
        float value_b;
        size_t component_index;
    };

    std::vector<RawElement> raw_elements;

    // Check whether a device has ANY ports in the signal map.
    // Devices without ports (e.g., test stubs not merged with type definitions)
    // are benign skips. Devices WITH ports that are missing a specific electrical
    // port indicate a bug (typo in port name, incomplete type definition, etc.).
    auto device_has_any_ports = [&](const DeviceInstance& dev) -> bool {
        for (const auto& [port_name, port] : dev.ports) {
            std::string full_port = dev.name + "." + port_name;
            if (port_to_signal.find(full_port) != port_to_signal.end()) {
                return true;
            }
        }
        return false;
    };

    auto resolve_port = [&](const DeviceInstance& dev, const std::string& port_name) -> std::optional<uint32_t> {
        const std::string full_port = dev.name + "." + port_name;
        auto it = port_to_signal.find(full_port);
        if (it == port_to_signal.end()) {
            spdlog::warn("[codegen] electrical port '{}' not found in signal map for device '{}' (classname: {}). Element skipped from electrical plan.",
                full_port, dev.name, dev.classname);
            return std::nullopt;
        }
        return it->second;
    };

    size_t element_idx = 0;
    for (const auto& dev : devices) {
        if (dev.visual_only) continue;

        // Skip devices with no ports in the signal map (e.g., test stubs
        // that were never merged with their type definition). These devices
        // can't participate in the electrical solve because they have no
        // signal bindings. This is distinct from a device that HAS ports
        // but is missing a specific electrical port — that case is warned
        // below and indicates a likely bug.
        if (!device_has_any_ports(dev)) continue;

        // == Path 1: Metadata-driven extraction via solver_role ==
        if (dev.solver_role.has_value()) {
            const auto& role = *dev.solver_role;

            if (role.kind == "FixedVoltageNode") {
                float value = 0.0f;
                auto it_val = role.param_map.find("voltage");
                if (it_val != role.param_map.end()) {
                    auto it_param = dev.params.find(it_val->second);
                    if (it_param != dev.params.end()) {
                        value = parse_float_codegen(it_param->second, 0.0f);
                    }
                }
                auto node_a_opt = resolve_port(dev, role.port_map.at("node"));
                if (!node_a_opt.has_value()) continue;
                raw_elements.push_back({
                    ElectricalElementKindCodegen::FixedVoltageNode,
                    *node_a_opt, UINT32_MAX, value, 0.0f, element_idx++
                });
            }
            else if (role.kind == "TheveninSource") {
                float voltage = 28.0f;
                float resistance = 0.01f;
                auto it_v = role.param_map.find("voltage");
                if (it_v != role.param_map.end()) {
                    auto it_param = dev.params.find(it_v->second);
                    if (it_param != dev.params.end()) {
                        voltage = parse_float_codegen(it_param->second, 28.0f);
                    }
                }
                auto it_r = role.param_map.find("resistance");
                if (it_r != role.param_map.end()) {
                    auto it_param = dev.params.find(it_r->second);
                    if (it_param != dev.params.end()) {
                        resistance = parse_float_codegen(it_param->second, 0.01f);
                    }
                }
                auto node_pos_opt = resolve_port(dev, role.port_map.at("pos"));
                auto node_neg_opt = resolve_port(dev, role.port_map.at("neg"));
                if (!node_pos_opt.has_value() || !node_neg_opt.has_value()) continue;
                raw_elements.push_back({
                    ElectricalElementKindCodegen::TheveninSource,
                    *node_pos_opt, *node_neg_opt, voltage, resistance, element_idx++
                });
            }
            else if (role.kind == "ConductanceBranch") {
                float conductance = 0.1f;
                auto it_g = role.param_map.find("g");
                if (it_g != role.param_map.end()) {
                    auto it_param = dev.params.find(it_g->second);
                    if (it_param != dev.params.end()) {
                        conductance = parse_float_codegen(it_param->second, 0.1f);
                    }
                }
                auto node_a_opt = resolve_port(dev, role.port_map.at("a"));
                auto node_b_opt = resolve_port(dev, role.port_map.at("b"));
                if (!node_a_opt.has_value() || !node_b_opt.has_value()) continue;
                raw_elements.push_back({
                    ElectricalElementKindCodegen::ConductanceBranch,
                    *node_a_opt, *node_b_opt, conductance, 0.0f, element_idx++
                });
            }
            continue;
        }

        // == Path 2: Classname-based fallback for wrapper components ==
        if (dev.classname == "Battery") {
            float v_nominal = parse_float_codegen(dev.params.count("v_nominal") ?
                dev.params.at("v_nominal") : "", 28.0f);
            float internal_r = parse_float_codegen(dev.params.count("internal_r") ?
                dev.params.at("internal_r") : "", 0.01f);
            auto node_pos_opt = resolve_port(dev, "v_out");
            auto node_neg_opt = resolve_port(dev, "v_in");
            if (!node_pos_opt.has_value() || !node_neg_opt.has_value()) continue;
            raw_elements.push_back({
                ElectricalElementKindCodegen::TheveninSource,
                *node_pos_opt, *node_neg_opt, v_nominal, internal_r, element_idx++
            });
        }
        else if (dev.classname == "Generator") {
            float v_nominal = parse_float_codegen(dev.params.count("v_nominal") ?
                dev.params.at("v_nominal") : "", 28.5f);
            float internal_r = parse_float_codegen(dev.params.count("internal_r") ?
                dev.params.at("internal_r") : "", 0.005f);
            auto node_pos_opt = resolve_port(dev, "v_out");
            auto node_neg_opt = resolve_port(dev, "v_in");
            if (!node_pos_opt.has_value() || !node_neg_opt.has_value()) continue;
            raw_elements.push_back({
                ElectricalElementKindCodegen::TheveninSource,
                *node_pos_opt, *node_neg_opt, v_nominal, internal_r, element_idx++
            });
        }
        else if (dev.classname == "RefNode") {
            float value = parse_float_codegen(dev.params.count("value") ?
                dev.params.at("value") : "", 0.0f);
            auto node_a_opt = resolve_port(dev, "v");
            if (!node_a_opt.has_value()) continue;
            raw_elements.push_back({
                ElectricalElementKindCodegen::FixedVoltageNode,
                *node_a_opt, UINT32_MAX, value, 0.0f, element_idx++
            });
        }
        else if (dev.classname == "Resistor") {
            float conductance = parse_float_codegen(dev.params.count("conductance") ?
                dev.params.at("conductance") : "", 0.1f);
            auto node_a_opt = resolve_port(dev, "v_in");
            auto node_b_opt = resolve_port(dev, "v_out");
            if (!node_a_opt.has_value() || !node_b_opt.has_value()) continue;
            raw_elements.push_back({
                ElectricalElementKindCodegen::ConductanceBranch,
                *node_a_opt, *node_b_opt, conductance, 0.0f, element_idx++
            });
        }
        else if (dev.classname == "IndicatorLight") {
            float conductance = parse_float_codegen(dev.params.count("conductance") ?
                dev.params.at("conductance") : "", 1.0f);
            auto node_a_opt = resolve_port(dev, "v_in");
            auto node_b_opt = resolve_port(dev, "v_out");
            if (!node_a_opt.has_value() || !node_b_opt.has_value()) continue;
            raw_elements.push_back({
                ElectricalElementKindCodegen::ConductanceBranch,
                *node_a_opt, *node_b_opt, conductance, 0.0f, element_idx++
            });
        }
        else if (dev.classname == "CurrentSense") {
            float conductance = parse_float_codegen(dev.params.count("conductance") ?
                dev.params.at("conductance") : "", 1000.0f);
            auto node_a_opt = resolve_port(dev, "v_in");
            auto node_b_opt = resolve_port(dev, "v_out");
            if (!node_a_opt.has_value() || !node_b_opt.has_value()) continue;
            raw_elements.push_back({
                ElectricalElementKindCodegen::ConductanceBranch,
                *node_a_opt, *node_b_opt, conductance, 0.0f, element_idx++
            });
        }
        else if (dev.classname == "ElectricalConductance") {
            float conductance = parse_float_codegen(dev.params.count("conductance") ?
                dev.params.at("conductance") : "", 0.1f);
            auto node_a_opt = resolve_port(dev, "v_in");
            auto node_b_opt = resolve_port(dev, "v_out");
            if (!node_a_opt.has_value() || !node_b_opt.has_value()) continue;
            raw_elements.push_back({
                ElectricalElementKindCodegen::ConductanceBranch,
                *node_a_opt, *node_b_opt, conductance, 0.0f, element_idx++
            });
        }
        else if (dev.classname == "ElectricalSource") {
            float voltage = parse_float_codegen(dev.params.count("voltage") ?
                dev.params.at("voltage") : "", 28.0f);
            float resistance = parse_float_codegen(dev.params.count("resistance") ?
                dev.params.at("resistance") : "", 0.01f);
            auto node_pos_opt = resolve_port(dev, "v_out");
            auto node_neg_opt = resolve_port(dev, "v_in");
            if (!node_pos_opt.has_value() || !node_neg_opt.has_value()) continue;
            raw_elements.push_back({
                ElectricalElementKindCodegen::TheveninSource,
                *node_pos_opt, *node_neg_opt, voltage, resistance, element_idx++
            });
        }
    }

    if (raw_elements.empty()) {
        return plan;
    }

    // Union-find over node indices to group into islands
    std::unordered_map<uint32_t, uint32_t> uf_parent;
    std::unordered_map<uint32_t, uint32_t> uf_rank;
    std::unordered_set<uint32_t> all_nodes;
    for (const auto& elem : raw_elements) {
        all_nodes.insert(elem.node_a);
        if (elem.node_b != UINT32_MAX) all_nodes.insert(elem.node_b);
    }
    for (uint32_t n : all_nodes) {
        uf_parent[n] = n;
        uf_rank[n] = 0;
    }
    auto uf_find = [&](uint32_t x) {
        while (uf_parent[x] != x) {
            uf_parent[x] = uf_parent[uf_parent[x]];
            x = uf_parent[x];
        }
        return x;
    };
    auto uf_unite = [&](uint32_t a, uint32_t b) {
        uint32_t ra = uf_find(a), rb = uf_find(b);
        if (ra == rb) return;
        if (uf_rank[ra] < uf_rank[rb]) std::swap(ra, rb);
        uf_parent[rb] = ra;
        if (uf_rank[ra] == uf_rank[rb]) uf_rank[ra]++;
    };
    for (const auto& elem : raw_elements) {
        if (elem.node_b != UINT32_MAX) {
            uf_unite(elem.node_a, elem.node_b);
        }
    }

    // Group elements by island (root node)
    std::map<uint32_t, std::vector<size_t>> island_members;
    for (size_t i = 0; i < raw_elements.size(); ++i) {
        uint32_t root = uf_find(raw_elements[i].node_a);
        island_members[root].push_back(i);
    }

    // Sort islands by smallest node for determinism
    std::vector<std::pair<uint32_t, std::vector<size_t>>> sorted_islands(
        island_members.begin(), island_members.end());
    std::sort(sorted_islands.begin(), sorted_islands.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    for (const auto& [root, elem_indices] : sorted_islands) {
        (void)root;
        ElectricalIslandPlanCodegen island;

        std::set<uint32_t> island_nodes;
        for (size_t idx : elem_indices) {
            island_nodes.insert(raw_elements[idx].node_a);
            if (raw_elements[idx].node_b != UINT32_MAX) {
                island_nodes.insert(raw_elements[idx].node_b);
            }
        }
        island.signal_indices.assign(island_nodes.begin(), island_nodes.end());

        std::vector<size_t> sorted_indices = elem_indices;
        std::sort(sorted_indices.begin(), sorted_indices.end());
        for (size_t idx : sorted_indices) {
            const auto& re = raw_elements[idx];
            island.elements.push_back({
                re.kind,
                re.node_a,
                re.node_b,
                re.value_a,
                re.value_b,
                static_cast<uint32_t>(re.component_index)
            });
        }

        plan.islands.push_back(std::move(island));
    }

    return plan;
}

std::string CodeGen::generate_header(
    const std::string& source_file,
    const std::vector<DeviceInstance>& devices_unfiltered,
    const std::vector<Connection>& connections,
    const std::unordered_map<std::string, uint32_t>& port_to_signal,
    uint32_t signal_count,
    const std::string& class_name,
    const ElectricalPlanCodegen& electrical_plan
) {
    // Filter out visual-only devices (no simulation behavior, e.g. Group)
    std::vector<DeviceInstance> devices;
    devices.reserve(devices_unfiltered.size());
    for (const auto& d : devices_unfiltered)
        if (!d.visual_only) devices.push_back(d);

    std::ostringstream oss;

    // Header guard
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
    oss << "#include \"jit_solver/state.h\"\n";
    oss << "#include \"jit_solver/components/all.h\"\n";
    oss << "#include \"jit_solver/components/port_registry.h\"\n\n";
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

    // Signal constants
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
            std::string port_key = dev.name + ".v_out";
            if (port_to_signal.count(port_key)) {
                if (!first) oss << ", ";
                oss << port_to_signal.at(port_key);
                first = false;
            } else {
                port_key = dev.name + ".v";
                if (port_to_signal.count(port_key)) {
                    if (!first) oss << ", ";
                    oss << port_to_signal.at(port_key);
                    first = false;
                }
            }
        }
    }
    oss << "};\n\n";

    // Signal count
    oss << "/// Total number of unique signals (for memory allocation)\n";
    oss << "constexpr uint32_t SIGNAL_COUNT = " << signal_count << ";\n\n";

    // Device count
    oss << "/// Number of devices in this system\n";
    oss << "constexpr uint32_t DEVICE_COUNT = " << devices.size() << ";\n\n";

    // Electrical plan — static island data for AOT solve_electrical()
    if (!electrical_plan.islands.empty()) {
        oss << "// ==============================================================================\n";
        oss << "// ELECTRICAL ISLAND PLAN (AOT static arrays)\n";
        oss << "// ==============================================================================\n\n";

        oss << "constexpr uint32_t ELECTRICAL_ISLAND_COUNT = "
            << electrical_plan.islands.size() << ";\n\n";

        // Emit element kind enum for codegen use in static arrays
        // (runtime uses ElectricalElementKind which has same values)
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

        // ElectricalBuildPlan struct using static arrays
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
    } else {
        oss << "constexpr uint32_t ELECTRICAL_ISLAND_COUNT = 0;\n\n";
    }

    // Global simulation state pointer (set at init, available globally)
    oss << "/// Global simulation state pointer (set once, used by all components)\n";
    oss << "extern SimulationState* g_state;\n\n";

    // Systems class - ECS-like optimized
    oss << "// ==============================================================================\n";
    oss << "// SYSTEMS CLASS (ECS-like: direct field access, no virtual calls)\n";
    oss << "// Components are NON-VIRTUAL for AOT - no vtable overhead\n";
    oss << "// ==============================================================================\n\n";

    oss << "class " << class_name << " {\n";
    oss << "public:\n";

    // Device objects with AotProvider - compile-time constexpr port index lookup
    // Zero-cost abstraction: provider.get(PortNames::v_in) compiles to a constant
    for (const auto& dev : devices) {
        std::string aot_type = generate_aot_provider_type(dev, port_to_signal, signal_count);
        oss << "    " << dev.classname << "<" << aot_type << "> " << sanitize_name(dev.name) << ";\n";
    }
    oss << "\n";

    // Port indices as flat arrays (DATA-ORIENTED: cache-friendly, no indirection)
    oss << "    // Port indices - stored separately for direct access (data-oriented)\n";
    oss << "    // This allows O(1) access without loading from object fields\n";
    for (const auto& dev : devices) {
        for (const auto& port : dev.ports) {
            const std::string& port_name = port.first;
            if (port.second.alias.has_value() && !port.second.alias.value().empty()) {
                continue;  // Skip alias ports
            }
            std::string port_key = dev.name + "." + port_name;
            uint32_t sig = port_to_signal.count(port_key) ? port_to_signal.at(port_key) : signal_count;
            oss << "    static constexpr uint32_t " << sanitize_name(dev.name) << "_" << port_name << "_idx = " << sig << ";\n";
        }
    }
    oss << "\n";

    // Global simulation step counter (not modulo cycle index)
    oss << "    uint32_t step_counter_ = 0;\n\n";

    // Electrical plan and runtime state (for AOT solve_electrical)
    if (!electrical_plan.islands.empty()) {
        oss << "    AotElectricalPlan electrical_plan_;\n";
        oss << "    ElectricalRuntimeState electrical_rt_;\n";
    }
    oss << "\n";

    // Constructor / Destructor
    oss << "    " << class_name << "();\n";
    oss << "    ~" << class_name << "();\n\n";

    // Non-copyable (owns raw pointer)
    oss << "    " << class_name << "(const " << class_name << "&) = delete;\n";
    oss << "    " << class_name << "& operator=(const " << class_name << "&) = delete;\n\n";

    // Methods - all inline for optimization
    oss << "    /// Pre-load initialization\n";
    oss << "    void pre_load();\n\n";

    oss << "    /// Main solve step with jump table dispatch (ECS-like)\n";
    oss << "    void solve_step(void* state, uint32_t step, float dt);\n\n";

    // Generate CYCLE_LENGTH step methods
    for (int step = 0; step < 60; ++step) {
        oss << "    AOT_INLINE void step_" << step << "(void* state, float dt);\n";
    }
    oss << "\n";

    oss << "    /// Convergence check (sparse sampling)\n";
    oss << "    AOT_INLINE bool check_convergence(void* state, float tolerance) const;\n\n";

    oss << "    uint32_t component_count() const { return " << devices.size() << "; }\n";

    oss << "};\n\n";

    return oss.str();
}

std::string CodeGen::generate_source(
    const std::string& header_name,
    const std::vector<DeviceInstance>& devices_unfiltered,
    const std::vector<Connection>& connections,
    const std::unordered_map<std::string, uint32_t>& port_to_signal,
    uint32_t signal_count,
    const std::string& class_name,
    const ElectricalPlanCodegen& electrical_plan
) {
    // Filter out visual-only devices (no simulation behavior, e.g. Group)
    std::vector<DeviceInstance> devices;
    devices.reserve(devices_unfiltered.size());
    for (const auto& d : devices_unfiltered)
        if (!d.visual_only) devices.push_back(d);

    std::ostringstream oss;

    oss << "#include \"" << header_name << "\"\n";
    // Include template definitions from all.cpp so compiler can instantiate AotProvider versions
    oss << "#include \"jit_solver/components/all.cpp\"\n";
    // Electrical subsolver for AOT island solve
    oss << "#include \"jit_solver/subsolvers/electrical_subsolver.h\"\n";
    oss << "#include <cstring>  // memcpy\n\n";
    // Enable fast-math for generated code only (not spdlog)
    oss << "#ifdef __GNUC__\n";
    oss << "#pragma GCC optimize(\"fast-math,unroll-loops\")\n";
    oss << "#endif\n\n";

    // Explicit template instantiations for AotProvider
    // These tell the compiler to generate code for Component<AotProvider<Bindings...>>
    oss << "// Explicit template instantiations for AOT\n";
    for (const auto& dev : devices) {
        std::string aot_type = generate_aot_provider_type(dev, port_to_signal, signal_count);
        oss << "template class " << dev.classname << "<" << aot_type << ">;\n";
    }
    oss << "\n";

    // Constructor - initialize component parameters (port indices are compile-time constants)
    oss << class_name << "::" << class_name << "()\n";
    oss << "{\n";

    // Port indices are now static constexpr - no runtime initialization needed!

    // Collect LUT tables for arena generation
    struct LutEntry { std::string dev_name; std::vector<float> keys; std::vector<float> values; };
    std::vector<LutEntry> lut_entries;
    uint32_t lut_arena_offset = 0;

    // Generate parameter assignments
    for (const auto& dev : devices) {
        // LUT: parse table param into arena, emit offset/size instead
        if (dev.classname == "LUT") {
            auto it = dev.params.find("table");
            if (it != dev.params.end()) {
                LutEntry entry;
                entry.dev_name = sanitize_name(dev.name);
                // Inline parse (same format as LUT::parse_table)
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
                    } else { break; }
                    pos = end;
                }
                oss << "    " << entry.dev_name << ".table_offset = " << lut_arena_offset << ";\n";
                oss << "    " << entry.dev_name << ".table_size = " << entry.keys.size() << ";\n";
                lut_arena_offset += static_cast<uint32_t>(entry.keys.size());
                lut_entries.push_back(std::move(entry));
            }
            continue;  // skip generic param loop for LUT
        }

        for (const auto& param : dev.params) {
            const std::string& param_name = param.first;
            const std::string& value = param.second;

            // Skip internal computed fields
            if (param_name == "inv_internal_r" || param_name == "inv_capacity") continue;

            std::string type = infer_type(value);
            oss << "    " << sanitize_name(dev.name) << "." << param_name << " = " << format_value(value, type) << ";\n";
        }
    }
    oss << "}\n\n";

    // Destructor
    oss << class_name << "::~" << class_name << "() {}\n\n";

    // Pre-load: call pre_load() on components that have it, then LUT arena init
    oss << "void " << class_name << "::pre_load() {\n";
    // All cpp_class components have pre_load() (empty stub or real implementation)
    for (const auto& dev : devices) {
        oss << "    " << sanitize_name(dev.name) << ".pre_load();\n";
    }
    // Emit LUT arena initialization
    if (!lut_entries.empty()) {
        oss << "    // LUT arena: all breakpoint tables concatenated (" << lut_arena_offset << " floats total)\n";
        oss << "    static const float lut_keys_data[] = {";
        bool first_k = true;
        for (const auto& e : lut_entries) {
            for (float k : e.keys) {
                if (!first_k) oss << ", ";
                oss << k << "f";
                first_k = false;
            }
        }
        oss << "};\n";
        oss << "    static const float lut_vals_data[] = {";
        bool first_v = true;
        for (const auto& e : lut_entries) {
            for (float v : e.values) {
                if (!first_v) oss << ", ";
                oss << v << "f";
                first_v = false;
            }
        }
        oss << "};\n";
        oss << "    g_state->lut_keys.assign(lut_keys_data, lut_keys_data + " << lut_arena_offset << ");\n";
        oss << "    g_state->lut_values.assign(lut_vals_data, lut_vals_data + " << lut_arena_offset << ");\n";
    }
    oss << "}\n\n";

    // Jump table dispatch - computed goto (GCC/Clang) or switch fallback (MSVC)
    oss << "void " << class_name << "::solve_step(void* state, uint32_t step, float dt) {\n";
    oss << "    if (dt <= 0.0f) return;\n";
    oss << "\n";
    oss << "#ifndef _MSC_VER\n";
    oss << "    // Computed goto dispatch table (static const for one-time init)\n";
    oss << "    static const void* dispatch_table[" << 60 << "] = {\n";
    for (int i = 0; i < 60; ++i) {
        oss << "        &&step_" << i << (i < 60 - 1 ? ",\n" : "\n");
    }
    oss << "    };\n\n";
    oss << "    // Direct jump - no bounds check needed (step % " << 60 << " is always 0-" << 60 - 1 << ")\n";
    oss << "    goto *dispatch_table[step % " << 60 << "];\n\n";
    for (int i = 0; i < 60; ++i) {
        oss << "    step_" << i << ":\n";
        oss << "        step_" << i << "(state, dt);\n";
        oss << "        ++step_counter_;\n";
        oss << "        return;\n\n";
    }
    oss << "#else\n";
    oss << "    // MSVC fallback: switch-based dispatch\n";
    oss << "    switch (step % " << 60 << ") {\n";
    for (int i = 0; i < 60; ++i) {
        oss << "        case " << i << ": step_" << i << "(state, dt); ++step_counter_; return;\n";
    }
    oss << "    }\n";
    oss << "#endif\n";
    oss << "}\n\n";

    // Push model: single-pass per frame, sources/consumers already encoded in
    // component execute() behavior and device ordering generated from blueprint.
    for (int step = 0; step < 60; ++step) {
        oss << "AOT_INLINE void " << class_name << "::step_" << step << "(void* state, float dt) {\n";
        oss << "    auto* st = static_cast<SimulationState*>(state);\n";
        // Electrical solve: compute node voltages and branch currents before component execute
        if (!electrical_plan.islands.empty()) {
            oss << "    st->electrical_rt = &electrical_rt_;\n";
            oss << "    solve_electrical(electrical_plan_.islands, *st, electrical_rt_, dt);\n";
        }
        for (const auto& dev : devices) {
            oss << "    " << sanitize_name(dev.name) << ".execute(*st, dt);\n";
        }
        oss << "}\n\n";
    }

    // Push model single-pass has no iterative convergence stage.
    oss << "AOT_INLINE bool " << class_name << "::check_convergence(void* state, float tolerance) const {\n";
    oss << "    (void)state;\n";
    oss << "    (void)tolerance;\n";
    oss << "    return true;\n";
    oss << "}\n\n";

    return oss.str();
}

void CodeGen::write_files(
    const std::string& out_dir,
    const std::string& source_file,
    const std::vector<DeviceInstance>& devices,
    const std::vector<Connection>& connections,
    const std::unordered_map<std::string, uint32_t>& port_to_signal,
    uint32_t signal_count
) {
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
    std::string header = generate_header(source_file, devices, connections, port_to_signal, signal_count);
    std::ofstream hfile(header_path);
    if (!hfile.is_open()) {
        std::cerr << "Failed to open: " << header_path << "\n";
        return;
    }
    hfile << header;
    hfile.close();

    // Generate source
    std::string source = generate_source(header_name, devices, connections, port_to_signal, signal_count);
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
}

void CodeGen::generate_port_registry(const TypeRegistry& registry, const std::string& output_path) {
    std::cerr << "[codegen] Generating port registry from TypeRegistry (" << registry.types.size() << " types)\n";

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

    // Collect all unique port names for Param enum
    std::set<std::string> all_port_names;
    for (const auto& comp : all_components) {
        for (const auto& port : comp.ports) {
            all_port_names.insert(port.name);
        }
    }

    // Generate header file
    std::ostringstream oss;

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
    // These are relative to port_registry.h location (src/jit_solver/components/)
    oss << "#include \"provider.h\"\n";
    oss << "#include \"all.h\"\n";
    oss << "\n";

    // Generate enum for all port names (for Provider pattern)
    oss << "// Port names enum (for constexpr Provider pattern)\n";
    oss << "// Used by AOT components to get compile-time port indices\n";
    oss << "enum class PortNames : uint32_t {\n";
    size_t param_idx = 0;
    for (const auto& port_name : all_port_names) {
        oss << "    " << port_name;
        if (param_idx < all_port_names.size() - 1) oss << ",\n";
        else oss << "\n";
        param_idx++;
    }
    oss << "};\n";
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

    // Generate helper function to get ports by classname
    oss << "// Get port names for a component type\n";

    // Generate string_to_port_name lookup (auto-generated, never hand-maintain!)
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

    // Write to file
    std::ofstream out(output_path);
    if (!out.is_open()) {
        std::cerr << "[codegen] Error: could not write to " << output_path << "\n";
        return;
    }

    out << oss.str();
    out.close();

    std::cerr << "[codegen] Generated port registry: " << output_path << "\n";
    std::cerr << "[codegen]   - " << all_components.size() << " components\n";

    // Count total ports
    size_t total_ports = 0;
    for (const auto& comp : all_components) {
        total_ports += comp.ports.size();
    }
    std::cerr << "[codegen]   - " << total_ports << " total ports\n";
}

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

    for (const auto& dev : expanded.devices) {
        for (const auto& [port_name, port] : dev.ports) {
            std::string full_port = dev.name + "." + port_name;
            uint32_t idx = static_cast<uint32_t>(all_ports.size());
            all_ports.push_back(full_port);
            port_to_idx[full_port] = idx;
        }
    }

    // Union-Find
    std::vector<uint32_t> uf_parent(all_ports.size());
    std::vector<uint32_t> uf_rank(all_ports.size(), 0);
    for (uint32_t i = 0; i < all_ports.size(); ++i) uf_parent[i] = i;

    auto uf_find = [&](uint32_t x) -> uint32_t {
        while (uf_parent[x] != x) {
            uf_parent[x] = uf_parent[uf_parent[x]];
            x = uf_parent[x];
        }
        return x;
    };
    auto uf_unite = [&](uint32_t a, uint32_t b) {
        uint32_t ra = uf_find(a), rb = uf_find(b);
        if (ra == rb) return;
        if (uf_rank[ra] < uf_rank[rb]) std::swap(ra, rb);
        uf_parent[rb] = ra;
        if (uf_rank[ra] == uf_rank[rb]) uf_rank[ra]++;
    };

    // Union connected ports
    for (const auto& conn : expanded.connections) {
        auto it_from = port_to_idx.find(conn.from);
        auto it_to = port_to_idx.find(conn.to);
        if (it_from != port_to_idx.end() && it_to != port_to_idx.end()) {
            uf_unite(it_from->second, it_to->second);
        }
    }

    // Union alias ports within same device
    for (const auto& dev : expanded.devices) {
        for (const auto& [port_name, port] : dev.ports) {
            if (port.alias.has_value() && !port.alias->empty()) {
                std::string full_port = dev.name + "." + port_name;
                std::string full_alias = dev.name + "." + *port.alias;
                auto it_port = port_to_idx.find(full_port);
                auto it_alias = port_to_idx.find(full_alias);
                if (it_port != port_to_idx.end() && it_alias != port_to_idx.end()) {
                    uf_unite(it_port->second, it_alias->second);
                }
            }
        }
    }

    // Map each port → root, then remap roots to sequential 0-based signal indices
    std::unordered_map<std::string, uint32_t> port_to_signal;
    for (const auto& port : all_ports) {
        port_to_signal[port] = uf_find(port_to_idx[port]);
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
    uint32_t signal_count = next_signal;

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
