/// Splitter component tests — verifies o1 and o2 mirror input i
/// Also includes closed-loop regression for FirstOrderLag blueprint using Splitter

#include <gtest/gtest.h>
#include "core/solvers/jit/components/all.h"
#include "core/solvers/jit/components/all.cpp"
#include "core/solvers/common/port_registry.h"

// =============================================================================
// Test Helpers
// =============================================================================

template <typename Comp>
void step_component(Comp& comp, SimulationState& st, double dt) {
    comp.execute(st, dt);
    comp.commit(st, dt);
}

static SimulationState make_state(size_t n = 4) {
    SimulationState st;
    st.values.resize(n, 0.0f);
    return st;
}

static Splitter<JitProvider> make_splitter() {
    Splitter<JitProvider> comp;
    comp.provider.set(PortNames::i, 0);
    comp.provider.set(PortNames::o1, 1);
    comp.provider.set(PortNames::o2, 2);
    return comp;
}

// =============================================================================
// Splitter Unit Tests
// =============================================================================

TEST(SplitterTest, OutputsMirrorInput) {
    auto comp = make_splitter();
    auto st = make_state();

    st.values[0] = 42.0f;  // i

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], 42.0f);  // o1
    EXPECT_FLOAT_EQ(st.values[2], 42.0f);  // o2
}

TEST(SplitterTest, ZeroInput) {
    auto comp = make_splitter();
    auto st = make_state();

    st.values[0] = 0.0f;

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
    EXPECT_FLOAT_EQ(st.values[2], 0.0f);
}

TEST(SplitterTest, NegativeInput) {
    auto comp = make_splitter();
    auto st = make_state();

    st.values[0] = -7.5f;

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], -7.5f);
    EXPECT_FLOAT_EQ(st.values[2], -7.5f);
}

TEST(SplitterTest, LargeInput) {
    auto comp = make_splitter();
    auto st = make_state();

    st.values[0] = 1e6f;

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[1], 1e6f);
    EXPECT_FLOAT_EQ(st.values[2], 1e6f);
}

TEST(SplitterTest, OutputsTrackChangingInput) {
    auto comp = make_splitter();
    auto st = make_state();

    double dt = 1.0 / 60.0;

    st.values[0] = 10.0f;
    step_component(comp, st, dt);
    EXPECT_FLOAT_EQ(st.values[1], 10.0f);
    EXPECT_FLOAT_EQ(st.values[2], 10.0f);

    st.values[0] = 20.0f;
    step_component(comp, st, dt);
    EXPECT_FLOAT_EQ(st.values[1], 20.0f);
    EXPECT_FLOAT_EQ(st.values[2], 20.0f);

    st.values[0] = -3.0f;
    step_component(comp, st, dt);
    EXPECT_FLOAT_EQ(st.values[1], -3.0f);
    EXPECT_FLOAT_EQ(st.values[2], -3.0f);
}

TEST(SplitterTest, DomainIsLogical) {
    EXPECT_EQ(Splitter<JitProvider>::domain, Domain::Logical);
}

// =============================================================================
// FirstOrderLag Closed-Loop Regression (Splitter-based feedback)
// =============================================================================
// Manually build the FirstOrderLag topology:
//   in.port -> subtract.A
//   splitter.o1 -> subtract.B   (feedback)
//   subtract.o -> multiply.A
//   rate.port -> multiply.B
//   multiply.o -> accumulator.in
//   accumulator.out -> splitter.i
//   splitter.o2 -> out.port
//
// Filter equation: state += (target - state) * rate * dt
// With rate=6, dt=1/60, target=28.0, initial_state=0.6:
//   After many steps, state should converge to target (28.0).

TEST(SplitterTest, FirstOrderLag_ClosedLoop_ConvergesToTarget) {
    // Signal layout:
    //  0: in (target)
    //  1: rate
    //  2: subtract.A (= in)
    //  3: subtract.B (= splitter.o1 = accumulator state)
    //  4: subtract.o
    //  5: multiply.A (= subtract.o)
    //  6: multiply.B (= rate)
    //  7: multiply.o
    //  8: accumulator.in (= multiply.o)
    //  9: accumulator.out
    // 10: splitter.i (= accumulator.out)
    // 11: splitter.o1 (-> subtract.B = signal 3)
    // 12: splitter.o2 (-> out)
    //
    // With signal aliasing to simulate wiring:
    //  in.port = subtract.A = signal 0
    //  rate.port = multiply.B = signal 1
    //  subtract.o = multiply.A = signal 4
    //  multiply.o = accumulator.in = signal 7
    //  accumulator.out = splitter.i = signal 9
    //  splitter.o1 = subtract.B = signal 3
    //  splitter.o2 = out = signal 12

    SimulationState st;
    st.values.resize(13, 0.0f);

    // Components
    Subtract<JitProvider> sub;
    sub.provider.set(PortNames::A, 0);   // in (target)
    sub.provider.set(PortNames::B, 3);   // feedback (splitter.o1)
    sub.provider.set(PortNames::o, 4);

    Multiply<JitProvider> mul;
    mul.provider.set(PortNames::A, 4);   // subtract.o
    mul.provider.set(PortNames::B, 1);   // rate
    mul.provider.set(PortNames::o, 7);

    Accumulator<JitProvider> acc;
    acc.provider.set(PortNames::in, 7);   // multiply.o
    acc.provider.set(PortNames::out, 9);
    acc.initial_val = 0.6f;

    Splitter<JitProvider> spl;
    spl.provider.set(PortNames::i, 9);    // accumulator.out
    spl.provider.set(PortNames::o1, 3);    // -> subtract.B (feedback)
    spl.provider.set(PortNames::o2, 12);   // -> output

    // Set inputs
    st.values[0] = 28.0f;   // target
    st.values[1] = 6.0f;    // rate

    double dt = 1.0 / 60.0;

    // Run 600 steps (10 seconds)
    for (int i = 0; i < 600; ++i) {
        // Execute in data-flow order:
        // 1. Splitter (propagate accumulator state to feedback + output)
        spl.execute(st, dt);
        // 2. Subtract: error = target - state
        sub.execute(st, dt);
        // 3. Multiply: delta = error * rate
        mul.execute(st, dt);
        // 4. Accumulator: state += delta * dt
        acc.execute(st, dt);

        // Commit phase
        spl.commit(st, dt);
        sub.commit(st, dt);
        mul.commit(st, dt);
        acc.commit(st, dt);
    }

    float output = st.values[12];  // splitter.o2

    // After 10 seconds at rate=6, exponential decay converges:
    // time constant τ = 1/rate = 1/6 ≈ 0.167s
    // After 10s = 60τ, error < 1e-26. Should be essentially at target.
    EXPECT_NEAR(output, 28.0f, 0.01f)
        << "FirstOrderLag must converge to target 28.0V, got " << output;

    // Feedback signal must also be at target
    EXPECT_NEAR(st.values[3], 28.0f, 0.01f)
        << "Feedback (splitter.o1) must also be at target";
}

TEST(SplitterTest, FirstOrderLag_InitialValue_NotZero) {
    // Verify that the initial accumulator value (0.6) appears on output
    // during the first few frames, proving the Splitter propagates it.
    SimulationState st;
    st.values.resize(13, 0.0f);

    Subtract<JitProvider> sub;
    sub.provider.set(PortNames::A, 0);
    sub.provider.set(PortNames::B, 3);
    sub.provider.set(PortNames::o, 4);

    Multiply<JitProvider> mul;
    mul.provider.set(PortNames::A, 4);
    mul.provider.set(PortNames::B, 1);
    mul.provider.set(PortNames::o, 7);

    Accumulator<JitProvider> acc;
    acc.provider.set(PortNames::in, 7);
    acc.provider.set(PortNames::out, 9);
    acc.initial_val = 0.6f;

    Splitter<JitProvider> spl;
    spl.provider.set(PortNames::i, 9);
    spl.provider.set(PortNames::o1, 3);
    spl.provider.set(PortNames::o2, 12);

    // Target = initial, rate = 0 → no change, output should stay at initial_val
    st.values[0] = 0.6f;   // target = initial
    st.values[1] = 0.0f;   // rate = 0 → no movement

    double dt = 1.0 / 60.0;

    // Step a few times
    for (int i = 0; i < 5; ++i) {
        spl.execute(st, dt);
        sub.execute(st, dt);
        mul.execute(st, dt);
        acc.execute(st, dt);
        spl.commit(st, dt);
        sub.commit(st, dt);
        mul.commit(st, dt);
        acc.commit(st, dt);
    }

    float output = st.values[12];
    // With rate=0, accumulator should stay at initial_val=0.6
    // Splitter must propagate this to output
    EXPECT_NEAR(output, 0.6f, 0.01f)
        << "Output should hold at initial_val=0.6 when rate=0";
}
