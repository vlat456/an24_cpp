#include "codegen.h"
#include "jit_solver/SOR_constants.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <unordered_set>


namespace an24 {

namespace {

std::string to_upper(const std::string& s) {
    std::string result = s;
    for (char& c : result) c = std::toupper(c);
    return result;
}

std::string sanitize_name(const std::string& s) {
    std::string result;
    for (char c : s) {
        if (c == '.' || c == '-' || c == ':') result += '_';
        else result += c;
    }
    return result;
}

// Infer C++ type from parameter value
std::string infer_type(const std::string& value) {
    if (value.empty()) return "float";

    try {
        size_t pos;
        long long ival = std::stoll(value, &pos);
        if (pos == value.size()) {
            if (ival > INT32_MAX || ival < INT32_MIN) return "int64_t";
            return "int32_t";
        }
    } catch (...) {}

    try {
        size_t pos;
        float fval = std::stof(value, &pos);
        if (pos == value.size()) {
            return "float";
        }
    } catch (...) {}

    if (value == "true" || value == "false") return "bool";

    return "std::string";
}

// Format value for C++ code
std::string format_value(const std::string& value, const std::string& type) {
    if (type == "float") {
        try {
            float f = std::stof(value);
            std::ostringstream oss;
            oss << f;
            return oss.str();
        } catch (...) {
            std::string v = value;
            if (v.find('.') == std::string::npos) {
                v += ".0";
            }
            return v;
        }
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

// Get domain from device
std::string get_device_domain(const DeviceInstance& dev) {
    // Build domain string from the parsed domains vector (populated from JSON "domains")
    if (!dev.domains.empty()) {
        std::string result;
        for (auto d : dev.domains) {
            if (!result.empty()) result += "|";
            switch (d) {
                case Domain::Electrical: result += "Electrical"; break;
                case Domain::Logical:    result += "Logical"; break;
                case Domain::Mechanical: result += "Mechanical"; break;
                case Domain::Hydraulic:  result += "Hydraulic"; break;
                case Domain::Thermal:    result += "Thermal"; break;
                default: break;
            }
        }
        if (!result.empty()) return result;
    }
    // Fallback to params["domain"] for backward compat
    auto it = dev.params.find("domain");
    if (it != dev.params.end()) {
        return it->second;
    }
    return "Electrical";  // default
}

// Check if device has specific domain
bool has_domain(const DeviceInstance& dev, const std::string& domain) {
    std::string dev_domain = get_device_domain(dev);
    return dev_domain.find(domain) != std::string::npos;
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

std::string CodeGen::generate_header(
    const std::string& source_file,
    const std::vector<DeviceInstance>& devices_unfiltered,
    const std::vector<Connection>& connections,
    const std::unordered_map<std::string, uint32_t>& port_to_signal,
    uint32_t signal_count,
    const std::string& class_name
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
    oss << "#include \"jit_solver/SOR_constants.h\"\n";
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
    oss << "namespace an24 {\n\n";

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

    // Domain scheduling accumulators (FPS-independent sub-rate domains)
    oss << "    // Domain scheduling accumulators — accumulate dt each step,\n";
    oss << "    // pass accumulated value when sub-rate domain fires, then reset.\n";
    oss << "    float acc_mechanical_ = 0.0f;\n";
    oss << "    float acc_hydraulic_  = 0.0f;\n";
    oss << "    float acc_thermal_    = 0.0f;\n\n";

    // Pre-allocated convergence buffer pointer (set at init)
    oss << "    float* convergence_buffer = nullptr;\n\n";

    // Constructor
    oss << "    " << class_name << "();\n\n";

    // Methods - all inline for optimization
    oss << "    /// Pre-load initialization\n";
    oss << "    void pre_load();\n\n";

    oss << "    /// Main solve step with jump table dispatch (ECS-like)\n";
    oss << "    void solve_step(void* state, uint32_t step, float dt);\n\n";

    // Generate CYCLE_LENGTH step methods
    for (int step = 0; step < DomainSchedule::CYCLE_LENGTH; ++step) {
        oss << "    AOT_INLINE void step_" << step << "(void* state, float dt);\n";
    }
    oss << "\n";

    // Post-step updates
    oss << "    /// Post-step updates (state tracking, etc.)\n";
    oss << "    void post_step(void* state, float dt);\n\n";

    oss << "    /// Convergence check (sparse sampling)\n";
    oss << "    AOT_INLINE bool check_convergence(void* state, float tolerance) const;\n\n";

    oss << "    uint32_t component_count() const { return " << devices.size() << "; }\n";

    oss << "};\n\n";

    oss << "} // namespace an24\n";

    return oss.str();
}

std::string CodeGen::generate_source(
    const std::string& header_name,
    const std::vector<DeviceInstance>& devices_unfiltered,
    const std::vector<Connection>& connections,
    const std::unordered_map<std::string, uint32_t>& port_to_signal,
    uint32_t signal_count,
    const std::string& class_name
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
    oss << "#include <cstring>  // memcpy\n\n";
    // Enable fast-math for generated code only (not spdlog)
    oss << "#ifdef __GNUC__\n";
    oss << "#pragma GCC optimize(\"fast-math,unroll-loops\")\n";
    oss << "#endif\n\n";
    oss << "namespace an24 {\n\n";

    // Explicit template instantiations for AotProvider
    // These tell the compiler to generate code for Component<AotProvider<Bindings...>>
    oss << "// Explicit template instantiations for AOT\n";
    for (const auto& dev : devices) {
        std::string aot_type = generate_aot_provider_type(dev, port_to_signal, signal_count);
        oss << "template class " << dev.classname << "<" << aot_type << ">;\n";
    }
    oss << "\n";

    // Constructor - only initialize component parameters (port indices are compile-time constants)
    oss << class_name << "::" << class_name << "()\n";
    oss << "{\n";
    oss << "    // Pre-allocate convergence buffer (zero-allocation in hot path)\n";
    oss << "    alignas(64) static float buf[SIGNAL_COUNT];\n";
    oss << "    convergence_buffer = buf;\n\n";

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
                    try {
                        entry.keys.push_back(std::stof(tbl.substr(pos, colon - pos)));
                        entry.values.push_back(std::stof(tbl.substr(colon + 1, end - colon - 1)));
                    } catch (...) { break; }
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

    // Jump table dispatch - COMPUTED GOTO (faster than switch, branchless)
    // Uses GCC/Clang labels-as-values extension
    oss << "void " << class_name << "::solve_step(void* state, uint32_t step, float dt) {\n";
    oss << "    // Accumulate dt for sub-rate domain scheduling\n";
    oss << "    acc_mechanical_ += dt;\n";
    oss << "    acc_hydraulic_  += dt;\n";
    oss << "    acc_thermal_    += dt;\n\n";
    oss << "    // Computed goto dispatch table (static const for one-time init)\n";
    oss << "    static const void* dispatch_table[" << DomainSchedule::CYCLE_LENGTH << "] = {\n";
    for (int i = 0; i < DomainSchedule::CYCLE_LENGTH; ++i) {
        oss << "        &&step_" << i << (i < DomainSchedule::CYCLE_LENGTH - 1 ? ",\n" : "\n");
    }
    oss << "    };\n\n";
    oss << "    // Direct jump - no bounds check needed (step % " << DomainSchedule::CYCLE_LENGTH << " is always 0-" << DomainSchedule::CYCLE_LENGTH - 1 << ")\n";
    oss << "    goto *dispatch_table[step % " << DomainSchedule::CYCLE_LENGTH << "];\n\n";
    for (int i = 0; i < DomainSchedule::CYCLE_LENGTH; ++i) {
        oss << "    step_" << i << ":\n";
        oss << "        step_" << i << "(state, dt);\n";
        oss << "        return;\n\n";
    }
    oss << "}\n\n";

    // Generate CYCLE_LENGTH step methods with domain scheduling
    // Group devices by domain and step
    std::map<std::pair<int, std::string>, std::vector<std::string>> step_devices;

    for (const auto& dev : devices) {
        std::string domain = get_device_domain(dev);

        // Electrical: every step (0, 1, 2, ...)
        if (domain.find("Electrical") != std::string::npos) {
            for (int step = 0; step < DomainSchedule::CYCLE_LENGTH; ++step) {
                step_devices[{step, "electrical"}].push_back(dev.name);
            }
        }

        // Mechanical: step (0, 3, 6, ...)
        if (domain.find("Mechanical") != std::string::npos) {
            for (int step = 0; step < DomainSchedule::CYCLE_LENGTH; step += DomainSchedule::MECHANICAL_PERIOD) {
                step_devices[{step, "mechanical"}].push_back(dev.name);
            }
        }

        // Hydraulic: every 12th step (0, 12, 24, ...)
        if (domain.find("Hydraulic") != std::string::npos) {
            for (int step = 0; step < DomainSchedule::CYCLE_LENGTH; step += DomainSchedule::HYDRAULIC_PERIOD) {
                step_devices[{step, "hydraulic"}].push_back(dev.name);
            }
        }

        // Thermal: every THERMAL_PERIOD-th step (only step 0 when period == cycle length)
        if (domain.find("Thermal") != std::string::npos) {
            for (int step = 0; step < DomainSchedule::CYCLE_LENGTH; step += DomainSchedule::THERMAL_PERIOD) {
                step_devices[{step, "thermal"}].push_back(dev.name);
            }
        }

        // Logical: every step (0, 1, 2, ...) - 60Hz boolean logic
        if (domain.find("Logical") != std::string::npos) {
            for (int step = 0; step < DomainSchedule::CYCLE_LENGTH; ++step) {
                step_devices[{step, "logical"}].push_back(dev.name);
            }
        }
    }

    // Generate each step method
    for (int step = 0; step < DomainSchedule::CYCLE_LENGTH; ++step) {
        oss << "AOT_INLINE void " << class_name << "::step_" << step << "(void* state, float dt) {\n";
        oss << "    auto* st = static_cast<SimulationState*>(state);\n";
        oss << "    st->clear_through();\n";

        // Electrical (every step) - DATA-ORIENTED: direct index access
        auto elec_it = step_devices.find({step, "electrical"});
        if (elec_it != step_devices.end()) {
            for (const auto& dev_name : elec_it->second) {
                // Find device
                auto dev_it = std::find_if(devices.begin(), devices.end(),
                    [&dev_name](const DeviceInstance& d) { return d.name == dev_name; });
                if (dev_it == devices.end()) continue;

                // Call component method (compiler will inline with -O3 -ffast-math)
                // Bus/RefNode are no-ops, others have solve_electrical()
                if (dev_it->classname == "Bus" || dev_it->classname == "RefNode" || dev_it->classname == "Voltmeter") {
                    oss << "    // " << sanitize_name(dev_name) << " (no-op)\n";
                } else {
                    oss << "    " << sanitize_name(dev_name) << ".solve_electrical(*st, dt);\n";
                }
            }
        }

        // Mechanical (every MECHANICAL_PERIOD-th)
        if (step % DomainSchedule::MECHANICAL_PERIOD == 0) {
            auto mech_it = step_devices.find({step, "mechanical"});
            if (mech_it != step_devices.end()) {
                for (const auto& dev_name : mech_it->second) {
                    oss << "    " << sanitize_name(dev_name) << ".solve_mechanical(*st, acc_mechanical_);\n";
                }
                oss << "    acc_mechanical_ = 0.0f;\n";
            }
        }

        // Hydraulic (every HYDRAULIC_PERIOD-th)
        if (step % DomainSchedule::HYDRAULIC_PERIOD == 0) {
            auto hyd_it = step_devices.find({step, "hydraulic"});
            if (hyd_it != step_devices.end()) {
                for (const auto& dev_name : hyd_it->second) {
                    oss << "    " << sanitize_name(dev_name) << ".solve_hydraulic(*st, acc_hydraulic_);\n";
                }
                oss << "    acc_hydraulic_ = 0.0f;\n";
            }
        }

        // Thermal (every THERMAL_PERIOD-th)
        if (step % DomainSchedule::THERMAL_PERIOD == 0) {
            auto therm_it = step_devices.find({step, "thermal"});
            if (therm_it != step_devices.end()) {
                for (const auto& dev_name : therm_it->second) {
                    oss << "    " << sanitize_name(dev_name) << ".solve_thermal(*st, acc_thermal_);\n";
                }
                oss << "    acc_thermal_ = 0.0f;\n";
            }
        }

        // SOR solver - single iteration per step (real-time approximation)
        // precompute sets inv_g=0 for fixed signals, so SOR loop is branchless
        oss << "    st->precompute_inv_conductance();\n";
        oss << "    solve_sor_iteration(st->across.data(), st->through.data(), st->inv_conductance.data(), SIGNAL_COUNT, SOR::OMEGA);\n";

        // Post-step: update device state after SOR convergence
        // Must run before logical so logical components see updated state
        // (e.g., HoldButton.state is set in post_step, read by AND gate)
        {
            static const std::unordered_set<std::string> has_post_step = {
                "Switch", "Relay", "HoldButton", "GS24", "LerpNode", "DMR400", "RU19A",
                "PID", "PD", "PI", "P"
            };
            for (const auto& dev : devices) {
                if (has_post_step.count(dev.classname)) {
                    oss << "    " << sanitize_name(dev.name) << ".post_step(*st, dt);\n";
                }
            }
        }

        // Logical: AFTER SOR + post_step so logical gates read converged values
        // and their outputs are final (SOR does not overwrite them)
        auto log_it = step_devices.find({step, "logical"});
        if (log_it != step_devices.end()) {
            for (const auto& dev_name : log_it->second) {
                auto dev_it = std::find_if(devices.begin(), devices.end(),
                    [&dev_name](const DeviceInstance& d) { return d.name == dev_name; });
                if (dev_it == devices.end()) continue;
                oss << "    " << sanitize_name(dev_name) << ".solve_logical(*st, dt);\n";
            }
        }

        oss << "}\n\n";
    }

    // Post-step is now integrated into each step_N() method
    // (runs after SOR, before logical — correct execution order)
    // This method is kept empty for backward compatibility with test harnesses
    oss << "void " << class_name << "::post_step(void* state, float dt) {\n";
    oss << "    // No-op: post_step is now inlined into step_N() functions\n";
    oss << "    // for correct execution order (electrical -> SOR -> post_step -> logical)\n";
    oss << "    (void)state; (void)dt;\n";
    oss << "}\n\n";

    // Convergence check - optimized with sparse sampling
    oss << "AOT_INLINE bool " << class_name << "::check_convergence(void* state, float tolerance) const {\n";
    oss << "    auto* st = static_cast<SimulationState*>(state);\n";
    oss << "    const float* __restrict across = st->across.data();\n";
    oss << "    const float* __restrict buf = convergence_buffer;\n";
    oss << "    const uint32_t count = st->dynamic_signals_count;\n";
    oss << "\n";
    oss << "    // Sparse check: every 4th signal - cache friendly\n";
    oss << "    for (uint32_t i = 0; i < count; i += 4) {\n";
    oss << "        float delta = std::abs(across[i] - buf[i]);\n";
    oss << "        if (AOT_UNLIKELY(delta > tolerance)) return false;\n";
    oss << "    }\n";
    oss << "    return true;\n";
    oss << "}\n\n";

    oss << "} // namespace an24\n";

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
    std::cerr << "[codegen]   - Jump table dispatch (" << DomainSchedule::CYCLE_LENGTH << " cases)\n";
    std::cerr << "[codegen]   - Domain scheduling (" << DomainSchedule::CYCLE_LENGTH << " step methods)\n";
    std::cerr << "[codegen]   - __restrict pointers (no aliasing)\n";
    std::cerr << "[codegen]   - AOT_INLINE + AOT_LIKELY/AOT_UNLIKELY\n";
    std::cerr << "[codegen]   - Sparse convergence check\n";
}

void CodeGen::generate_port_registry(const TypeRegistry& registry, const std::string& output_path) {
    std::cerr << "[codegen] Generating port registry from TypeRegistry (" << registry.types.size() << " types)\n";

    struct ComponentPorts {
        std::string classname;
        std::vector<std::string> ports;
    };

    std::vector<ComponentPorts> all_components;

    for (const auto& [name, def] : registry.types) {
        if (!def.cpp_class) continue;

        ComponentPorts comp;
        comp.classname = def.classname;
        for (const auto& [port_name, _] : def.ports) {
            comp.ports.push_back(port_name);
        }
        std::sort(comp.ports.begin(), comp.ports.end());
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
            all_port_names.insert(port);
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
    oss << "#include <optional>\n";
    oss << "#include <vector>\n";
    oss << "#include <variant>\n";
    oss << "\n";
    // Forward declare PortNames for Provider pattern
    oss << "namespace an24 {\n";
    oss << "enum class PortNames : uint32_t;\n";
    oss << "} // namespace an24\n";
    oss << "\n";
    // Include Provider pattern and component definitions
    // These are relative to port_registry.h location (src/jit_solver/components/)
    oss << "#include \"provider.h\"\n";
    oss << "#include \"all.h\"\n";
    oss << "\n";
    oss << "namespace an24 {\n";
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
        oss << "    " << all_components[i].classname;
        if (i < all_components.size() - 1) oss << ",\n";
        else oss << "\n";
    }
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
            oss << "    \"" << comp.ports[i] << "\"";
            if (i < comp.ports.size() - 1) oss << ",\n";
            else oss << "\n";
        }
        oss << "};\n";
    }
    oss << "\n";

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
            oss << "\"" << comp.ports[i] << "\"";
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

    // Generate visitor helper for calling solve methods on variant
    oss << "// Visitor helper for calling solve_electrical on component variant\n";
    oss << "template <typename... Visitors>\n";
    oss << "struct overloaded : Visitors... {\n";
    oss << "    using Visitors::operator()...;\n";
    oss << "};\n\n";

    oss << "// Helper visitor to call solve_electrical on any component\n";
    oss << "inline auto solve_electrical_visitor = [](auto& component, SimulationState& st, float dt) {\n";
    oss << "    component.solve_electrical(st, dt);\n";
    oss << "};\n\n";

    oss << "// Helper visitor to call post_step on any component (optional)\n";
    oss << "inline auto post_step_visitor = [](auto& component, SimulationState& st, float dt) {\n";
    oss << "    if constexpr (requires { component.post_step(st, dt); }) {\n";
    oss << "        component.post_step(st, dt);\n";
    oss << "    }\n";
    oss << "};\n\n";

    oss << "} // namespace an24\n";

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

    // 4. Delegate to existing generate_header/generate_source
    std::string class_name = sanitize_name(td.classname) + "_Systems";
    std::string source_file = td.classname + ".blueprint";
    std::string header_name = "generated_" + sanitize_name(td.classname) + ".h";

    CompositeCodegenResult result;
    result.class_name = class_name;
    result.header = generate_header(source_file, expanded.devices, expanded.connections,
                                    port_to_signal, signal_count, class_name);
    result.source = generate_source(header_name, expanded.devices, expanded.connections,
                                    port_to_signal, signal_count, class_name);
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

} // namespace an24
