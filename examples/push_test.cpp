#include "../src/jit_solver/push_solver.h"
#include "../src/jit_solver/components/push_components.h"
#include "../src/json_parser/json_parser.h"
#include <iostream>
#include <fstream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <circuit.json>" << std::endl;
        return 1;
    }

    // Read JSON file
    std::string filepath(argv[1]);
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filepath << std::endl;
        return 1;
    }

    std::string json_str((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    // Parse JSON
    auto ctx = an24::parse_json(json_str);

    // Build connections
    std::vector<std::pair<std::string, std::string>> connections;
    for (const auto& c : ctx.connections) {
        connections.push_back({c.from, c.to});
    }

    // Create PushSolver
    an24::PushSolver solver;

    // Build solver
    if (!solver.build(ctx.devices, connections)) {
        std::cerr << "Failed to build solver" << std::endl;
        return 1;
    }

    std::cout << "PushSolver built successfully!" << std::endl;
    std::cout << "Devices: " << ctx.devices.size() << std::endl;
    std::cout << "Connections: " << connections.size() << std::endl;

    // Run simulation
    an24::PushState state;
    for (int step = 0; step < 10; ++step) {
        solver.step(state, 0.016f);

        std::cout << "\n=== Step " << step << " ===" << std::endl;

        // Print voltages for all ports
        for (const auto& [port_ref, signal_idx] : state.signal_to_idx) {
            float voltage = state.signals[signal_idx].voltage;
            std::cout << "  " << port_ref << " = " << voltage << "V" << std::endl;
        }
    }

    return 0;
}
