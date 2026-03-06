#include "generated_vsu_dmr_test.h"
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

    std::cout << "=== C++ AOT Benchmark: Full VSU with DMR ===\n";
    std::cout << "Signals: " << SIGNAL_COUNT << "\n";
    std::cout << "Devices: " << DEVICE_COUNT << "\n\n";

    const float omega = 1.8f;
    const float inv_omega = 1.0f / omega;  // Precompute for fast math
    const float dt = 0.016f;
    const float tolerance = 0.001f;

    const uint32_t bench_steps = 10000;

    std::cout << "=== Benchmark: " << bench_steps << " steps, fast math + smart convergence ===\n";

    uint64_t total_iters = 0;
    auto start = std::chrono::high_resolution_clock::now();

    for (uint32_t step = 0; step < bench_steps; ++step) {
        state.save_convergence_state();

        uint32_t iters = 0;
        for (iters = 0; iters < 20; ++iters) {
            state.clear_through();
            sys.solve_step(&state, step, dt);
            state.precompute_inv_conductance();

            // FAST MATH: use inv_omega instead of omega (no division in hot path!)
            for (size_t i = 0; i < state.across.size(); ++i) {
                if (!state.signal_types[i].is_fixed && state.inv_conductance[i] > 0.0f) {
                    state.across[i] += state.through[i] * state.inv_conductance[i] * inv_omega;
                }
            }

            if (state.has_converged(tolerance)) {
                break;
            }
        }
        total_iters += iters + 1;

        sys.post_step(&state, dt);

        if (step == 0 || step == 4999) {
            std::cout << "Step " << step << ": iters=" << iters+1;
            // Print some signals
            std::cout << "\n";
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(end - start).count();
    double per_step_ns = (total_ms * 1e6) / bench_steps;
    double avg_iters = (double)total_iters / bench_steps;
    double steps_per_sec = bench_steps / (total_ms / 1000.0);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n=== Results ===\n";
    std::cout << "Total time:     " << total_ms << " ms\n";
    std::cout << "Per step:       " << per_step_ns << " ns (" << steps_per_sec << " steps/sec)\n";
    std::cout << "Avg iters/step: " << avg_iters << "\n";

    return 0;
}
