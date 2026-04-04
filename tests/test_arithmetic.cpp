#include <gtest/gtest.h>
#include "core/solvers/jit/components/all.h"
#include "core/solvers/jit/components/all.cpp"
#include "core/solvers/jit/components/port_registry.h"


// =============================================================================
// Test Helpers
// =============================================================================

template <typename Comp>
void step_component(Comp& comp, SimulationState& st, double dt) {
    comp.execute(st, dt);
    comp.commit(st, dt);
}

template <typename Comp>
static Comp make_component() {
    Comp comp;
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

// =============================================================================
// Multiply Tests
// =============================================================================

TEST(MultiplyTest, BasicMultiplication) {
    auto comp = make_component<Multiply<JitProvider>>();
    auto st = make_state();

    st.values[0] = 5.0f;  // A
    st.values[1] = 3.0f;  // B

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_NEAR(st.values[2], 15.0f, 0.01f);  // o = 5 * 3
}

TEST(MultiplyTest, MultiplyByZero) {
    auto comp = make_component<Multiply<JitProvider>>();
    auto st = make_state();

    st.values[0] = 100.0f;
    st.values[1] = 0.0f;

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_NEAR(st.values[2], 0.0f, 0.01f);
}

TEST(MultiplyTest, NegativeNumbers) {
    auto comp = make_component<Multiply<JitProvider>>();
    auto st = make_state();

    st.values[0] = -5.0f;
    st.values[1] = 3.0f;

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_NEAR(st.values[2], -15.0f, 0.01f);
}

TEST(MultiplyTest, BothNegative) {
    auto comp = make_component<Multiply<JitProvider>>();
    auto st = make_state();

    st.values[0] = -4.0f;
    st.values[1] = -3.0f;

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_NEAR(st.values[2], 12.0f, 0.01f);
}

TEST(MultiplyTest, LargeNumbers) {
    auto comp = make_component<Multiply<JitProvider>>();
    auto st = make_state();

    st.values[0] = 1000.0f;
    st.values[1] = 1000.0f;

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_NEAR(st.values[2], 1000000.0f, 1.0f);
}

// =============================================================================
// Divide Tests
// =============================================================================

TEST(DivideTest, BasicDivision) {
    auto comp = make_component<Divide<JitProvider>>();
    auto st = make_state();

    st.values[0] = 15.0f;  // A
    st.values[1] = 3.0f;   // B

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_NEAR(st.values[2], 5.0f, 0.01f);  // o = 15 / 3
}

TEST(DivideTest, DivideByZero) {
    auto comp = make_component<Divide<JitProvider>>();
    auto st = make_state();

    st.values[0] = 10.0f;
    st.values[1] = 0.0f;   // Division by zero

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_NEAR(st.values[2], 0.0f, 0.01f);  // Should return 0
}

TEST(DivideTest, NegativeDivision) {
    auto comp = make_component<Divide<JitProvider>>();
    auto st = make_state();

    st.values[0] = -15.0f;
    st.values[1] = 3.0f;

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_NEAR(st.values[2], -5.0f, 0.01f);
}

TEST(DivideTest, DivideByNegative) {
    auto comp = make_component<Divide<JitProvider>>();
    auto st = make_state();

    st.values[0] = 15.0f;
    st.values[1] = -3.0f;

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_NEAR(st.values[2], -5.0f, 0.01f);
}

TEST(DivideTest, FractionalResult) {
    auto comp = make_component<Divide<JitProvider>>();
    auto st = make_state();

    st.values[0] = 10.0f;
    st.values[1] = 4.0f;

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_NEAR(st.values[2], 2.5f, 0.01f);
}

// =============================================================================
// Add Tests
// =============================================================================

TEST(AddTest, BasicAddition) {
    auto comp = make_component<Add<JitProvider>>();
    auto st = make_state();

    st.values[0] = 5.0f;  // A
    st.values[1] = 3.0f;  // B

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_NEAR(st.values[2], 8.0f, 0.01f);  // o = 5 + 3
}

TEST(AddTest, AddWithZero) {
    auto comp = make_component<Add<JitProvider>>();
    auto st = make_state();

    st.values[0] = 10.0f;
    st.values[1] = 0.0f;

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_NEAR(st.values[2], 10.0f, 0.01f);
}

TEST(AddTest, AddNegativeNumbers) {
    auto comp = make_component<Add<JitProvider>>();
    auto st = make_state();

    st.values[0] = 10.0f;
    st.values[1] = -3.0f;

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_NEAR(st.values[2], 7.0f, 0.01f);
}

TEST(AddTest, AddBothNegative) {
    auto comp = make_component<Add<JitProvider>>();
    auto st = make_state();

    st.values[0] = -5.0f;
    st.values[1] = -3.0f;

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_NEAR(st.values[2], -8.0f, 0.01f);
}

TEST(AddTest, LargeNumbers) {
    auto comp = make_component<Add<JitProvider>>();
    auto st = make_state();

    st.values[0] = 100000.0f;
    st.values[1] = 200000.0f;

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_NEAR(st.values[2], 300000.0f, 1.0f);
}

// =============================================================================
// Combined Tests
// =============================================================================

TEST(ArithmeticTest, MultiplyThenAdd) {
    // Test chaining: (5 * 3) + 2 = 17
    auto mul = make_component<Multiply<JitProvider>>();
    auto add = make_component<Add<JitProvider>>();
    auto st = make_state(6);  // Need more slots

    // Multiply: A=5, B=3, o=2
    mul.provider.set(PortNames::A, 0);
    mul.provider.set(PortNames::B, 1);
    mul.provider.set(PortNames::o, 2);

    // Add: A=2 (from mul), B=3, o=4
    add.provider.set(PortNames::A, 2);
    add.provider.set(PortNames::B, 3);
    add.provider.set(PortNames::o, 4);

    st.values[0] = 5.0f;
    st.values[1] = 3.0f;
    st.values[3] = 2.0f;

    step_component(mul, st, 1.0f / 60.0f);
    EXPECT_NEAR(st.values[2], 15.0f, 0.01f);

    step_component(add, st, 1.0f / 60.0f);
    EXPECT_NEAR(st.values[4], 17.0f, 0.01f);
}

TEST(ArithmeticTest, SubtractThenDivide) {
    // Test chaining: (10 - 2) / 4 = 2
    // Note: Can't easily test Subtract here as we only have 2-template helper
    // Just testing Divide with a pre-calculated value
    auto div = make_component<Divide<JitProvider>>();
    auto st = make_state();

    st.values[0] = 8.0f;  // Result of (10 - 2)
    st.values[1] = 4.0f;

    step_component(div, st, 1.0f / 60.0f);

    EXPECT_NEAR(st.values[2], 2.0f, 0.01f);
}
