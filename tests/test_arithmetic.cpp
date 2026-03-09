#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/all.cpp"
#include "jit_solver/components/port_registry.h"

using namespace an24;

// =============================================================================
// Test Helpers
// =============================================================================

template <typename Comp>
static Comp make_component() {
    Comp comp;
    comp.provider.indices[PortNames::A] = 0;
    comp.provider.indices[PortNames::B] = 1;
    comp.provider.indices[PortNames::o] = 2;
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
// Multiply Tests
// =============================================================================

TEST(MultiplyTest, BasicMultiplication) {
    auto comp = make_component<Multiply<JitProvider>>();
    auto st = make_state();

    st.across[0] = 5.0f;  // A
    st.across[1] = 3.0f;  // B

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[2], 15.0f, 0.01f);  // o = 5 * 3
}

TEST(MultiplyTest, MultiplyByZero) {
    auto comp = make_component<Multiply<JitProvider>>();
    auto st = make_state();

    st.across[0] = 100.0f;
    st.across[1] = 0.0f;

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[2], 0.0f, 0.01f);
}

TEST(MultiplyTest, NegativeNumbers) {
    auto comp = make_component<Multiply<JitProvider>>();
    auto st = make_state();

    st.across[0] = -5.0f;
    st.across[1] = 3.0f;

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[2], -15.0f, 0.01f);
}

TEST(MultiplyTest, BothNegative) {
    auto comp = make_component<Multiply<JitProvider>>();
    auto st = make_state();

    st.across[0] = -4.0f;
    st.across[1] = -3.0f;

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[2], 12.0f, 0.01f);
}

TEST(MultiplyTest, LargeNumbers) {
    auto comp = make_component<Multiply<JitProvider>>();
    auto st = make_state();

    st.across[0] = 1000.0f;
    st.across[1] = 1000.0f;

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[2], 1000000.0f, 1.0f);
}

// =============================================================================
// Divide Tests
// =============================================================================

TEST(DivideTest, BasicDivision) {
    auto comp = make_component<Divide<JitProvider>>();
    auto st = make_state();

    st.across[0] = 15.0f;  // A
    st.across[1] = 3.0f;   // B

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[2], 5.0f, 0.01f);  // o = 15 / 3
}

TEST(DivideTest, DivideByZero) {
    auto comp = make_component<Divide<JitProvider>>();
    auto st = make_state();

    st.across[0] = 10.0f;
    st.across[1] = 0.0f;   // Division by zero

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[2], 0.0f, 0.01f);  // Should return 0
}

TEST(DivideTest, NegativeDivision) {
    auto comp = make_component<Divide<JitProvider>>();
    auto st = make_state();

    st.across[0] = -15.0f;
    st.across[1] = 3.0f;

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[2], -5.0f, 0.01f);
}

TEST(DivideTest, DivideByNegative) {
    auto comp = make_component<Divide<JitProvider>>();
    auto st = make_state();

    st.across[0] = 15.0f;
    st.across[1] = -3.0f;

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[2], -5.0f, 0.01f);
}

TEST(DivideTest, FractionalResult) {
    auto comp = make_component<Divide<JitProvider>>();
    auto st = make_state();

    st.across[0] = 10.0f;
    st.across[1] = 4.0f;

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[2], 2.5f, 0.01f);
}

// =============================================================================
// Add Tests
// =============================================================================

TEST(AddTest, BasicAddition) {
    auto comp = make_component<Add<JitProvider>>();
    auto st = make_state();

    st.across[0] = 5.0f;  // A
    st.across[1] = 3.0f;  // B

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[2], 8.0f, 0.01f);  // o = 5 + 3
}

TEST(AddTest, AddWithZero) {
    auto comp = make_component<Add<JitProvider>>();
    auto st = make_state();

    st.across[0] = 10.0f;
    st.across[1] = 0.0f;

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[2], 10.0f, 0.01f);
}

TEST(AddTest, AddNegativeNumbers) {
    auto comp = make_component<Add<JitProvider>>();
    auto st = make_state();

    st.across[0] = 10.0f;
    st.across[1] = -3.0f;

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[2], 7.0f, 0.01f);
}

TEST(AddTest, AddBothNegative) {
    auto comp = make_component<Add<JitProvider>>();
    auto st = make_state();

    st.across[0] = -5.0f;
    st.across[1] = -3.0f;

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[2], -8.0f, 0.01f);
}

TEST(AddTest, LargeNumbers) {
    auto comp = make_component<Add<JitProvider>>();
    auto st = make_state();

    st.across[0] = 100000.0f;
    st.across[1] = 200000.0f;

    comp.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[2], 300000.0f, 1.0f);
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
    mul.provider.indices[PortNames::A] = 0;
    mul.provider.indices[PortNames::B] = 1;
    mul.provider.indices[PortNames::o] = 2;

    // Add: A=2 (from mul), B=3, o=4
    add.provider.indices[PortNames::A] = 2;
    add.provider.indices[PortNames::B] = 3;
    add.provider.indices[PortNames::o] = 4;

    st.across[0] = 5.0f;
    st.across[1] = 3.0f;
    st.across[3] = 2.0f;

    mul.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[2], 15.0f, 0.01f);

    add.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[4], 17.0f, 0.01f);
}

TEST(ArithmeticTest, SubtractThenDivide) {
    // Test chaining: (10 - 2) / 4 = 2
    // Note: Can't easily test Subtract here as we only have 2-template helper
    // Just testing Divide with a pre-calculated value
    auto div = make_component<Divide<JitProvider>>();
    auto st = make_state();

    st.across[0] = 8.0f;  // Result of (10 - 2)
    st.across[1] = 4.0f;

    div.solve_logical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[2], 2.0f, 0.01f);
}
