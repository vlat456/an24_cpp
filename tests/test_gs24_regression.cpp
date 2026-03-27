#include <gtest/gtest.h>

#include "jit_solver/SOR_constants.h"
#include "jit_solver/components/all.cpp"
#include "jit_solver/components/all.h"
#include "jit_solver/components/port_registry.h"

namespace {

GS24<JitProvider> make_gs24() {
    GS24<JitProvider> gs;
    gs.mode = GS24Mode::STARTER;
    gs.current_rpm = 0.0f;
    gs.target_rpm = 15000.0f;
    gs.rpm_cutoff = 0.45f;
    gs.rpm_threshold = 0.4f;
    gs.k_motor = 0.5f;
    gs.i_max_starter = 800.0f;
    gs.i_max = 400.0f;
    gs.r_internal = 0.025f;
    gs.r_norton = 0.08f;
    gs.pre_load();

    gs.provider.indices[PortNames::v_out] = 0;
    gs.provider.indices[PortNames::k_mod] = 1;
    return gs;
}

SimulationState make_state(size_t n = 4) {
    SimulationState st;
    st.across.resize(n, 0.0f);
    st.through.resize(n, 0.0f);
    st.conductance.resize(n, 0.0f);
    st.inv_conductance.resize(n, 0.0f);
    st.dynamic_signals_count = static_cast<uint32_t>(n);
    return st;
}

} // namespace

TEST(GS24Regression, SolveElectricalDoesNotMutateModeOrRpm) {
    auto gs = make_gs24();
    auto st = make_state();

    const GS24Mode mode0 = gs.mode;
    const float rpm0 = gs.current_rpm;
    st.across[1] = 1.0f;

    for (int i = 0; i < 20; ++i) {
        gs.solve_electrical(st, 1.0f / 60.0f);
    }

    EXPECT_EQ(gs.mode, mode0);
    EXPECT_FLOAT_EQ(gs.current_rpm, rpm0);
}

TEST(GS24Regression, FinalizeTransitionsStarterToGenerator) {
    auto gs = make_gs24();
    auto st = make_state();
    const float dt = 1.0f / 60.0f;

    // STARTER acceleration is 300 rpm/s. Reaching cutoff (0.45 * 15000 = 6750)
    // requires ~22.5s, then STARTER_WAIT requires 1.0s before GENERATOR.
    bool reached_wait = false;
    bool reached_generator = false;
    for (int i = 0; i < 1600; ++i) {
        gs.finalize_step(st, dt);
        if (gs.mode == GS24Mode::STARTER_WAIT) {
            reached_wait = true;
        }
        if (gs.mode == GS24Mode::GENERATOR) {
            reached_generator = true;
            break;
        }
    }

    EXPECT_TRUE(reached_wait);
    EXPECT_TRUE(reached_generator);
    EXPECT_GE(gs.current_rpm, gs.target_rpm * gs.rpm_cutoff);
}

TEST(GS24Regression, DeterministicRegardlessOfElectricalIterationCount) {
    auto run = [&](int electrical_calls_per_step) {
        auto gs = make_gs24();
        auto st = make_state();
        st.across[1] = 1.0f;

        const float dt = 1.0f / 60.0f;
        for (int step = 0; step < 300; ++step) {
            st.through.assign(st.through.size(), 0.0f);
            st.conductance.assign(st.conductance.size(), 0.0f);
            for (int i = 0; i < electrical_calls_per_step; ++i) {
                gs.solve_electrical(st, dt);
            }
            gs.finalize_step(st, dt);
        }
        return std::pair{gs.mode, gs.current_rpm};
    };

    auto [mode3, rpm3] = run(3);
    auto [mode30, rpm30] = run(30);

    EXPECT_EQ(mode3, mode30);
    EXPECT_NEAR(rpm3, rpm30, 1e-4f);
}

TEST(GS24Regression, FixedVsVariableDt_BaselineEquivalentAfterSameSimTime) {
    auto run_for_time = [](const std::vector<float>& dts, float total_time) {
        auto gs = make_gs24();
        auto st = make_state();

        float t = 0.0f;
        size_t i = 0;
        while (t < total_time) {
            const float dt_nominal = dts[i % dts.size()];
            const float dt = std::min(dt_nominal, total_time - t);

            st.through.assign(st.through.size(), 0.0f);
            st.conductance.assign(st.conductance.size(), 0.0f);
            gs.solve_electrical(st, dt);
            gs.finalize_step(st, dt);

            t += dt;
            ++i;
        }

        return std::pair{gs.mode, gs.current_rpm};
    };

    const float total_time = 24.0f;
    auto [mode_fixed, rpm_fixed] = run_for_time({1.0f / 60.0f}, total_time);
    auto [mode_var, rpm_var] = run_for_time({1.0f / 144.0f, 1.0f / 50.0f, 1.0f / 200.0f, 1.0f / 75.0f}, total_time);

    EXPECT_EQ(mode_fixed, mode_var)
        << "GS24 mode progression should depend on simulated time, not caller cadence";
    EXPECT_NEAR(rpm_fixed, rpm_var, 10.0f)
        << "GS24 RPM should be cadence-agnostic for equal simulated time";
}
