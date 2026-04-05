#include <gtest/gtest.h>
#include "core/solvers/jit/components/all.h"
#include "core/solvers/jit/components/all.cpp"
#include "core/solvers/jit/components/port_registry.h"
#include <limits>


// =============================================================================
// Test Helpers
// =============================================================================

template <typename Comp>
void step_component(Comp& comp, SimulationState& st, double dt) {
    comp.execute(st, dt);
    comp.commit(st, dt);
}

// =============================================================================
// AND Gate Tests
// =============================================================================

static AND<JitProvider> make_and() {
    AND<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);
    return comp;
}

static SimulationState make_state() {
    SimulationState st;
    st.values.resize(3, 0.0f);
    return st;
}

TEST(ANDTest, BothTrue_ReturnsTrue) {
    auto comp = make_and();
    auto st = make_state();
    st.values[0] = 1.0f;  // A = TRUE
    st.values[1] = 1.0f;  // B = TRUE
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[2], 1.0f, 0.001f);  // o = TRUE
}

TEST(ANDTest, OneFalse_ReturnsFalse) {
    auto comp = make_and();
    auto st = make_state();
    st.values[0] = 1.0f;  // A = TRUE
    st.values[1] = 0.0f;  // B = FALSE
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[2], 0.0f, 0.001f);  // o = FALSE
}

TEST(ANDTest, BothFalse_ReturnsFalse) {
    auto comp = make_and();
    auto st = make_state();
    st.values[0] = 0.0f;  // A = FALSE
    st.values[1] = 0.0f;  // B = FALSE
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[2], 0.0f, 0.001f);  // o = FALSE
}

TEST(ANDTest, Threshold_0_5_IsTrue) {
    auto comp = make_and();
    auto st = make_state();
    st.values[0] = 0.6f;  // A = 0.6V > 0.5V → TRUE
    st.values[1] = 0.6f;  // B = 0.6V > 0.5V → TRUE
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[2], 1.0f, 0.001f);  // o = TRUE
}

TEST(ANDTest, Threshold_0_4_IsFalse) {
    auto comp = make_and();
    auto st = make_state();
    st.values[0] = 0.4f;  // A = 0.4V < 0.5V → FALSE
    st.values[1] = 1.0f;  // B = TRUE
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[2], 0.0f, 0.001f);  // o = FALSE
}

// =============================================================================
// OR Gate Tests
// =============================================================================

static OR<JitProvider> make_or() {
    OR<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);
    return comp;
}

TEST(ORTest, BothTrue_ReturnsTrue) {
    auto comp = make_or();
    auto st = make_state();
    st.values[0] = 1.0f;
    st.values[1] = 1.0f;
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[2], 1.0f, 0.001f);
}

TEST(ORTest, OneTrue_ReturnsTrue) {
    auto comp = make_or();
    auto st = make_state();
    st.values[0] = 1.0f;
    st.values[1] = 0.0f;
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[2], 1.0f, 0.001f);
}

TEST(ORTest, BothFalse_ReturnsFalse) {
    auto comp = make_or();
    auto st = make_state();
    st.values[0] = 0.0f;
    st.values[1] = 0.0f;
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[2], 0.0f, 0.001f);
}

TEST(ORTest, OtherInputTrue_ReturnsTrue) {
    auto comp = make_or();
    auto st = make_state();
    st.values[0] = 0.0f;
    st.values[1] = 1.0f;
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[2], 1.0f, 0.001f);
}

// =============================================================================
// XOR Gate Tests
// =============================================================================

static XOR<JitProvider> make_xor() {
    XOR<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);
    return comp;
}

TEST(XORTest, DifferentInputs_ReturnsTrue) {
    auto comp = make_xor();
    auto st = make_state();
    st.values[0] = 1.0f;  // A = TRUE
    st.values[1] = 0.0f;  // B = FALSE
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[2], 1.0f, 0.001f);
}

TEST(XORTest, SameInputs_BothTrue_ReturnsFalse) {
    auto comp = make_xor();
    auto st = make_state();
    st.values[0] = 1.0f;
    st.values[1] = 1.0f;
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[2], 0.0f, 0.001f);
}

TEST(XORTest, SameInputs_BothFalse_ReturnsFalse) {
    auto comp = make_xor();
    auto st = make_state();
    st.values[0] = 0.0f;
    st.values[1] = 0.0f;
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[2], 0.0f, 0.001f);
}

TEST(XORTest, OppositeInputs) {
    auto comp = make_xor();
    auto st = make_state();
    st.values[0] = 0.0f;
    st.values[1] = 1.0f;
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[2], 1.0f, 0.001f);
}

// =============================================================================
// NOT Gate Tests
// =============================================================================

static NOT<JitProvider> make_not() {
    NOT<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::o, 1);
    return comp;
}

static SimulationState make_state_2() {
    SimulationState st;
    st.values.resize(2, 0.0f);
    return st;
}

TEST(NOTTest, InvertTrueToFalse) {
    auto comp = make_not();
    auto st = make_state_2();
    st.values[0] = 1.0f;  // A = TRUE
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[1], 0.0f, 0.001f);  // o = FALSE
}

TEST(NOTTest, InvertFalseToTrue) {
    auto comp = make_not();
    auto st = make_state_2();
    st.values[0] = 0.0f;  // A = FALSE
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[1], 1.0f, 0.001f);  // o = TRUE
}

TEST(NOTTest, DoubleNegation) {
    auto comp1 = make_not();
    auto comp2 = make_not();
    auto st = make_state_2();

    st.values[0] = 1.0f;  // A = TRUE
    step_component(comp1, st, 1.0f / 60.0f);

    float after_first = st.values[1];

    // Second NOT (using output of first as input)
    st.values[0] = after_first;
    step_component(comp2, st, 1.0f / 60.0f);

    EXPECT_NEAR(st.values[1], 1.0f, 0.001f);  // !!TRUE = TRUE
}

TEST(NOTTest, Threshold_0_6_IsTrue_BecomesFalse) {
    auto comp = make_not();
    auto st = make_state_2();
    st.values[0] = 0.6f;  // A = 0.6V > 0.5V → TRUE
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[1], 0.0f, 0.001f);  // o = FALSE
}

TEST(NOTTest, Threshold_0_4_IsFalse_BecomesTrue) {
    auto comp = make_not();
    auto st = make_state_2();
    st.values[0] = 0.4f;  // A = 0.4V < 0.5V → FALSE
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[1], 1.0f, 0.001f);  // o = TRUE
}

// =============================================================================
// NAND Gate Tests
// =============================================================================

static NAND<JitProvider> make_nand() {
    NAND<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);
    return comp;
}

TEST(NANDTest, BothTrue_ReturnsFalse) {
    auto comp = make_nand();
    auto st = make_state();
    st.values[0] = 1.0f;
    st.values[1] = 1.0f;
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[2], 0.0f, 0.001f);  // !(TRUE && TRUE) = FALSE
}

TEST(NANDTest, OneFalse_ReturnsTrue) {
    auto comp = make_nand();
    auto st = make_state();
    st.values[0] = 1.0f;
    st.values[1] = 0.0f;
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[2], 1.0f, 0.001f);  // !(TRUE && FALSE) = TRUE
}

TEST(NANDTest, BothFalse_ReturnsTrue) {
    auto comp = make_nand();
    auto st = make_state();
    st.values[0] = 0.0f;
    st.values[1] = 0.0f;
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[2], 1.0f, 0.001f);  // !(FALSE && FALSE) = TRUE
}

TEST(NANDTest, TruthTable_Complete) {
    auto comp = make_nand();
    auto st = make_state();

    // FALSE, FALSE → TRUE
    st.values[0] = 0.0f; st.values[1] = 0.0f;
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[2], 1.0f, 0.001f);

    // FALSE, TRUE → TRUE
    st.values[0] = 0.0f; st.values[1] = 1.0f;
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[2], 1.0f, 0.001f);

    // TRUE, FALSE → TRUE
    st.values[0] = 1.0f; st.values[1] = 0.0f;
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[2], 1.0f, 0.001f);

    // TRUE, TRUE → FALSE
    st.values[0] = 1.0f; st.values[1] = 1.0f;
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[2], 0.0f, 0.001f);
}

// =============================================================================
// Combined Logic Tests
// =============================================================================

TEST(CombinedLogic, AND_OR_SameResult) {
    // AND then OR should match truth table
    auto and_comp = make_and();
    auto or_comp = make_or();
    auto st = make_state();

    // Both inputs TRUE: both gates TRUE
    st.values[0] = 1.0f;
    st.values[1] = 1.0f;

    step_component(and_comp, st, 1.0f / 60.0f);
    float and_out = st.values[2];

    step_component(or_comp, st, 1.0f / 60.0f);
    float or_out = st.values[2];

    EXPECT_NEAR(and_out, or_out, 0.001f);
}

TEST(CombinedLogic, XOR_Equals_1_WhenInputsDiffer) {
    auto comp = make_xor();
    auto st = make_state();

    st.values[0] = 1.0f;
    st.values[1] = 0.0f;
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[2], 1.0f, 0.001f);

    st.values[0] = 0.0f;
    st.values[1] = 1.0f;
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_NEAR(st.values[2], 1.0f, 0.001f);
}

TEST(CombinedLogic, NOT_NAND_Equivalent) {
    // NOT(NAND(A,B)) should equal AND(A,B)
    auto nand_comp = make_nand();
    auto not_comp = make_not();
    auto and_comp = make_and();

    auto st = make_state();
    st.values[0] = 1.0f;
    st.values[1] = 0.0f;

    // NAND output
    step_component(nand_comp, st, 1.0f / 60.0f);
    float nand_out = st.values[2];

    // NOT(NAND)
    auto st2 = make_state();
    st2.values[0] = nand_out;
    step_component(not_comp, st2, 1.0f / 60.0f);
    float not_nand_out = st2.values[1];

    // AND direct
    step_component(and_comp, st, 1.0f / 60.0f);
    float and_out = st.values[2];

    EXPECT_NEAR(not_nand_out, and_out, 0.001f);
}

// =============================================================================
// NaN Robustness Tests
// =============================================================================

TEST(NaNRobustness, AND_NaN_TreatedAsFalse) {
    auto comp = make_and();
    auto st = make_state();
    st.values[0] = std::numeric_limits<float>::quiet_NaN();
    st.values[1] = 1.0f;
    step_component(comp, st, 1.0 / 60.0);
    // NaN > 0.5f is false, so AND(false, true) = false
    EXPECT_FLOAT_EQ(st.values[2], 0.0f);
}

TEST(NaNRobustness, OR_NaN_TreatedAsFalse) {
    auto comp = make_or();
    auto st = make_state();
    st.values[0] = std::numeric_limits<float>::quiet_NaN();
    st.values[1] = 0.0f;
    step_component(comp, st, 1.0 / 60.0);
    // NaN > 0.5f is false, so OR(false, false) = false
    EXPECT_FLOAT_EQ(st.values[2], 0.0f);
}

TEST(NaNRobustness, OR_NaN_WithTrueInput_ReturnsTrue) {
    auto comp = make_or();
    auto st = make_state();
    st.values[0] = std::numeric_limits<float>::quiet_NaN();
    st.values[1] = 1.0f;
    step_component(comp, st, 1.0 / 60.0);
    // NaN > 0.5f is false, so OR(false, true) = true
    EXPECT_FLOAT_EQ(st.values[2], 1.0f);
}

TEST(NaNRobustness, NOT_NaN_TreatedAsFalse) {
    auto comp = make_not();
    auto st = make_state_2();
    st.values[0] = std::numeric_limits<float>::quiet_NaN();
    step_component(comp, st, 1.0 / 60.0);
    // NaN > 0.5f is false, so NOT(false) = true
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);
}

TEST(NaNRobustness, XOR_NaN_TreatedAsFalse) {
    auto comp = make_xor();
    auto st = make_state();
    st.values[0] = std::numeric_limits<float>::quiet_NaN();
    st.values[1] = 1.0f;
    step_component(comp, st, 1.0 / 60.0);
    // NaN > 0.5f is false, so XOR(false, true) = true
    EXPECT_FLOAT_EQ(st.values[2], 1.0f);
}

TEST(NaNRobustness, NAND_NaN_TreatedAsFalse) {
    auto comp = make_nand();
    auto st = make_state();
    st.values[0] = std::numeric_limits<float>::quiet_NaN();
    st.values[1] = 1.0f;
    step_component(comp, st, 1.0 / 60.0);
    // NaN > 0.5f is false, so NAND(false, true) = !(false && true) = true
    EXPECT_FLOAT_EQ(st.values[2], 1.0f);
}

TEST(NaNRobustness, Inf_TreatedAsTrue) {
#if defined(__FAST_MATH__)
    GTEST_SKIP() << "Inf/NaN semantics are undefined under -ffast-math";
#endif
    auto comp = make_and();
    auto st = make_state();
    st.values[0] = std::numeric_limits<float>::infinity();
    st.values[1] = 1.0f;
    step_component(comp, st, 1.0 / 60.0);
    // Inf > 0.5f is true, so AND(true, true) = true
    EXPECT_FLOAT_EQ(st.values[2], 1.0f);
}

TEST(NaNRobustness, NegInf_TreatedAsFalse) {
    auto comp = make_and();
    auto st = make_state();
    st.values[0] = -std::numeric_limits<float>::infinity();
    st.values[1] = 1.0f;
    step_component(comp, st, 1.0 / 60.0);
    // -Inf > 0.5f is false, so AND(false, true) = false
    EXPECT_FLOAT_EQ(st.values[2], 0.0f);
}
