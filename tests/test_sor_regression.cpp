#include <gtest/gtest.h>
#include "jit_solver/SOR_constants.h"
#include "jit_solver/state.h"

TEST(SorRegression, AdaptiveOmegaConstantsAreSane) {
    EXPECT_GE(SORAdaptive::OMEGA_MIN, 1.0f);
    EXPECT_GT(SORAdaptive::ERROR_WORSE_FACTOR, 1.0f);
    EXPECT_LT(SORAdaptive::ERROR_BETTER_FACTOR, 1.0f);
    EXPECT_LT(SORAdaptive::OMEGA_DOWNSCALE, 1.0f);
    EXPECT_GT(SORAdaptive::OMEGA_UPSCALE, 1.0f);
}

TEST(SorRegression, FixedSignalsNotModifiedBySor) {
    SimulationState st;
    st.across = {1.0f, 2.0f, 0.0f};
    st.through = {10.0f, -10.0f, 123.0f};
    st.conductance = {1.0f, 1.0f, 10.0f};
    st.inv_conductance.resize(3, 0.0f);

    st.dynamic_signals_count = 2; // signal 2 is fixed and must not be updated
    st.precompute_inv_conductance();

    solve_sor_iteration(
        st.across.data(),
        st.through.data(),
        st.inv_conductance.data(),
        st.dynamic_signals_count,
        SOR::OMEGA
    );

    EXPECT_NE(st.across[0], 1.0f);
    EXPECT_NE(st.across[1], 2.0f);
    EXPECT_FLOAT_EQ(st.across[2], 0.0f);
}

TEST(SorRegression, DomainScheduleCycleIsConsistent) {
    EXPECT_EQ(DomainSchedule::CYCLE_LENGTH % DomainSchedule::MECHANICAL_PERIOD, 0);
    EXPECT_EQ(DomainSchedule::CYCLE_LENGTH % DomainSchedule::HYDRAULIC_PERIOD, 0);
    EXPECT_EQ(DomainSchedule::CYCLE_LENGTH % DomainSchedule::THERMAL_PERIOD, 0);
}
