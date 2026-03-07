#include "json_parser/json_parser.h"
#include "jit_solver/jit_solver.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <vector>

using namespace an24;

// Benchmark results
struct BenchmarkResult {
    std::string name;
    double setup_ms;
    double simulation_ms;
    double total_ms;
    uint64_t steps;
};

BenchmarkResult benchmark_jit(const std::string& json_file, uint64_t iterations) {
    BenchmarkResult result;
    result.name = "JIT (ComponentVariant + JitProvider)";
    result.steps = iterations;

    // Load JSON
    auto start = std::chrono::high_resolution_clock::now();

    std::ifstream file(json_file);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    auto ctx = parse_json(content);

    std::vector<std::pair<std::string, std::string>> conn_pairs;
    for (const auto& c : ctx.connections) {
        conn_pairs.push_back({c.from, c.to});
    }

    auto build_result = build_systems_dev(ctx.devices, conn_pairs);

    auto setup_done = std::chrono::high_resolution_clock::now();
    result.setup_ms = std::chrono::duration<double, std::milli>(setup_done - start).count();

    // Setup simulation state
    SimulationState state;
    state.across.resize(build_result.signal_count, 0.0f);
    state.through.resize(build_result.signal_count, 0.0f);
    state.conductance.resize(build_result.signal_count, 0.0f);
    state.inv_conductance.resize(build_result.signal_count, 0.0f);
    state.signal_types.resize(build_result.signal_count);
    state.convergence_buffer.resize(build_result.signal_count, 0.0f);

    // Run simulation
    auto sim_start = std::chrono::high_resolution_clock::now();

    for (uint64_t step = 0; step < iterations; ++step) {
        // Electrical/Logical: every step (60 Hz)
        for (auto* variant : build_result.domain_components.electrical) {
            std::visit([&](auto& comp) {
                if constexpr (requires { comp.solve_electrical(state, 0.0f); }) {
                    comp.solve_electrical(state, 0.016f);
                }
            }, *variant);
        }

        // Mechanical: every 3rd step (20 Hz)
        if ((step % 3) == 0) {
            for (auto* variant : build_result.domain_components.mechanical) {
                std::visit([&](auto& comp) {
                    if constexpr (requires { comp.solve_mechanical(state, 0.016f); }) {
                        comp.solve_mechanical(state, 0.016f * 3.0f);
                    }
                }, *variant);
            }
        }

        // Thermal: every 60th step (1 Hz)
        if (step == 0) {
            for (auto* variant : build_result.domain_components.thermal) {
                std::visit([&](auto& comp) {
                    if constexpr (requires { comp.solve_thermal(state, 0.016f); }) {
                        comp.solve_thermal(state, 0.016f * 60.0f);
                    }
                }, *variant);
            }
        }
    }

    auto sim_done = std::chrono::high_resolution_clock::now();
    result.simulation_ms = std::chrono::duration<double, std::milli>(sim_done - sim_start).count();
    result.total_ms = result.setup_ms + result.simulation_ms;

    return result;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <json_file> [iterations]\n";
        return 1;
    }

    std::string json_file = argv[1];
    uint64_t iterations = argc > 2 ? std::stoull(argv[2]) : 10000;

    std::cout << "=============================================================================\n";
    std::cout << "AN-24 Simulation Benchmark: JIT vs AOT\n";
    std::cout << "=============================================================================\n";
    std::cout << "JSON file: " << json_file << "\n";
    std::cout << "Iterations: " << iterations << " steps\n\n";

    // Warmup
    std::cout << "Warming up JIT...\n";
    benchmark_jit(json_file, 1000);

    // Actual benchmarks
    std::cout << "\nRunning benchmarks...\n\n";

    auto jit_result = benchmark_jit(json_file, iterations);

    // Print results
    std::cout << "=============================================================================\n";
    std::cout << "RESULTS:\n";
    std::cout << "=============================================================================\n\n";

    std::cout << jit_result.name << ":\n";
    std::cout << "  Setup:       " << jit_result.setup_ms << " ms\n";
    std::cout << "  Simulation: " << jit_result.simulation_ms << " ms\n";
    std::cout << "  Total:       " << jit_result.total_ms << " ms\n";
    std::cout << "  Time/step:  " << (jit_result.simulation_ms / jit_result.steps * 1000.0) << " µs\n";
    std::cout << "  Steps/sec:   " << (jit_result.steps / jit_result.simulation_ms * 1000.0) << " steps/s\n";
    std::cout << "  Target:      60000 steps/s (60 Hz)\n";
    std::cout << "  Efficiency:  " << ((jit_result.steps / jit_result.simulation_ms * 1000.0) / 60000.0 * 100.0) << "%\n";

    std::cout << "\nNOTE: To benchmark AOT, run:\n";
    std::cout << "  cd /tmp/aot_test\n";
    std::cout << "  ./aot_blueprint  # This will run simulation\n";
    std::cout << "  # For profiling: perf record ./aot_blueprint\n";

    return 0;
}
