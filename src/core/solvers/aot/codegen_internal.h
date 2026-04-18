#pragma once

#include "codegen.h"
#include "codegen_utils.h"
#include "../common/signal_key.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace codegen_detail {

inline std::string sanitize_name(const std::string& s) {
    return sanitize_codegen_name(s);
}

inline std::string to_upper(const std::string& s) {
    std::string result = s;
    for (char& c : result) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return result;
}

inline std::string generate_aot_provider_type(
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
        std::string port_key = signal_key::make_node_port_key(dev.name, port_name);
        uint32_t sig = port_to_signal.count(port_key) ? port_to_signal.at(port_key) : signal_count;
        if (!first) {
            oss << ", ";
        }
        oss << "Binding<PortNames::" << port_name << ", " << sig << ">";
        first = false;
    }
    oss << ">";
    return oss.str();
}

/// Filter out visual-only devices, returning only simulation-active ones.
inline std::vector<DeviceInstance> filter_simulation_devices(
    const std::vector<DeviceInstance>& devices_unfiltered
) {
    std::vector<DeviceInstance> devices;
    devices.reserve(devices_unfiltered.size());
    for (const auto& d : devices_unfiltered) {
        if (!d.visual_only) {
            devices.push_back(d);
        }
    }
    return devices;
}

/// Emit the ElectricalDebugEntry struct definition.
inline void emit_electrical_debug_entry_struct(std::ostringstream& oss) {
    oss << "struct ElectricalDebugEntry {\n";
    oss << "    uint32_t element_id;\n";
    oss << "    uint32_t island_index;\n";
    oss << "    uint32_t element_index;\n";
    oss << "    const char* device_name;\n";
    oss << "    const char* classname;\n";
    oss << "    const char* role;\n";
    oss << "    uint32_t node_a;\n";
    oss << "    uint32_t node_b;\n";
    oss << "};\n\n";
}

/// Emit execute() + commit() calls for all devices in a step body.
inline void emit_device_execute_commit(
    std::ostringstream& oss,
    const std::vector<DeviceInstance>& devices
) {
    // Match JIT scheduler semantics: source bucket before consumer bucket.
    // Order within each bucket remains declaration order.
    for (const auto& dev : devices) {
        if (!dev.scheduler_source) {
            continue;
        }
        oss << "    " << sanitize_name(dev.name) << ".execute(*st, dt);\n";
    }
    for (const auto& dev : devices) {
        if (dev.scheduler_source) {
            continue;
        }
        oss << "    " << sanitize_name(dev.name) << ".execute(*st, dt);\n";
    }

    for (const auto& dev : devices) {
        if (!dev.scheduler_source) {
            continue;
        }
        oss << "    " << sanitize_name(dev.name) << ".commit(*st, dt);\n";
    }
    for (const auto& dev : devices) {
        if (dev.scheduler_source) {
            continue;
        }
        oss << "    " << sanitize_name(dev.name) << ".commit(*st, dt);\n";
    }
}

/// Emit a debug-map lookup loop that finds an entry by a field match and logs it.
/// Used both in step diagnostics (match by element_id) and dump_island_debug
/// (match by island_index).
inline void emit_debug_map_lookup(
    std::ostringstream& oss,
    const std::string& match_field,
    const std::string& match_expr,
    const std::string& log_prefix,
    const std::string& log_fields,
    bool break_on_match
) {
    oss << "    for (uint32_t i = 0; i < ELECTRICAL_DEBUG_COUNT; ++i) {\n";
    oss << "        const auto& e = ELECTRICAL_DEBUG_MAP[i];\n";
    oss << "        if (e." << match_field << " " << match_expr << ") {\n";
    oss << "            spdlog::warn(\"" << log_prefix << "\",\n";
    oss << "                " << log_fields << ");\n";
    if (break_on_match) {
        oss << "            break;\n";
    }
    oss << "        }\n";
    oss << "    }\n";
}

} // namespace codegen_detail
