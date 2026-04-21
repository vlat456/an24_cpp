/// Standalone tool to regenerate port_registry.h from library/*.blueprint.
/// Built as CMake target 'update_port_registry'.
/// Usage: ./update_port_registry [library_dir]
///
/// Reads all .blueprint files from the library directory, extracts component
/// port definitions, and writes the auto-generated port_registry.h.

#include "core/solvers/aot/codegen.h"
#include "io/json/component_registry_json_loader.h"
#include <iostream>

int main(int argc, char** argv) {
    std::string library_dir = "library";
    if (argc >= 2) library_dir = argv[1];

    std::string port_registry_path = "src/core/solvers/jit/components/port_registry.h";

    auto registry = load_component_registry(library_dir);
    std::cout << "Loaded " << registry.types.size() << " types from " << library_dir << "/\n";

    CodeGen::generate_port_registry(registry, port_registry_path);

    std::cout << "Done! Port registry updated: " << port_registry_path << "\n";
    return 0;
}
