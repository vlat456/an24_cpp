#pragma once

#include "../json_parser/json_parser.h"
#include <string>
#include <map>

namespace an24 {

/// Result of composite code generation
struct CompositeCodegenResult {
    std::string header;
    std::string source;
    std::string class_name;
};

/// Code generator - produces C++ source files from device configuration
class CodeGen {
public:
    /// Generate C++ header file with Systems struct
    static std::string generate_header(
        const std::string& source_file,
        const std::vector<DeviceInstance>& devices,
        const std::vector<Connection>& connections,
        const std::unordered_map<std::string, uint32_t>& port_to_signal,
        uint32_t signal_count,
        const std::string& class_name = "Systems"
    );

    /// Generate C++ source file with implementations
    static std::string generate_source(
        const std::string& header_name,
        const std::vector<DeviceInstance>& devices,
        const std::vector<Connection>& connections,
        const std::unordered_map<std::string, uint32_t>& port_to_signal,
        uint32_t signal_count,
        const std::string& class_name = "Systems"
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

    /// Generate port registry header from TypeRegistry
    static void generate_port_registry(const TypeRegistry& registry, const std::string& output_path);

    /// Generate Systems class for a composite blueprint.
    /// Expands sub-blueprint references into flat devices + connections,
    /// runs union-find signal allocation, then delegates to generate_header/source.
    /// Produces fully branchless ECS-like code identical to flat codegen.
    /// Throws on cycles or missing types.
    static CompositeCodegenResult generate_composite_systems(
        const TypeDefinition& td,
        const TypeRegistry& registry);

    /// Generate all composites in topological order (leaves first).
    /// Returns map: classname → {header, source}.
    static std::map<std::string, CompositeCodegenResult> generate_all_composites(
        const TypeRegistry& registry);
};

} // namespace an24
