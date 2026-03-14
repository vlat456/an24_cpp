/// Standalone tool to regenerate port_registry.h from library/*.blueprint.
/// Built as CMake target 'update_port_registry'.
/// Usage: ./update_port_registry [library_dir]
///
/// Reads all .blueprint files from the library directory, extracts component
/// port definitions, and writes the auto-generated port_registry.h.

#include "codegen/codegen.h"
#include "json_parser/json_parser.h"
#include <iostream>

int main(int argc, char** argv) {
    std::string library_dir = "library";
    if (argc >= 2) library_dir = argv[1];

    std::string port_registry_path = "src/jit_solver/components/port_registry.h";

    auto registry = load_type_registry(library_dir);
    std::cout << "Loaded " << registry.types.size() << " types from " << library_dir << "/\n";

    CodeGen::generate_port_registry(registry, port_registry_path);

    std::cout << "Done! Port registry updated: " << port_registry_path << "\n";
    return 0;
}
