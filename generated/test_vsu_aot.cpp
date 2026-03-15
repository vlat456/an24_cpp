#include "generated_vsu_test.h"
#include "jit_solver/state.h"
#include <iostream>

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

    std::cout << "=== AOT Compiled VSU Test ===\n";
    std::cout << "Signal count: " << SIGNAL_COUNT << "\n";

    // Run simulation (5000 cycles like json_test)
    // step_X is now self-contained: clear_through + solve + precompute + SOR
    const float dt = 0.016f;

    for (int step = 0; step < 5000; ++step) {
        sys.solve_step(&state, step, dt);

        if (step % 500 == 0 || step == 4999) {
            std::cout << "Step " << step
                      << ": bus=" << state.across[SIG_VSU_1_V_BUS] << "V"
                      << ", rpm=" << state.across[SIG_VSU_1_RPM_OUT] << "%"
                      << ", k_mod=" << state.across[SIG_RUG_VSU_K_MOD]
                      << ", brightness=" << state.across[SIG_LIGHT_1_BRIGHTNESS]
                      << "\n";
        }

        sys.post_step(&state, dt);
    }

    std::cout << "\nFinal signals:\n";
    for (size_t i = 0; i < state.across.size(); ++i) {
        std::cout << "  signal[" << i << "]: " << state.across[i] << "\n";
    }

    return 0;
}
