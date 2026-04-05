#include <gtest/gtest.h>
#include "core/solvers/jit/components/all.h"
#include "core/solvers/jit/components/port_registry.h"
#include "core/solvers/jit/state.h"


// =============================================================================
// Test Helpers
// =============================================================================

static AsymTMO<JitProvider> make_asym_tmo(float tau_up = 0.1f, float tau_down = 0.5f, float deadzone = 0.001f)
{
    AsymTMO<JitProvider> comp;
    comp.tau_up = tau_up;
    comp.tau_down = tau_down;
    comp.deadzone = deadzone;
    comp.provider.set(PortNames::in, 0);
    comp.provider.set(PortNames::out, 1);
    comp.pre_load();
    return comp;
}

static FastTMO<JitProvider> make_fast_tmo(float tau = 0.1f, float deadzone = 0.001f)
{
    FastTMO<JitProvider> comp;
    comp.tau = tau;
    comp.deadzone = deadzone;
    comp.provider.set(PortNames::in, 0);
    comp.provider.set(PortNames::out, 1);
    comp.pre_load();
    return comp;
}

static SimulationState make_state(float input_val)
{
    SimulationState st;
    st.values.resize(2, 0.0f);
    st.values[0] = input_val;
    st.values[1] = 0.0f;
    return st;
}

/// Simulate one complete frame: execute + commit (two-phase semantics)
template <typename Comp>
void step_component(Comp& comp, SimulationState& st, double dt) {
    comp.execute(st, dt);
    comp.commit(st, dt);
}

#define step_asym(comp, st, dt) step_component(comp, st, dt)
#define step_fast(comp, st, dt) step_component(comp, st, dt)

// =============================================================================
// AsymTMO Tests
// =============================================================================

TEST(AsymTMOTest, ColdStart_FirstFrame)
{
    // First frame should immediately set output to input (branchless init)
    auto comp = make_asym_tmo();
    auto st = make_state(10.0f);

    step_asym(comp, st, 1.0 / 60.0);

    // Cold-start: output = cold-start-adjusted committed value = 10.0
    EXPECT_FLOAT_EQ(st.values[1], 10.0f);
    EXPECT_FLOAT_EQ(comp.first_frame_mask, 0.0f);
}

TEST(AsymTMOTest, AsymmetricResponse_RiseFastFallSlow)
{
    // tau_up=0.1 (fast), tau_down=0.5 (slow)
    auto comp = make_asym_tmo(0.1f, 0.5f);

    // Start at 0
    auto st = make_state(0.0f);
    step_asym(comp, st, 1.0 / 60.0);  // cold start at 0

    // Step to 10.0 (rising)
    st.values[0] = 10.0f;
    float out_after_rise = 0.0f;
    for (int i = 0; i < 30; ++i) {
        step_asym(comp, st, 1.0 / 60.0);
        out_after_rise = st.values[1];
    }

    // Step back to 0 (falling)
    st.values[0] = 0.0f;
    float out_falling = 0.0f;
    for (int i = 0; i < 30; ++i) {
        step_asym(comp, st, 1.0 / 60.0);
        out_falling = st.values[1];
    }

    // After same time, falling should be closer to start than rising was to end
    // (slower decay means it stays higher)
    EXPECT_GT(out_falling, 0.1f);  // Should still be > 0.1 after 30 frames
}

TEST(AsymTMOTest, SymmetricBehavior_WhenTauEqual)
{
    // When tau_up == tau_down, should behave like FastTMO
    auto comp = make_asym_tmo(0.1f, 0.1f);

    auto st = make_state(0.0f);
    step_asym(comp, st, 1.0 / 60.0);  // cold start at 0

    // Step up to 10.0
    st.values[0] = 10.0f;
    for (int i = 0; i < 10; ++i) {
        step_asym(comp, st, 1.0 / 60.0);
    }
    float out_rise = st.values[1];

    // Reset and step down
    auto comp2 = make_asym_tmo(0.1f, 0.1f);
    auto st2 = make_state(10.0f);
    step_asym(comp2, st2, 1.0f / 60.0f);  // cold start at 10

    st2.values[0] = 0.0f;
    for (int i = 0; i < 10; ++i) {
        step_asym(comp2, st2, 1.0f / 60.0f);
    }
    float out_fall = st2.values[1];

    // With symmetric taus, rise and fall rates should be similar
    // (checking that both respond significantly in 10 frames)
    EXPECT_GT(out_rise, 1.0f);
    EXPECT_LT(out_fall, 9.0f);
}

TEST(AsymTMOTest, Deadzone_PreventsMicroAdjustments)
{
    auto comp = make_asym_tmo(0.1f, 0.1f, 0.5f);  // Large deadzone

    auto st = make_state(5.0f);
    step_asym(comp, st, 1.0 / 60.0);  // cold start at 5

    float initial_out = st.values[1];

    // Change input by less than deadzone
    st.values[0] = 5.3f;  // diff = 0.3 < deadzone (0.5)

    for (int i = 0; i < 10; ++i) {
        step_asym(comp, st, 1.0 / 60.0);
    }

    // Output should not have changed significantly
    EXPECT_NEAR(st.values[1], initial_out, 0.01f);
}

TEST(AsymTMOTest, Deadzone_AllowsLargeChanges)
{
    auto comp = make_asym_tmo(0.1f, 0.1f, 0.5f);

    auto st = make_state(5.0f);
    step_asym(comp, st, 1.0 / 60.0);  // cold start at 5

    // Change input by MORE than deadzone
    st.values[0] = 10.0f;  // diff = 5.0 > deadzone (0.5)

    for (int i = 0; i < 30; ++i) {
        step_asym(comp, st, 1.0 / 60.0);
    }

    // Output should approach new input
    EXPECT_GT(st.values[1], 7.0f);
}

TEST(AsymTMOTest, SelectsTauUp_WhenDiffPositive)
{
    auto comp = make_asym_tmo(0.05f, 1.0f);  // Fast rise, very slow fall

    auto st = make_state(0.0f);
    step_asym(comp, st, 1.0 / 60.0);  // cold start at 0

    // Rising edge (input > current_value)
    st.values[0] = 10.0f;
    step_asym(comp, st, 1.0 / 60.0);  // stages first rise step
    float after_first_rise = st.values[1];

    for (int i = 0; i < 10; ++i) {
        step_asym(comp, st, 1.0 / 60.0);
    }

    // With fast tau_up, should quickly approach target
    EXPECT_GT(st.values[1], 8.0f);
    EXPECT_GT(st.values[1], after_first_rise);
}

TEST(AsymTMOTest, SelectsTauDown_WhenDiffNegative)
{
    auto comp = make_asym_tmo(0.01f, 1.0f);  // Very fast rise, very slow fall

    auto st = make_state(10.0f);
    step_asym(comp, st, 1.0 / 60.0);  // cold start at 10

    // Falling edge (input < current_value)
    st.values[0] = 0.0f;

    for (int i = 0; i < 10; ++i) {
        step_asym(comp, st, 1.0 / 60.0);
    }

    // With slow tau_down, should fall very slowly
    EXPECT_GT(st.values[1], 5.0f);  // Still above 5 after 10 frames
}

TEST(AsymTMOTest, PreLoad_ComputesInverseTaus)
{
    auto comp = make_asym_tmo(0.1f, 0.5f);

    EXPECT_NEAR(comp.inv_tau_up, 10.0f, 0.01f);
    EXPECT_NEAR(comp.inv_tau_down, 2.0f, 0.01f);
}

TEST(AsymTMOTest, PreLoad_HandlesZeroTau)
{
    AsymTMO<JitProvider> comp;
    comp.tau_up = 0.0f;
    comp.tau_down = 0.0f;
    comp.provider.set(PortNames::in, 0);
    comp.provider.set(PortNames::out, 1);
    comp.pre_load();

    // Should clamp to minimum (1/0.0001 = 10000)
    EXPECT_FLOAT_EQ(comp.inv_tau_up, 10000.0f);
    EXPECT_FLOAT_EQ(comp.inv_tau_down, 10000.0f);
}

TEST(AsymTMOTest, HandlesZeroDt_Pause)
{
    auto comp = make_asym_tmo();
    auto st = make_state(5.0f);

    step_asym(comp, st, 1.0 / 60.0);  // cold start at 5
    float out_before_pause = st.values[1];

    // Simulate pause (dt = 0)
    for (int i = 0; i < 10; ++i) {
        step_asym(comp, st, 0.0f);
    }

    // Output should not change during pause
    EXPECT_FLOAT_EQ(st.values[1], out_before_pause);
}

TEST(AsymTMOTest, ExtremeValues_LargeTau)
{
    // Very large tau means very slow response
    auto comp = make_asym_tmo(100.0f, 100.0f);

    auto st = make_state(0.0f);
    step_asym(comp, st, 1.0 / 60.0);  // cold start at 0

    st.values[0] = 10.0f;
    for (int i = 0; i < 10; ++i) {
        step_asym(comp, st, 1.0 / 60.0);
    }

    // With very large tau, output should barely move
    EXPECT_LT(st.values[1], 1.0f);
}

TEST(AsymTMOTest, ExtremeValues_SmallTau)
{
    // Very small tau means very fast response
    auto comp = make_asym_tmo(0.001f, 0.001f);

    auto st = make_state(0.0f);
    step_asym(comp, st, 1.0 / 60.0);  // cold start at 0

    st.values[0] = 10.0f;
    step_asym(comp, st, 1.0 / 60.0);  // output = committed 0, stages ~10
    // One-frame delay: output still 0 after first step
    step_asym(comp, st, 1.0 / 60.0);  // output = committed ~10

    // With very small tau, should reach target after one-frame delay
    EXPECT_GT(st.values[1], 9.0f);
}

// =============================================================================
// FastTMO Tests
// =============================================================================

TEST(FastTMOTest, ColdStart_FirstFrame)
{
    auto comp = make_fast_tmo();
    auto st = make_state(10.0f);

    step_fast(comp, st, 1.0 / 60.0);

    // Cold-start: output = cold-start-adjusted committed value = 10.0
    EXPECT_FLOAT_EQ(st.values[1], 10.0f);
    EXPECT_FLOAT_EQ(comp.first_frame_mask, 0.0f);
}

TEST(FastTMOTest, BasicLowPassFiltering)
{
    auto comp = make_fast_tmo(0.1f);

    auto st = make_state(0.0f);
    step_fast(comp, st, 1.0 / 60.0);  // cold start at 0

    // Step to 10.0
    st.values[0] = 10.0f;

    for (int i = 0; i < 30; ++i) {
        step_fast(comp, st, 1.0 / 60.0);
    }

    // Should approach 10.0 (exponential decay)
    EXPECT_GT(st.values[1], 7.0f);
    EXPECT_LT(st.values[1], 10.0f);
}

TEST(FastTMOTest, LargerTau_SlowerResponse)
{
    auto comp_fast = make_fast_tmo(0.01f);
    auto comp_slow = make_fast_tmo(1.0f);

    auto st_fast = make_state(0.0f);
    auto st_slow = make_state(0.0f);

    step_fast(comp_fast, st_fast, 1.0f / 60.0f);  // cold start
    step_fast(comp_slow, st_slow, 1.0f / 60.0f);  // cold start

    st_fast.values[0] = 10.0f;
    st_slow.values[0] = 10.0f;

    for (int i = 0; i < 10; ++i) {
        step_fast(comp_fast, st_fast, 1.0f / 60.0f);
        step_fast(comp_slow, st_slow, 1.0f / 60.0f);
    }

    // Fast tau (0.01) should respond more quickly than slow tau (1.0)
    EXPECT_GT(st_fast.values[1], st_slow.values[1]);
}

TEST(FastTMOTest, Deadzone_PreventsMicroAdjustments)
{
    auto comp = make_fast_tmo(0.1f, 0.5f);

    auto st = make_state(5.0f);
    step_fast(comp, st, 1.0 / 60.0);  // cold start at 5

    float initial_out = st.values[1];

    // Change by less than deadzone
    st.values[0] = 5.3f;

    for (int i = 0; i < 10; ++i) {
        step_fast(comp, st, 1.0 / 60.0);
    }

    EXPECT_NEAR(st.values[1], initial_out, 0.01f);
}

TEST(FastTMOTest, HandlesZeroDt_Pause)
{
    auto comp = make_fast_tmo();
    auto st = make_state(5.0f);

    step_fast(comp, st, 1.0 / 60.0);  // cold start at 5
    float out_before_pause = st.values[1];

    for (int i = 0; i < 10; ++i) {
        step_fast(comp, st, 0.0f);
    }

    EXPECT_FLOAT_EQ(st.values[1], out_before_pause);
}

TEST(FastTMOTest, RationalApproximation_Stable)
{
    // The rational approximation should be stable for any dt
    auto comp = make_fast_tmo(0.1f);

    auto st = make_state(0.0f);
    step_fast(comp, st, 1.0 / 60.0);  // cold start at 0

    st.values[0] = 10.0f;

    // Even with very large dt (lag spike), should not explode
    step_fast(comp, st, 1.0f);  // 1 second!

    EXPECT_FALSE(std::isinf(st.values[1]));
    EXPECT_FALSE(std::isnan(st.values[1]));
    // One-frame delay: output still shows 0.0 (cold-start committed value)
    // The staged value is clamped to [0, 10]; it will appear next frame
    EXPECT_GE(st.values[1], 0.0f);
    EXPECT_LE(st.values[1], 10.0f);
}

TEST(FastTMOTest, PreLoad_ComputesInverseTau)
{
    auto comp = make_fast_tmo(0.2f);

    EXPECT_NEAR(comp.inv_tau, 5.0f, 0.01f);
}

TEST(FastTMOTest, PreLoad_HandlesZeroTau)
{
    FastTMO<JitProvider> comp;
    comp.tau = 0.0f;
    comp.provider.set(PortNames::in, 0);
    comp.provider.set(PortNames::out, 1);
    comp.pre_load();

    EXPECT_FLOAT_EQ(comp.inv_tau, 10000.0f);
}

TEST(FastTMOTest, StatePersistsBetweenFrames)
{
    auto comp = make_fast_tmo(0.1f);

    auto st = make_state(0.0f);
    step_fast(comp, st, 1.0 / 60.0);  // cold start at 0

    st.values[0] = 10.0f;
    step_fast(comp, st, 1.0 / 60.0);  // output = 0.0 (committed), stages first step
    step_fast(comp, st, 1.0 / 60.0);  // output = first step value
    float out1 = st.values[1];

    // Same input, next frame should continue approaching target
    step_fast(comp, st, 1.0 / 60.0);
    float out2 = st.values[1];

    EXPECT_GT(out2, out1);
    EXPECT_LT(out2, 10.0f);
}

TEST(FastTMOTest, ZeroDeadzone_AllowsAllChanges)
{
    auto comp = make_fast_tmo(0.1f, 0.0f);

    auto st = make_state(5.0f);
    step_fast(comp, st, 1.0 / 60.0);  // cold start at 5

    // Even tiny change should propagate
    st.values[0] = 5.001f;

    for (int i = 0; i < 20; ++i) {
        step_fast(comp, st, 1.0 / 60.0);
    }

    // Should approach new value
    EXPECT_NEAR(st.values[1], 5.001f, 0.01f);
}

// =============================================================================
// Combined Tests
// =============================================================================

TEST(TMOTest, AsymVsFast_WithSameParameters)
{
    // When taus are equal, AsymTMO should behave like FastTMO
    auto asym = make_asym_tmo(0.1f, 0.1f, 0.0f);
    auto fast = make_fast_tmo(0.1f, 0.0f);

    auto st_asym = make_state(0.0f);
    auto st_fast = make_state(0.0f);

    step_asym(asym, st_asym, 1.0f / 60.0f);  // cold start
    step_fast(fast, st_fast, 1.0f / 60.0f);   // cold start

    // Step to 10.0
    st_asym.values[0] = 10.0f;
    st_fast.values[0] = 10.0f;

    for (int i = 0; i < 30; ++i) {
        step_asym(asym, st_asym, 1.0f / 60.0f);
        step_fast(fast, st_fast, 1.0f / 60.0f);
    }

    // Outputs should be very close
    EXPECT_NEAR(st_asym.values[1], st_fast.values[1], 0.1f);
}
