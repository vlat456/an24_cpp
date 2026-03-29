#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/state.h"
#include <cmath>

// =============================================================================
// Test Helpers
// =============================================================================

/// Port layout: [0]=v_in, [1]=v_out, [2]=i_out
static CurrentSense<JitProvider> make_current_sense(float g = 1000.0f) {
    CurrentSense<JitProvider> comp;
    comp.conductance = g;
    comp.provider.set(PortNames::v_in, 0);
    comp.provider.set(PortNames::v_out, 1);
    comp.provider.set(PortNames::i_out, 2);
    return comp;
}

static SimulationState make_state(size_t n = 3) {
    SimulationState st;
    st.values.resize(n, 0.0f);
    st.signal_types.resize(n, {Domain::Electrical, false});
    st.dynamic_signals_count = static_cast<uint32_t>(n);
    return st;
}

// =============================================================================
// Basic Push Behavior Tests
// =============================================================================

TEST(CurrentSense, ComputesCurrentFromVoltageDifference) {
    auto comp = make_current_sense(1000.0f);
    auto st = make_state();

    // v_in > v_out => positive current (conventional direction: in -> out)
    st.values[0] = 28.0f;  // v_in
    st.values[1] = 27.9f;  // v_out (slight drop across ammeter)

    comp.solve_electrical(st, 1.0f / 60.0f);

    float expected_i = (28.0f - 27.9f) * 1000.0f; // 100 A
    EXPECT_FLOAT_EQ(st.values[2], expected_i)
        << "i_out = (v_in - v_out) * conductance";
}

TEST(CurrentSense, ZeroCurrentWhenEqualVoltage) {
    auto comp = make_current_sense(1000.0f);
    auto st = make_state();
    st.values[0] = 28.0f;  // v_in
    st.values[1] = 28.0f;  // v_out (same as v_in)

    comp.solve_electrical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.values[2], 0.0f)
        << "i_out should be zero when v_in == v_out";
}

TEST(CurrentSense, NegativeCurrentWhenReversed) {
    // v_out > v_in => negative current (reverse flow)
    auto comp = make_current_sense(1000.0f);
    auto st = make_state();
    st.values[0] = 27.0f;  // v_in
    st.values[1] = 28.0f;  // v_out

    comp.solve_electrical(st, 1.0f / 60.0f);

    float expected_i = (27.0f - 28.0f) * 1000.0f; // -1000 A
    EXPECT_FLOAT_EQ(st.values[2], expected_i)
        << "i_out should be negative when v_out > v_in";
}

TEST(CurrentSense, UsesConfiguredConductance) {
    auto comp = make_current_sense(500.0f);
    auto st = make_state();
    st.values[0] = 10.0f;
    st.values[1] = 9.0f;

    comp.solve_electrical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.values[2], (10.0f - 9.0f) * 500.0f)
        << "i_out should use the configured conductance";
}

TEST(CurrentSense, ZeroVoltageDifference_ZeroCurrent) {
    auto comp = make_current_sense(1000.0f);
    auto st = make_state();
    // both v_in and v_out = 0 => no current
    st.values[0] = 0.0f;
    st.values[1] = 0.0f;

    comp.solve_electrical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.values[2], 0.0f)
        << "i_out should be zero when no voltage difference";
}

// =============================================================================
// CurrentSense execute() method
// =============================================================================

TEST(CurrentSense, ExecuteComputesCurrent) {
    auto comp = make_current_sense(1000.0f);
    auto st = make_state();
    st.values[0] = 28.0f;
    st.values[1] = 27.0f;

    comp.execute(st, 1.0f / 60.0f);

    float expected_i = (28.0f - 27.0f) * 1000.0f;
    EXPECT_FLOAT_EQ(st.values[2], expected_i);
}
