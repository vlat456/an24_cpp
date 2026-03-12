#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/all.cpp"
#include "jit_solver/components/port_registry.h"


static Subtract<JitProvider> make_subtract() {
    Subtract<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);
    return comp;
}

static SimulationState make_state(size_t n = 4) {
    SimulationState st;
    st.across.resize(n, 0.0f);
    st.through.resize(n, 0.0f);
    st.conductance.resize(n, 0.0f);
    return st;
}

TEST(SubtractTest, BasicSubtraction_Positive) {
    auto comp = make_subtract();
    auto st = make_state();
    st.across[0] = 30.0f;  // A
    st.across[1] = 20.0f;  // B
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[2], 10.0f, 0.001f);
}

TEST(SubtractTest, BasicSubtraction_Negative) {
    auto comp = make_subtract();
    auto st = make_state();
    st.across[0] = 20.0f;
    st.across[1] = 30.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[2], -10.0f, 0.001f);
}

TEST(SubtractTest, ZeroInput) {
    auto comp = make_subtract();
    auto st = make_state();
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[2], 0.0f, 0.001f);
}

TEST(SubtractTest, EqualInputs) {
    auto comp = make_subtract();
    auto st = make_state();
    st.across[0] = 28.0f;
    st.across[1] = 28.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[2], 0.0f, 0.001f);
}

TEST(SubtractTest, LargePositive) {
    auto comp = make_subtract();
    auto st = make_state();
    st.across[0] = 100.0f;
    st.across[1] = 5.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[2], 95.0f, 0.001f);
}

TEST(SubtractTest, LargeNegative) {
    auto comp = make_subtract();
    auto st = make_state();
    st.across[0] = 5.0f;
    st.across[1] = 100.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[2], -95.0f, 0.001f);
}

TEST(SubtractTest, SmallDifference) {
    auto comp = make_subtract();
    auto st = make_state();
    st.across[0] = 28.5f;
    st.across[1] = 28.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[2], 0.5f, 0.001f);
}

TEST(SubtractTest, NegativeResult) {
    auto comp = make_subtract();
    auto st = make_state();
    st.across[0] = 28.0f;
    st.across[1] = 29.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[2], -1.0f, 0.001f);
}

TEST(SubtractTest, MultipleSteps_Stability) {
    auto comp = make_subtract();
    auto st = make_state();
    st.across[0] = 30.0f;
    st.across[1] = 20.0f;
    for (int i = 0; i < 10; i++) {
        comp.solve_logical(st, 1.0f / 60.0f);
    }
    EXPECT_NEAR(st.across[2], 10.0f, 0.001f);
}
