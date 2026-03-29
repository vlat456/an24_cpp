#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/all.cpp"
#include "jit_solver/components/port_registry.h"
#include <limits>


// =============================================================================
// Test Helpers
// =============================================================================

static Any_V_to_Bool<JitProvider> make_any_v_to_bool() {
    Any_V_to_Bool<JitProvider> comp;
    comp.provider.indices[PortNames::Vin] = 0;
    comp.provider.indices[PortNames::out] = 1;  // Note: Any_V_to_Bool uses PortNames::out
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
    st.values.resize(n, 0.0f);
    return st;
}

// =============================================================================
// Any_V_to_Bool Tests
// =============================================================================

TEST(Any_V_to_BoolTest, PositiveVoltageToTrue) {
    auto comp = make_any_v_to_bool();
    auto st = make_state();

    st.values[0] = 5.0f;  // positive voltage
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.values[1], 1.0f);  // TRUE
}

TEST(Any_V_to_BoolTest, NegativeVoltageToFalse) {
    auto comp = make_any_v_to_bool();
    auto st = make_state();

    st.values[0] = -5.0f;  // negative voltage
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.values[1], 0.0f);  // FALSE (threshold is > 0.5V, negative fails)
}

TEST(Any_V_to_BoolTest, ZeroVoltageToFalse) {
    auto comp = make_any_v_to_bool();
    auto st = make_state();

    st.values[0] = 0.0f;  // zero voltage
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.values[1], 0.0f);  // FALSE
}

TEST(Any_V_to_BoolTest, SmallPositiveVoltageToFalse) {
    auto comp = make_any_v_to_bool();
    auto st = make_state();

    st.values[0] = 0.001f;  // very small positive voltage (below 0.5V threshold)
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.values[1], 0.0f);  // FALSE (threshold is > 0.5V)
}

TEST(Any_V_to_BoolTest, SmallNegativeVoltageToFalse) {
    auto comp = make_any_v_to_bool();
    auto st = make_state();

    st.values[0] = -0.001f;  // very small negative voltage
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.values[1], 0.0f);  // FALSE (threshold is > 0.5V, negative fails)
}

TEST(Any_V_to_BoolTest, LargePositiveVoltageToTrue) {
    auto comp = make_any_v_to_bool();
    auto st = make_state();

    st.values[0] = 1000.0f;  // large positive voltage
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.values[1], 1.0f);  // TRUE
}

TEST(Any_V_to_BoolTest, LargeNegativeVoltageToFalse) {
    auto comp = make_any_v_to_bool();
    auto st = make_state();

    st.values[0] = -1000.0f;  // large negative voltage
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.values[1], 0.0f);  // FALSE (threshold is > 0.5V, negative fails)
}

// =============================================================================
// Positive_V_to_Bool Tests
// =============================================================================

TEST(Positive_V_to_BoolTest, PositiveVoltageToTrue) {
    auto comp = make_positive_v_to_bool();
    auto st = make_state();

    st.values[0] = 5.0f;  // positive voltage
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.values[1], 1.0f);  // TRUE
}

TEST(Positive_V_to_BoolTest, NegativeVoltageToFalse) {
    auto comp = make_positive_v_to_bool();
    auto st = make_state();

    st.values[0] = -5.0f;  // negative voltage
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.values[1], 0.0f);  // FALSE (only positive > 0)
}

TEST(Positive_V_to_BoolTest, ZeroVoltageToFalse) {
    auto comp = make_positive_v_to_bool();
    auto st = make_state();

    st.values[0] = 0.0f;  // zero voltage
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.values[1], 0.0f);  // FALSE (not > 0)
}

TEST(Positive_V_to_BoolTest, SmallPositiveVoltageToTrue) {
    auto comp = make_positive_v_to_bool();
    auto st = make_state();

    st.values[0] = 0.001f;  // very small positive voltage
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.values[1], 1.0f);  // TRUE (v > 0)
}

TEST(Positive_V_to_BoolTest, SmallNegativeVoltageToFalse) {
    auto comp = make_positive_v_to_bool();
    auto st = make_state();

    st.values[0] = -0.001f;  // very small negative voltage
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.values[1], 0.0f);  // FALSE (v is not > 0)
}

TEST(Positive_V_to_BoolTest, LargePositiveVoltageToTrue) {
    auto comp = make_positive_v_to_bool();
    auto st = make_state();

    st.values[0] = 1000.0f;  // large positive voltage
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.values[1], 1.0f);  // TRUE
}

TEST(Positive_V_to_BoolTest, LargeNegativeVoltageToFalse) {
    auto comp = make_positive_v_to_bool();
    auto st = make_state();

    st.values[0] = -1000.0f;  // large negative voltage
    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.values[1], 0.0f);  // FALSE
}

// =============================================================================
// Comparison Tests
// =============================================================================

TEST(V_to_BoolComparisonTest, AnyVsPositiveOnNegativeVoltage) {
    // Any_V_to_Bool: negative -> FALSE (threshold > 0.5V)
    auto any_comp = make_any_v_to_bool();
    auto st = make_state();
    st.values[0] = -10.0f;
    any_comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);

    // Positive_V_to_Bool: negative -> FALSE
    auto pos_comp = make_positive_v_to_bool();
    st = make_state();
    st.values[0] = -10.0f;
    pos_comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
}

TEST(V_to_BoolComparisonTest, AnyVsPositiveOnPositiveVoltage) {
    // Both should return TRUE for positive voltage
    auto any_comp = make_any_v_to_bool();
    auto st = make_state();
    st.values[0] = 10.0f;
    any_comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);

    auto pos_comp = make_positive_v_to_bool();
    st = make_state();
    st.values[0] = 10.0f;
    pos_comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);
}

TEST(V_to_BoolComparisonTest, AnyVsPositiveOnZeroVoltage) {
    // Both should return FALSE for zero voltage
    auto any_comp = make_any_v_to_bool();
    auto st = make_state();
    st.values[0] = 0.0f;
    any_comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);

    auto pos_comp = make_positive_v_to_bool();
    st = make_state();
    st.values[0] = 0.0f;
    pos_comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
}

// =============================================================================
// NaN/Inf Robustness Tests — Any_V_to_Bool
// =============================================================================
// These verify the std::isfinite guard: NaN and Inf must be treated as FALSE
// to prevent SOR-produced non-finite values from collapsing the logic tree.

TEST(Any_V_to_BoolTest, NaN_TreatedAsFalse) {
    auto comp = make_any_v_to_bool();
    auto st = make_state();
    st.values[0] = std::numeric_limits<float>::quiet_NaN();
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);  // NaN → FALSE
}

TEST(Any_V_to_BoolTest, SignalingNaN_TreatedAsFalse) {
    auto comp = make_any_v_to_bool();
    auto st = make_state();
    st.values[0] = std::numeric_limits<float>::signaling_NaN();
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);  // sNaN → FALSE
}

TEST(Any_V_to_BoolTest, PosInf_TreatedAsTrue) {
    auto comp = make_any_v_to_bool();
    auto st = make_state();
    st.values[0] = std::numeric_limits<float>::infinity();
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);  // TRUE (+Inf > 0.5f is true in IEEE 754)
}

TEST(Any_V_to_BoolTest, NegInf_TreatedAsFalse) {
    auto comp = make_any_v_to_bool();
    auto st = make_state();
    st.values[0] = -std::numeric_limits<float>::infinity();
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);  // -Inf → FALSE (not finite)
}

TEST(Any_V_to_BoolTest, NegativeZero_TreatedAsFalse) {
    auto comp = make_any_v_to_bool();
    auto st = make_state();
    st.values[0] = -0.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);  // -0.0 → FALSE (zero is zero)
}

TEST(Any_V_to_BoolTest, Denormalized_TreatedAsFalse) {
    auto comp = make_any_v_to_bool();
    auto st = make_state();
    st.values[0] = std::numeric_limits<float>::denorm_min();
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);  // FALSE (denorm_min ≤ 0.5V threshold)
}

// =============================================================================
// NaN/Inf Robustness Tests — Positive_V_to_Bool
// =============================================================================

TEST(Positive_V_to_BoolTest, NaN_TreatedAsFalse) {
    auto comp = make_positive_v_to_bool();
    auto st = make_state();
    st.values[0] = std::numeric_limits<float>::quiet_NaN();
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);  // NaN > 0.0f is false → FALSE
}

TEST(Positive_V_to_BoolTest, PosInf_TreatedAsTrue) {
    auto comp = make_positive_v_to_bool();
    auto st = make_state();
    st.values[0] = std::numeric_limits<float>::infinity();
    comp.solve_logical(st, 1.0f / 60.0f);
    // +Inf > 0.0f is true in IEEE 754
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);
}

TEST(Positive_V_to_BoolTest, NegInf_TreatedAsFalse) {
    auto comp = make_positive_v_to_bool();
    auto st = make_state();
    st.values[0] = -std::numeric_limits<float>::infinity();
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);  // -Inf > 0.0f is false → FALSE
}

TEST(Positive_V_to_BoolTest, NegativeZero_TreatedAsFalse) {
    auto comp = make_positive_v_to_bool();
    auto st = make_state();
    st.values[0] = -0.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);  // -0.0 > 0.0f is false → FALSE
}
