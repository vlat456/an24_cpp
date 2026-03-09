#include "codegen/codegen.h"
#include "json_parser/json_parser.h"
#include <iostream>

int main() {
    std::string port_registry_path = "src/jit_solver/components/port_registry.h";

    auto registry = an24::load_type_registry("library");
    std::cout << "Loaded " << registry.types.size() << " types from library/\n";

    an24::CodeGen::generate_port_registry(registry, port_registry_path);

    std::cout << "Done! Port registry updated.\n";
    return 0;
}
