#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/all.cpp"
#include "jit_solver/components/port_registry.h"


// =============================================================================
// Test Helpers
// =============================================================================

static Any_V_to_Bool<JitProvider> make_any_v_to_bool() {
    Any_V_to_Bool<JitProvider> comp;
    comp.provider.indices[PortNames::Vin] = 0;
    comp.provider.indices[PortNames::o] = 1;
    return comp;
}

static Positive_V_to_Bool<JitProvider> make_positive_v_to_bool() {
    Positive_V_to_Bool<JitProvider> comp;
    comp.provider.indices[PortNames::Vin] = 0;
    comp.provider.indices[PortNames::o] = 1;
    return comp;
}

static SimulationState make_state(size_t n = 4) {
    SimulationState st;
    st.across.resize(n, 0.0f);
    st.through.resize(n, 0.0f);
    st.conductance.resize(n, 0.0f);
    return st;
}

// =============================================================================
// Any_V_to_Bool Tests
// =============================================================================

TEST(Any_V_to_BoolTest, PositiveVoltageToTrue) {
    auto comp = make_any_v_to_bool();
    auto st = make_state();

    st.across[0] = 5.0f;  // positive voltage
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[1], 1.0f, 0.01f);  // TRUE
}

TEST(Any_V_to_BoolTest, NegativeVoltageToTrue) {
    auto comp = make_any_v_to_bool();
    auto st = make_state();

    st.across[0] = -5.0f;  // negative voltage
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[1], 1.0f, 0.01f);  // TRUE (any non-zero voltage)
}

TEST(Any_V_to_BoolTest, ZeroVoltageToFalse) {
    auto comp = make_any_v_to_bool();
    auto st = make_state();

    st.across[0] = 0.0f;  // zero voltage
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[1], 0.0f, 0.01f);  // FALSE
}

TEST(Any_V_to_BoolTest, SmallPositiveVoltageToTrue) {
    auto comp = make_any_v_to_bool();
    auto st = make_state();

    st.across[0] = 0.001f;  // very small positive voltage
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[1], 1.0f, 0.01f);  // TRUE (any non-zero)
}

TEST(Any_V_to_BoolTest, SmallNegativeVoltageToTrue) {
    auto comp = make_any_v_to_bool();
    auto st = make_state();

    st.across[0] = -0.001f;  // very small negative voltage
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[1], 1.0f, 0.01f);  // TRUE (any non-zero)
}

TEST(Any_V_to_BoolTest, LargePositiveVoltageToTrue) {
    auto comp = make_any_v_to_bool();
    auto st = make_state();

    st.across[0] = 1000.0f;  // large positive voltage
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[1], 1.0f, 0.01f);  // TRUE
}

TEST(Any_V_to_BoolTest, LargeNegativeVoltageToTrue) {
    auto comp = make_any_v_to_bool();
    auto st = make_state();

    st.across[0] = -1000.0f;  // large negative voltage
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[1], 1.0f, 0.01f);  // TRUE
}

// =============================================================================
// Positive_V_to_Bool Tests
// =============================================================================

TEST(Positive_V_to_BoolTest, PositiveVoltageToTrue) {
    auto comp = make_positive_v_to_bool();
    auto st = make_state();

    st.across[0] = 5.0f;  // positive voltage
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[1], 1.0f, 0.01f);  // TRUE
}

TEST(Positive_V_to_BoolTest, NegativeVoltageToFalse) {
    auto comp = make_positive_v_to_bool();
    auto st = make_state();

    st.across[0] = -5.0f;  // negative voltage
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[1], 0.0f, 0.01f);  // FALSE (only positive > 0)
}

TEST(Positive_V_to_BoolTest, ZeroVoltageToFalse) {
    auto comp = make_positive_v_to_bool();
    auto st = make_state();

    st.across[0] = 0.0f;  // zero voltage
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[1], 0.0f, 0.01f);  // FALSE (not > 0)
}

TEST(Positive_V_to_BoolTest, SmallPositiveVoltageToTrue) {
    auto comp = make_positive_v_to_bool();
    auto st = make_state();

    st.across[0] = 0.001f;  // very small positive voltage
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[1], 1.0f, 0.01f);  // TRUE (v > 0)
}

TEST(Positive_V_to_BoolTest, SmallNegativeVoltageToFalse) {
    auto comp = make_positive_v_to_bool();
    auto st = make_state();

    st.across[0] = -0.001f;  // very small negative voltage
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[1], 0.0f, 0.01f);  // FALSE (v is not > 0)
}

TEST(Positive_V_to_BoolTest, LargePositiveVoltageToTrue) {
    auto comp = make_positive_v_to_bool();
    auto st = make_state();

    st.across[0] = 1000.0f;  // large positive voltage
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[1], 1.0f, 0.01f);  // TRUE
}

TEST(Positive_V_to_BoolTest, LargeNegativeVoltageToFalse) {
    auto comp = make_positive_v_to_bool();
    auto st = make_state();

    st.across[0] = -1000.0f;  // large negative voltage
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[1], 0.0f, 0.01f);  // FALSE
}

// =============================================================================
// Comparison Tests
// =============================================================================

TEST(V_to_BoolComparisonTest, AnyVsPositiveOnNegativeVoltage) {
    // Any_V_to_Bool: negative -> TRUE
    auto any_comp = make_any_v_to_bool();
    auto st = make_state();
    st.across[0] = -10.0f;
    any_comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 1.0f, 0.01f);

    // Positive_V_to_Bool: negative -> FALSE
    auto pos_comp = make_positive_v_to_bool();
    st = make_state();
    st.across[0] = -10.0f;
    pos_comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 0.0f, 0.01f);
}

TEST(V_to_BoolComparisonTest, AnyVsPositiveOnPositiveVoltage) {
    // Both should return TRUE for positive voltage
    auto any_comp = make_any_v_to_bool();
    auto st = make_state();
    st.across[0] = 10.0f;
    any_comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 1.0f, 0.01f);

    auto pos_comp = make_positive_v_to_bool();
    st = make_state();
    st.across[0] = 10.0f;
    pos_comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 1.0f, 0.01f);
}

TEST(V_to_BoolComparisonTest, AnyVsPositiveOnZeroVoltage) {
    // Both should return FALSE for zero voltage
    auto any_comp = make_any_v_to_bool();
    auto st = make_state();
    st.across[0] = 0.0f;
    any_comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 0.0f, 0.01f);

    auto pos_comp = make_positive_v_to_bool();
    st = make_state();
    st.across[0] = 0.0f;
    pos_comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 0.0f, 0.01f);
}
