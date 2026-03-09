#include "codegen/codegen.h"
#include "json_parser/json_parser.h"
#include "jit_solver/jit_solver.h"
#include <iostream>
#include <fstream>

using namespace an24;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <json_file> <output_dir>\n";
        return 1;
    }

    std::string json_file = argv[1];
    std::string out_dir = argv[2];

    // Generate port registry from library/
    auto registry = load_type_registry("library");
    std::string port_registry_path = "src/jit_solver/components/port_registry.h";
    CodeGen::generate_port_registry(registry, port_registry_path);

    // Load JSON
    std::ifstream file(json_file);
    if (!file.is_open()) {
        std::cerr << "Failed to open: " << json_file << "\n";
        return 1;
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    auto ctx = parse_json(content);
    std::cout << "Parsed: " << ctx.devices.size() << " devices\n";

    // Build systems to get port_to_signal
    std::vector<Connection> conn;
    for (const auto& c : ctx.connections) {
        conn.push_back({c.from, c.to});
    }

    // Convert to pair format for build_systems_dev
    std::vector<std::pair<std::string, std::string>> conn_pairs;
    for (const auto& c : ctx.connections) {
        conn_pairs.push_back({c.from, c.to});
    }

    auto result = build_systems_dev(ctx.devices, conn_pairs);

    std::cout << "Signals: " << result.signal_count << "\n";
    std::cout << "Fixed: " << result.fixed_signals.size() << "\n";

    // Generate code
    CodeGen::write_files(
        out_dir,
        json_file,
        ctx.devices,
        conn,  // use Connection vector
        result.port_to_signal,
        result.signal_count
    );

    std::cout << "Generated files in: " << out_dir << "\n";

    return 0;
}
