#include <gtest/gtest.h>
#include "jit_solver/jit_solver.h"

TEST(JitSolverTest, BuildSystemsBasic) {
    an24::DeviceInstance battery;
    battery.name = "bat_main_1";
    battery.internal = "Battery";
    battery.params["v_nominal"] = "28.0";
    battery.ports["v_in"] = "input";
    battery.ports["v_out"] = "output";

    std::vector<an24::DeviceInstance> devices = {battery};
    std::vector<std::pair<std::string, std::string>> connections;

    auto result = an24::build_systems_dev(devices, connections);

    EXPECT_GT(result.signal_count, 0);
}

TEST(SimulationStateTest, AllocateSignal) {
    an24::SimulationState state;

    auto idx = state.allocate_signal(12.0f, {an24::Domain::Electrical, false});

    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(state.across.size(), 1u);
    EXPECT_FLOAT_EQ(state.across[idx], 12.0f);
}

TEST(SimulationStateTest, ClearThrough) {
    an24::SimulationState state;

    state.allocate_signal(0.0f, {an24::Domain::Electrical, false});
    state.through[0] = 5.0f;
    state.conductance[0] = 2.0f;

    state.clear_through();

    EXPECT_FLOAT_EQ(state.through[0], 0.0f);
    EXPECT_FLOAT_EQ(state.conductance[0], 0.0f);
}
