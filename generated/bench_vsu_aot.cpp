#include "generated_vsu_test.h"
#include "jit_solver/state.h"
#include <iostream>
#include <chrono>
#include <iomanip>

using namespace an24;

int main() {
    Systems sys;
    sys.pre_load();

    SimulationState state;
    for (uint32_t i = 0; i < SIGNAL_COUNT; ++i) {
        bool is_fixed = false;
        for (size_t j = 0; j < sizeof(FIXED_SIGNALS)/sizeof(FIXED_SIGNALS[0]); ++j) {
            if (i == FIXED_SIGNALS[j]) {
                is_fixed = true;
                break;
            }
        }
        state.allocate_signal(0.0f, {Domain::Electrical, is_fixed});
    }

    // Initialize convergence buffer (zero-allocation in hot path!)
    state.resize_buffers(SIGNAL_COUNT);

    std::cout << "=== C++ AOT Benchmark (Smart Convergence) ===\n";
    std::cout << "Signals: " << SIGNAL_COUNT << "\n";
    std::cout << "Devices: " << DEVICE_COUNT << "\n\n";

    const float omega = 1.8f;
    const float dt = 0.016f;
    const float tolerance = 0.001f;  // 1mV convergence

    // Benchmark with smart convergence
    const uint32_t bench_steps = 10000;
    const uint32_t max_iter = 20;

    std::cout << "=== Benchmark: " << bench_steps << " steps, smart convergence ===\n";

    // First, run to steady state
    uint64_t total_iters = 0;
    auto start = std::chrono::high_resolution_clock::now();

    for (uint32_t step = 0; step < bench_steps; ++step) {
        // Save state before iteration
        state.save_convergence_state();

        uint32_t iters = 0;
        for (iters = 0; iters < max_iter; ++iters) {
            state.clear_through();
            sys.solve_step(&state, step);
            state.precompute_inv_conductance();

            // Direct solver loop
            for (size_t i = 0; i < state.across.size(); ++i) {
                if (!state.signal_types[i].is_fixed && state.inv_conductance[i] > 0.0f) {
                    state.across[i] += state.through[i] * state.inv_conductance[i] * omega;
                }
            }

            // Check convergence
            if (state.has_converged(tolerance)) {
                break;
            }
        }
        total_iters += iters + 1;

        sys.post_step(&state, dt);
    }

    auto end = std::chrono::high_resolution_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(end - start).count();
    double per_step_ns = (total_ms * 1e6) / bench_steps;
    double per_iter_ns = (total_ms * 1e6) / total_iters;
    double avg_iters = (double)total_iters / bench_steps;
    double steps_per_sec = bench_steps / (total_ms / 1000.0);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Total time:      " << total_ms << " ms\n";
    std::cout << "Per step:        " << per_step_ns << " ns (" << steps_per_sec << " steps/sec)\n";
    std::cout << "Per iter:        " << per_iter_ns << " ns\n";
    std::cout << "Avg iters/step:  " << avg_iters << " (max " << max_iter << ")\n";

    std::cout << "\nResults:\n";
    std::cout << "  bus=" << state.across[SIG_VSU_1_V_OUT] << "V\n";
    std::cout << "  rpm=" << state.across[SIG_VSU_1_RPM_OUT] << "%\n";
    std::cout << "  k_mod=" << state.across[SIG_RUG_VSU_K_MOD] << "\n";
    std::cout << "  brightness=" << state.across[SIG_LIGHT_1_BRIGHTNESS] << "\n";

    return 0;
}
