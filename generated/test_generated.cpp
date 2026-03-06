#include "generated_an24_composite_test.h"
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

    std::cout << "Signal count: " << SIGNAL_COUNT << "\n";
    std::cout << "Fixed signals: ";
    for (size_t i = 0; i < sizeof(FIXED_SIGNALS)/sizeof(FIXED_SIGNALS[0]); ++i) {
        std::cout << FIXED_SIGNALS[i] << " ";
    }
    std::cout << "\n";

    // Run simulation
    const float dt = 1.0f / 60.0f;
    for (int step = 0; step < 10; ++step) {
        state.clear_through();
        sys.solve_step(&state, step, dt);
        state.precompute_inv_conductance();

        // SOR update
        float omega = 1.5f;
        for (size_t i = 0; i < state.across.size(); ++i) {
            if (!state.signal_types[i].is_fixed && state.inv_conductance[i] > 0.0f) {
                state.across[i] += state.through[i] * state.inv_conductance[i] * omega;
            }
        }

        sys.post_step(&state, 1.0f / 60.0f);

        if (step < 3 || step == 9) {
            std::cout << "Step " << step << ": ";
            for (size_t i = 0; i < state.across.size(); ++i) {
                std::cout << state.across[i] << " ";
            }
            std::cout << "\n";
        }
    }

    return 0;
}
