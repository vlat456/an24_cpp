#include "generated_vsu_dmr_test.h"
#include "jit_solver/state.h"
#include <iostream>

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
    state.resize_buffers(SIGNAL_COUNT);

    std::cout << "=== AOT Compiled VSU DMR Test ===\n";
    std::cout << "Signal count: " << SIGNAL_COUNT << "\n";
    std::cout << "Device count: " << DEVICE_COUNT << "\n";

    const float omega = 1.8f;
    const float dt = 0.016f;

    // Run 5000 steps - AOT uses 1 iteration (smart convergence)
    for (int step = 0; step < 5000; ++step) {
        state.clear_through();
        sys.solve_step(&state, step);
        state.precompute_inv_conductance();

        for (size_t i = 0; i < state.across.size(); ++i) {
            if (i < state.dynamic_signals_count && state.inv_conductance[i] > 0.0f) {
                state.across[i] += state.through[i] * state.inv_conductance[i] * omega;
            }
        }

        sys.post_step(&state, dt);

        if (step % 500 == 0 || step == 4999) {
            std::cout << "Step " << step << ": bus=" << state.across[1] << "V"
                      << ", rpm=" << state.across[4] << "%"
                      << ", k_mod=" << state.across[5]
                      << "\n";
        }
    }

    std::cout << "\nFinal signals:\n";
    for (size_t i = 0; i < state.across.size(); ++i) {
        std::cout << "  signal[" << i << "]: " << state.across[i] << "\n";
    }

    return 0;
}
