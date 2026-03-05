#include "json_parser/json_parser.h"
#include "jit_solver/jit_solver.h"
#include "jit_solver/state.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <vector>

using namespace an24;

// Benchmark result structure
struct BenchResult {
    std::string name;
    double total_ms;
    double per_step_ns;
    uint32_t iterations;
    double steps_per_sec;
};

void print_bench_result(const BenchResult& r) {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  " << r.name << ": "
              << r.per_step_ns << " ns/step"
              << " (" << r.steps_per_sec << " steps/sec)"
              << " [" << r.total_ms << " ms total]\n";
}

int main(int argc, char** argv) {
    spdlog::set_level(spdlog::level::warn);  // Reduce logging noise

    // Default to vsu_test.json if no args
    std::string json_file = (argc > 1) ? argv[1] : "/Users/vladimir/an24_cpp/src/aircraft/vsu_test.json";

    std::cout << "=== AN-24 Benchmark ===\n";
    std::cout << "Config: " << json_file << "\n\n";

    // Load JSON
    std::ifstream file(json_file);
    if (!file.is_open()) {
        std::cerr << "Failed to open: " << json_file << "\n";
        return 1;
    }
    std::string json_content((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());

    auto ctx = parse_json(json_content);
    std::cout << "Parsed: " << ctx.devices.size() << " devices, "
              << ctx.connections.size() << " connections\n";

    // Build systems
    std::vector<std::pair<std::string, std::string>> conn;
    for (const auto& c : ctx.connections) {
        conn.push_back({c.from, c.to});
    }

    auto result = build_systems_dev(ctx.devices, conn);
    std::cout << "Signals: " << result.signal_count << "\n";
    std::cout << "Components: " << result.systems.component_count() << "\n\n";

    // Allocate state
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

    // Resize convergence buffer
    state.resize_buffers(result.signal_count);

    // Initialize fixed signals
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
            }
        }
    }

    // Benchmark parameters
    const uint32_t warmup_steps = 100;
    const uint32_t bench_steps = 10000;
    const float omega = 1.8f;
    const float dt = 0.016f;

    std::cout << "=== Warmup (" << warmup_steps << " steps) ===\n";
    for (uint32_t step = 0; step < warmup_steps; ++step) {
        state.clear_through();
        result.systems.solve_step(state, step);
        state.precompute_inv_conductance();
        state.solve_signals_balance(omega);
        result.systems.post_step(state, dt);
    }
    std::cout << "Warmup complete.\n\n";

    // Benchmark 1: Full simulation loop (like original)
    std::cout << "=== Benchmark 1: Full Simulation Loop ===\n";
    {
        auto start = std::chrono::high_resolution_clock::now();

        for (uint32_t step = 0; step < bench_steps; ++step) {
            state.clear_through();
            result.systems.solve_step(state, step);
            state.precompute_inv_conductance();
            state.solve_signals_balance(omega);
            result.systems.post_step(state, dt);
        }

        auto end = std::chrono::high_resolution_clock::now();
        double total_ms = std::chrono::duration<double, std::milli>(end - start).count();
        double per_step_ns = (total_ms * 1e6) / bench_steps;
        double steps_per_sec = bench_steps / (total_ms / 1000.0);

        BenchResult r = {"Full loop (clear+solve+balance+post)", total_ms, per_step_ns, bench_steps, steps_per_sec};
        print_bench_result(r);
    }

    // Benchmark 2: Just solve_step (no I/O)
    std::cout << "\n=== Benchmark 2: Just solve_step ===\n";
    {
        auto start = std::chrono::high_resolution_clock::now();

        for (uint32_t step = 0; step < bench_steps; ++step) {
            state.clear_through();
            result.systems.solve_step(state, step);
        }

        auto end = std::chrono::high_resolution_clock::now();
        double total_ms = std::chrono::duration<double, std::milli>(end - start).count();
        double per_step_ns = (total_ms * 1e6) / bench_steps;
        double steps_per_sec = bench_steps / (total_ms / 1000.0);

        BenchResult r = {"solve_step only", total_ms, per_step_ns, bench_steps, steps_per_sec};
        print_bench_result(r);
    }

    // Benchmark 3: SOR solver only
    std::cout << "\n=== Benchmark 3: SOR Solver Only ===\n";
    {
        auto start = std::chrono::high_resolution_clock::now();

        for (uint32_t step = 0; step < bench_steps; ++step) {
            state.precompute_inv_conductance();
            state.solve_signals_balance(omega);
        }

        auto end = std::chrono::high_resolution_clock::now();
        double total_ms = std::chrono::duration<double, std::milli>(end - start).count();
        double per_step_ns = (total_ms * 1e6) / bench_steps;
        double steps_per_sec = bench_steps / (total_ms / 1000.0);

        BenchResult r = {"SOR balance only", total_ms, per_step_ns, bench_steps, steps_per_sec};
        print_bench_result(r);
    }

    // Benchmark 4: Multiple SOR iterations (convergence)
    std::cout << "\n=== Benchmark 4: Full Convergence (20 iter) ===\n";
    {
        const uint32_t iter = 20;

        auto start = std::chrono::high_resolution_clock::now();

        for (uint32_t step = 0; step < bench_steps; ++step) {
            for (uint32_t i = 0; i < iter; ++i) {
                state.clear_through();
                result.systems.solve_step(state, step);
                state.precompute_inv_conductance();
                state.solve_signals_balance(omega);
            }
            result.systems.post_step(state, dt);
        }

        auto end = std::chrono::high_resolution_clock::now();
        double total_ms = std::chrono::duration<double, std::milli>(end - start).count();
        double per_step_ns = (total_ms * 1e6) / bench_steps;
        double steps_per_sec = bench_steps / (total_ms / 1000.0);

        BenchResult r = {"20 iter convergence", total_ms, per_step_ns, bench_steps, steps_per_sec};
        print_bench_result(r);
    }

    // Summary
    std::cout << "\n=== Summary ===\n";
    std::cout << "Signals: " << result.signal_count << "\n";
    std::cout << "Devices: " << result.systems.component_count() << "\n";
    std::cout << "Steps: " << bench_steps << "\n";
    std::cout << "Total time: see above\n";

    return 0;
}
