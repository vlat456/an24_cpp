/// Standalone tool to regenerate port_registry.h and build_factory.cpp
/// from library/*.blueprint.
/// Built as CMake target 'update_port_registry'.
/// Usage: ./update_port_registry [library_dir]

#include "core/solvers/aot/codegen.h"
#include "io/json/component_registry_json_loader.h"
#include <iostream>

int main(int argc, char** argv) {
    std::string library_dir = "library";
    if (argc >= 2) library_dir = argv[1];

    std::string port_registry_path = "src/core/solvers/jit/components/port_registry.h";
    std::string build_factory_path = "src/core/solvers/jit/build_factory.cpp";

    auto registry = load_component_registry(library_dir);
    std::cout << "Loaded " << registry.types.size() << " types from " << library_dir << "/\n";

    CodeGen::generate_port_registry(registry, port_registry_path);
    std::cout << "Done! Port registry updated: " << port_registry_path << "\n";

    CodeGen::generate_build_factory(registry, build_factory_path);
    std::cout << "Done! Build factory updated: " << build_factory_path << "\n";

    return 0;
}
