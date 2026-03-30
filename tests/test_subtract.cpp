#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/all.cpp"
#include "jit_solver/components/port_registry.h"


// =============================================================================
// Test Helpers
// =============================================================================

template <typename Comp>
void step_component(Comp& comp, SimulationState& st, float dt) {
    comp.execute(st, dt);
    comp.commit(st, dt);
}

static Subtract<JitProvider> make_subtract() {
    Subtract<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);
    return comp;
}

static SimulationState make_state(size_t n = 4) {
    SimulationState st;
    st.values.resize(n, 0.0f);
    return st;
}

TEST(SubtractTest, BasicSubtraction_Positive) {
    auto comp = make_subtract();
    auto st = make_state();
    st.values[0] = 30.0f;  // A
    st.values[1] = 20.0f;  // B
    step_component(comp, st, 1.0f / 60.0f);
    EXPECT_NEAR(st.values[2], 10.0f, 0.001f);
}

TEST(SubtractTest, BasicSubtraction_Negative) {
    auto comp = make_subtract();
    auto st = make_state();
    st.values[0] = 20.0f;
    st.values[1] = 30.0f;
    step_component(comp, st, 1.0f / 60.0f);
    EXPECT_NEAR(st.values[2], -10.0f, 0.001f);
}

TEST(SubtractTest, ZeroInput) {
    auto comp = make_subtract();
    auto st = make_state();
    step_component(comp, st, 1.0f / 60.0f);
    EXPECT_NEAR(st.values[2], 0.0f, 0.001f);
}

TEST(SubtractTest, EqualInputs) {
    auto comp = make_subtract();
    auto st = make_state();
    st.values[0] = 28.0f;
    st.values[1] = 28.0f;
    step_component(comp, st, 1.0f / 60.0f);
    EXPECT_NEAR(st.values[2], 0.0f, 0.001f);
}

TEST(SubtractTest, LargePositive) {
    auto comp = make_subtract();
    auto st = make_state();
    st.values[0] = 100.0f;
    st.values[1] = 5.0f;
    step_component(comp, st, 1.0f / 60.0f);
    EXPECT_NEAR(st.values[2], 95.0f, 0.001f);
}

TEST(SubtractTest, LargeNegative) {
    auto comp = make_subtract();
    auto st = make_state();
    st.values[0] = 5.0f;
    st.values[1] = 100.0f;
    step_component(comp, st, 1.0f / 60.0f);
    EXPECT_NEAR(st.values[2], -95.0f, 0.001f);
}

TEST(SubtractTest, SmallDifference) {
    auto comp = make_subtract();
    auto st = make_state();
    st.values[0] = 28.5f;
    st.values[1] = 28.0f;
    step_component(comp, st, 1.0f / 60.0f);
    EXPECT_NEAR(st.values[2], 0.5f, 0.001f);
}

TEST(SubtractTest, NegativeResult) {
    auto comp = make_subtract();
    auto st = make_state();
    st.values[0] = 28.0f;
    st.values[1] = 29.0f;
    step_component(comp, st, 1.0f / 60.0f);
    EXPECT_NEAR(st.values[2], -1.0f, 0.001f);
}

TEST(SubtractTest, MultipleSteps_Stability) {
    auto comp = make_subtract();
    auto st = make_state();
    st.values[0] = 30.0f;
    st.values[1] = 20.0f;
    for (int i = 0; i < 10; i++) {
        step_component(comp, st, 1.0f / 60.0f);
    }
    EXPECT_NEAR(st.values[2], 10.0f, 0.001f);
}
