#include "json_parser/json_parser.h"
#include "jit_solver/jit_solver.h"
#include "jit_solver/state.h"
#include "jit_solver/SOR_constants.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <fstream>
#include <iomanip>


int main(int argc, char** argv) {
    spdlog::set_level(spdlog::level::debug);

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <json_file>\n";
        return 1;
    }

    // Load JSON file
    std::ifstream file(argv[1]);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << argv[1] << "\n";
        return 1;
    }
    std::string json_content((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());

    std::cout << "=== Loading JSON ===\n";
    auto ctx = parse_json(json_content);

    std::cout << "Parsed: " << ctx.devices.size() << " devices, "
              << ctx.connections.size() << " connections\n";

    // Print devices
    std::cout << "\n=== Devices ===\n";
    for (const auto& dev : ctx.devices) {
        std::cout << "  " << dev.name << ": " << dev.classname;
        if (!dev.params.empty()) {
            std::cout << " (";
            bool first = true;
            for (const auto& [k, v] : dev.params) {
                if (!first) std::cout << ", ";
                std::cout << k << "=" << v;
                first = false;
            }
            std::cout << ")";
        }
        std::cout << "\n";
    }

    // Build systems - pass devices and connections directly
    std::vector<std::pair<std::string, std::string>> conn;
    for (const auto& c : ctx.connections) {
        conn.push_back({c.from, c.to});
    }

    std::cout << "\n=== Building Systems ===\n";
    auto result = build_systems_dev(ctx.devices, conn);

    std::cout << "Signals: " << result.signal_count << "\n";
    std::cout << "Components: " << result.systems.component_count() << "\n";

    // Print port mapping
    std::cout << "\n=== Port Mapping ===\n";
    for (const auto& [port, sig] : result.port_to_signal) {
        std::cout << "  " << port << " -> signal " << sig << "\n";
    }

    // Allocate state with fixed signals
    SimulationState state;
    for (size_t i = 0; i < result.signal_count; ++i) {
        bool is_fixed = false;
        for (uint32_t fixed : result.fixed_signals) {
            if (static_cast<uint32_t>(i) == fixed) {
                is_fixed = true;
                break;
            }
        }
        (void)state.allocate_signal(0.0f, {Domain::Electrical, is_fixed});
    }

    // Initialize voltages from RefNodes as starting point
    std::cout << "\n=== Fixed Signals ===\n";
    for (const auto& dev : ctx.devices) {
        if (dev.classname == "RefNode") {
            float value = 0.0f;
            auto it_val = dev.params.find("value");
            if (it_val != dev.params.end()) {
                value = std::stof(it_val->second);
            }
            std::string port = dev.name + ".v";
            auto it_sig = result.port_to_signal.find(port);
            if (it_sig != result.port_to_signal.end()) {
                state.across[it_sig->second] = value;
                std::cout << "  " << port << " = " << value << "V (signal " << it_sig->second << ")\n";
            }
        }
    }

    // Look up signal indices by port name
    auto get_sig = [&](const std::string& port) -> size_t {
        auto it = result.port_to_signal.find(port);
        return (it != result.port_to_signal.end()) ? it->second : 0;
    };

    // Find common signal indices
    size_t sig_bus = get_sig("main_bus.v");
    if (sig_bus == 0) sig_bus = get_sig("bat_main_1.v_out");
    size_t sig_rpm = get_sig("vsu_1.rpm_out");
    size_t sig_k_mod = get_sig("rug_vsu.k_mod");
    size_t sig_brightness = get_sig("light_1.brightness");

    // Run simulation with SOR
    std::cout << "\n=== Running Simulation (5000 cycles) ===\n";
    const float omega = SOR::OMEGA;
    const float dt = 1.0f / 60.0f;
    for (int step = 0; step < 5000; ++step) {
        state.clear_through();
        result.systems.solve_step(state, step, dt);

        // Show values every 500 cycles
        if (step % 500 == 0 || step == 4999) {
            std::cout << "Step " << step
                      << ": bus=" << state.across[sig_bus] << "V"
                      << ", rpm=" << state.across[sig_rpm] << "%"
                      << ", k_mod=" << state.across[sig_k_mod]
                      << ", brightness=" << state.across[sig_brightness] << "\n";
        }

        state.precompute_inv_conductance();

        for (size_t i = 0; i < state.across.size(); ++i) {
            if (!state.signal_types[i].is_fixed && state.inv_conductance[i] > 0.0f) {
                state.across[i] += state.through[i] * state.inv_conductance[i] * omega;
            }
        }

        // Apply post-step constraints (relay contacts, etc.) after SOR update
        result.systems.post_step(state, dt);
    }

    // Print results
    std::cout << "\n=== Results ===\n";
    std::cout << std::fixed << std::setprecision(2);
    for (size_t i = 0; i < state.across.size(); ++i) {
        std::cout << "  signal[" << i << "]: " << state.across[i] << "V\n";
    }

    return 0;
}
