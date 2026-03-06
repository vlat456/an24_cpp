#pragma once

#include "../json_parser/json_parser.h"

namespace an24 {

/// Code generator - produces C++ source files from device configuration
class CodeGen {
public:
    /// Generate C++ header file with Systems struct
    static std::string generate_header(
        const std::string& source_file,
        const std::vector<DeviceInstance>& devices,
        const std::vector<Connection>& connections,
        const std::unordered_map<std::string, uint32_t>& port_to_signal,
        uint32_t signal_count
    );

    /// Generate C++ source file with implementations
    static std::string generate_source(
        const std::string& header_name,
        const std::vector<DeviceInstance>& devices,
        const std::vector<Connection>& connections,
        const std::unordered_map<std::string, uint32_t>& port_to_signal,
        uint32_t signal_count
    );

    /// Write generated files to directory
    static void write_files(
        const std::string& out_dir,
        const std::string& source_file,
        const std::vector<DeviceInstance>& devices,
        const std::vector<Connection>& connections,
        const std::unordered_map<std::string, uint32_t>& port_to_signal,
        uint32_t signal_count
    );

    /// Generate port registry header from components/*.json
    static void generate_port_registry(const std::string& components_dir, const std::string& output_path);
};

} // namespace an24
