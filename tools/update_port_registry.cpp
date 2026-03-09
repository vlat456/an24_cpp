#include "codegen/codegen.h"
#include <iostream>

int main() {
    std::string components_dir = "/Users/vladimir/an24_cpp/library";
    std::string port_registry_path = "src/jit_solver/components/port_registry.h";

    std::cout << "Generating port registry from " << components_dir << "...\n";

    an24::CodeGen::generate_port_registry(components_dir, port_registry_path);

    std::cout << "Done! Port registry updated.\n";
    return 0;
}
