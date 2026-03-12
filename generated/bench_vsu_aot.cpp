#include "generated_vsu_test.h"
#include "jit_solver/state.h"
#include <iostream>
#include <chrono>
#include <iomanip>

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

    state.resize_buffers(SIGNAL_COUNT);

    std::cout << "=== C++ AOT Benchmark (Single-iteration, like JIT) ===\n";
    std::cout << "Signals: " << SIGNAL_COUNT << "\n";
    std::cout << "Devices: " << DEVICE_COUNT << "\n\n";

    const float dt = 0.016f;
    const uint32_t bench_steps = 10'000'000;

    // Warmup (100k steps)
    for (uint32_t step = 0; step < 100'000; ++step) {
        sys.solve_step(&state, step, dt);
        sys.post_step(&state, dt);
    }

    std::cout << "=== Benchmark: " << bench_steps << " steps, 1 iter/step (like JIT) ===\n";

    auto start = std::chrono::high_resolution_clock::now();

    for (uint32_t step = 0; step < bench_steps; ++step) {
        sys.solve_step(&state, step, dt);
        sys.post_step(&state, dt);
    }

    auto end = std::chrono::high_resolution_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(end - start).count();
    double steps_per_sec = bench_steps / (total_ms / 1000.0);
    double per_step_ns = (total_ms * 1e6) / bench_steps;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Total time:      " << total_ms << " ms\n";
    std::cout << "Per step:        " << per_step_ns << " ns\n";
    std::cout << "Steps/sec:       " << steps_per_sec / 1e6 << "M steps/s\n";

    std::cout << "\nResults:\n";
    std::cout << "  bus=" << state.across[SIG_VSU_1_V_BUS] << "V\n";
    std::cout << "  rpm=" << state.across[SIG_VSU_1_RPM_OUT] << "%\n";
    std::cout << "  k_mod=" << state.across[SIG_RUG_VSU_K_MOD] << "\n";
    std::cout << "  brightness=" << state.across[SIG_LIGHT_1_BRIGHTNESS] << "\n";

    return 0;
}
