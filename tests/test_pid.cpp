#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/state.h"
#include <algorithm>
#include <cmath>

using namespace an24;

// Helper: build JIT PID with ports wired to: [0]=setpoint, [1]=feedback, [2]=output
static PID<JitProvider> make_pid(float Kp = 1.0f, float Ki = 0.0f, float Kd = 0.0f,
                                  float out_min = -1000.0f, float out_max = 1000.0f,
                                  float alpha = 0.2f)
{
    PID<JitProvider> pid;
    pid.Kp = Kp;
    pid.Ki = Ki;
    pid.Kd = Kd;
    pid.output_min = out_min;
    pid.output_max = out_max;
    pid.filter_alpha = alpha;
    pid.provider.set(PortNames::setpoint, 0);
    pid.provider.set(PortNames::feedback, 1);
    pid.provider.set(PortNames::output,   2);
    return pid;
}

static SimulationState make_state(float sp, float fb)
{
    SimulationState st;
    st.across.resize(3, 0.0f);
    st.through.resize(3, 0.0f);
    st.conductance.resize(3, 0.0f);
    st.across[0] = sp;
    st.across[1] = fb;
    st.across[2] = 0.0f;
    return st;
}

// ─── P only ──────────────────────────────────────────────────────────────────

TEST(PIDTest, ProportionalOnly)
{
    auto pid = make_pid(/*Kp=*/2.0f);
    auto st  = make_state(/*sp=*/10.0f, /*fb=*/0.0f);

    pid.post_step(st, 0.016f);

    // output = Kp * (sp - fb) = 2 * 10 = 20
    EXPECT_FLOAT_EQ(st.across[2], 20.0f);
}

TEST(PIDTest, ProportionalSign)
{
    auto pid = make_pid(/*Kp=*/1.0f);
    auto st  = make_state(/*sp=*/-5.0f, /*fb=*/5.0f);

    pid.post_step(st, 0.016f);

    EXPECT_FLOAT_EQ(st.across[2], -10.0f);
}

// ─── I only ──────────────────────────────────────────────────────────────────

TEST(PIDTest, IntegralAccumulatesOverOneSecond_60Hz)
{
    auto pid = make_pid(/*Kp=*/0.0f, /*Ki=*/1.0f);

    SimulationState st = make_state(5.0f, 0.0f);

    const float dt = 1.0f / 60.0f;
    const int   N  = 60;
    for (int i = 0; i < N; ++i) {
        pid.post_step(st, dt);
    }

    // integral = 5.0 * 1.0 = 5.0 after 1 second (regardless of dt size)
    EXPECT_NEAR(st.across[2], 5.0f, 1e-3f);
}

TEST(PIDTest, IntegralTimeInvariance)
{
    // Same conditions at 60 Hz and 144 Hz should yield comparable integral
    // after 1 second
    auto pid60  = make_pid(0.0f, 1.0f);
    auto pid144 = make_pid(0.0f, 1.0f);

    SimulationState st60  = make_state(5.0f, 0.0f);
    SimulationState st144 = make_state(5.0f, 0.0f);

    for (int i = 0; i < 60;  ++i) pid60 .post_step(st60,  1.0f / 60.0f);
    for (int i = 0; i < 144; ++i) pid144.post_step(st144, 1.0f / 144.0f);

    EXPECT_NEAR(st60.across[2], st144.across[2], 5e-3f);
}

// ─── D only ──────────────────────────────────────────────────────────────────

TEST(PIDTest, DerivativeFilterReducesNoise)
{
    // Kd=1, alpha=0.2 — filtered amplitude must be < raw d amplitude
    auto pid = make_pid(0.0f, 0.0f, /*Kd=*/1.0f, -1e9f, 1e9f, /*alpha=*/0.2f);

    const float dt = 0.01f;
    float max_filtered = 0.0f;
    float max_raw      = 0.0f;

    SimulationState st = make_state(0.0f, 0.0f);

    // High-frequency noise: alternate +1 / -1 every step
    for (int i = 0; i < 100; ++i) {
        float noise = (i % 2 == 0) ? 1.0f : -1.0f;
        st.across[1] = noise;   // feedback with noise
        pid.post_step(st, dt);

        float raw = std::abs((noise - (i > 0 ? ((i - 1) % 2 == 0 ? 1.0f : -1.0f) : 0.0f)) / dt);
        max_raw      = std::max(max_raw, raw);
        max_filtered = std::max(max_filtered, std::abs(st.across[2]));
    }

    EXPECT_LT(max_filtered, max_raw);
}

// ─── Anti-windup ─────────────────────────────────────────────────────────────

TEST(PIDTest, AntiWindupCapsOutput)
{
    // Large error, large Ki, tight output cap → output must stay ≤ cap
    auto pid = make_pid(/*Kp=*/1.0f, /*Ki=*/10.0f, 0.0f,
                        /*out_min=*/-10.0f, /*out_max=*/10.0f);
    auto st  = make_state(/*sp=*/100.0f, /*fb=*/0.0f);

    for (int i = 0; i < 200; ++i) {
        pid.post_step(st, 0.016f);
    }

    EXPECT_LE(st.across[2], 10.0f);
    EXPECT_GE(st.across[2], -10.0f);
}

TEST(PIDTest, AntiWindupIntegralClamped)
{
    // The integral contribution alone must not push output beyond [min, max]
    // Run with Kp=0 so only integral contributes
    auto pid = make_pid(/*Kp=*/0.0f, /*Ki=*/100.0f, 0.0f,
                        /*out_min=*/-5.0f, /*out_max=*/5.0f);
    auto st  = make_state(10.0f, 0.0f);

    for (int i = 0; i < 1000; ++i) {
        pid.post_step(st, 0.016f);
    }

    EXPECT_LE(st.across[2], 5.0f);
    EXPECT_GE(st.across[2], -5.0f);
}

// ─── solve_electrical stamps conductance ─────────────────────────────────────

TEST(PIDTest, SolveElectricalStampsConductance)
{
    auto pid = make_pid();
    auto st  = make_state(0.0f, 0.0f);

    pid.solve_electrical(st, 0.016f);

    // Output node gets a small conductance to ground (high-impedance stamp)
    EXPECT_GT(st.conductance[2], 0.0f);
    EXPECT_LT(st.conductance[2], 1e-4f);
}

// ─── Edge cases ───────────────────────────────────────────────────────────────

TEST(PIDTest, ZeroErrorProducesZeroPAndD)
{
    // setpoint == feedback → error = 0
    auto pid = make_pid(/*Kp=*/2.0f, /*Ki=*/0.0f, /*Kd=*/1.0f);
    auto st = make_state(/*sp=*/5.0f, /*fb=*/5.0f);

    pid.post_step(st, 0.016f);

    // P and D terms should be zero (error = 0, delta_error = 0)
    EXPECT_FLOAT_EQ(st.across[2], 0.0f);
}

TEST(PIDTest, AllGainsZero)
{
    // Kp=Ki=Kd=0 → output should stay at 0
    auto pid = make_pid(/*Kp=*/0.0f, /*Ki=*/0.0f, /*Kd=*/0.0f);
    auto st = make_state(/*sp=*/100.0f, /*fb=*/0.0f);

    pid.post_step(st, 0.016f);

    EXPECT_FLOAT_EQ(st.across[2], 0.0f);
}

TEST(PIDTest, ExtremeDt_ClampedToMax)
{
    // dt > 0.1s should be clamped to 0.1s
    auto pid = make_pid(/*Kp=*/0.0f, /*Ki=*/1.0f);
    auto st = make_state(10.0f, 0.0f);

    // Run with extremely large dt (simulated lag spike)
    pid.post_step(st, 10.0f);

    // integral should grow as if dt was 0.1s (clamped), not 10s
    EXPECT_FLOAT_EQ(pid.integral, 10.0f * 0.1f);  // error * safe_dt
}

TEST(PIDTest, TinyDt_ClampedToMin)
{
    // dt < 1e-6 should be clamped to 1e-6
    auto pid = make_pid(/*Kp=*/0.0f, /*Ki=*/1.0f);
    auto st = make_state(10.0f, 0.0f);

    pid.post_step(st, 1e-9f);

    // integral should grow as if dt was 1e-6
    EXPECT_FLOAT_EQ(pid.integral, 10.0f * 1e-6f);
}

TEST(PIDTest, FilterAlphaZero_NoFiltering)
{
    // alpha=0 means no filtering (d_filtered stays at 0)
    auto pid = make_pid(/*Kp=*/0.0f, /*Ki=*/0.0f, /*Kd=*/1.0f,
                        -1e9f, 1e9f, /*alpha=*/0.0f);
    auto st = make_state(0.0f, 0.0f);

    // Step error from 0 to 10
    st.across[1] = 10.0f;  // feedback = 10, error = -10
    pid.post_step(st, 0.01f);

    // With alpha=0, d_filtered should remain 0 (never updates)
    EXPECT_FLOAT_EQ(pid.d_filtered, 0.0f);
}

TEST(PIDTest, FilterAlphaOne_InstantTracking)
{
    // alpha=1 means d_filtered = d_raw instantly (no filtering)
    auto pid = make_pid(/*Kp=*/0.0f, /*Ki=*/0.0f, /*Kd=*/1.0f,
                        -1e9f, 1e9f, /*alpha=*/1.0f);
    auto st = make_state(0.0f, 0.0f);

    // First step: error = 0 → 10
    st.across[1] = 10.0f;
    pid.post_step(st, 0.01f);

    float expected_d_raw = (0.0f - 10.0f) / 0.01f;  // (error - last_error) / dt
    EXPECT_FLOAT_EQ(pid.d_filtered, expected_d_raw);
}

TEST(PIDTest, VeryLargeKd_WithSmallDt_Stability)
{
    // Large Kd + small dt can cause numerical instability
    // Test that the filter prevents explosion
    auto pid = make_pid(/*Kp=*/0.0f, /*Ki=*/0.0f, /*Kd=*/1000.0f,
                        -1e9f, 1e9f, /*alpha=*/0.1f);
    auto st = make_state(0.0f, 0.0f);

    // Add high-frequency noise
    for (int i = 0; i < 100; ++i) {
        float noise = (i % 2 == 0) ? 1.0f : -1.0f;
        st.across[1] = noise;
        pid.post_step(st, 0.001f);  // 1kHz sampling

        // Output should remain bounded (not inf/nan)
        EXPECT_FALSE(std::isinf(st.across[2]));
        EXPECT_FALSE(std::isnan(st.across[2]));
        EXPECT_LT(std::abs(st.across[2]), 1e6f);
    }
}

TEST(PIDTest, NegativeKp_InvertsControl)
{
    // Negative Kp is valid (e.g., for cooling instead of heating)
    auto pid = make_pid(/*Kp=*/-2.0f);
    auto st = make_state(/*sp=*/10.0f, /*fb=*/0.0f);

    pid.post_step(st, 0.016f);

    // output = -2 * (10 - 0) = -20
    EXPECT_FLOAT_EQ(st.across[2], -20.0f);
}

TEST(PIDTest, BidirectionalOutputLimits)
{
    // Test symmetric limits around zero
    auto pid = make_pid(/*Kp=*/10.0f, /*Ki=*/0.0f, /*Kd=*/0.0f,
                        /*out_min=*/-5.0f, /*out_max=*/5.0f);
    auto st = make_state(100.0f, 0.0f);

    pid.post_step(st, 0.016f);

    // P-only: output = 10 * 100 = 1000, should clamp to 5
    EXPECT_FLOAT_EQ(st.across[2], 5.0f);
}

TEST(PIDTest, AsymmetricOutputLimits)
{
    // Test asymmetric limits (e.g., PWM 0-100%)
    auto pid = make_pid(/*Kp=*/10.0f, /*Ki=*/0.0f, /*Kd=*/0.0f,
                        /*out_min=*/0.0f, /*out_max=*/100.0f);
    auto st_neg = make_state(10.0f, 100.0f);  // negative error
    auto st_pos = make_state(100.0f, 0.0f);   // positive error

    pid.post_step(st_neg, 0.016f);
    EXPECT_FLOAT_EQ(st_neg.across[2], 0.0f);  // Clamped to min

    pid.post_step(st_pos, 0.016f);
    EXPECT_FLOAT_EQ(st_pos.across[2], 100.0f); // Clamped to max
}

TEST(PIDTest, IntegralDecay_WhenErrorSignChanges)
{
    // Test that integral can decrease when error changes sign
    auto pid = make_pid(/*Kp=*/0.0f, /*Ki=*/1.0f);
    auto st = make_state(0.0f, 0.0f);

    // Accumulate positive integral
    st.across[0] = 10.0f;  // setpoint = 10
    for (int i = 0; i < 10; ++i) {
        pid.post_step(st, 0.016f);
    }
    float integral_after_positive = pid.integral;
    EXPECT_GT(integral_after_positive, 0.0f);

    // Reverse error direction
    st.across[0] = 0.0f;   // setpoint = 0, feedback still 0
    st.across[1] = 10.0f;  // feedback = 10, error = -10
    for (int i = 0; i < 5; ++i) {
        pid.post_step(st, 0.016f);
    }

    // Integral should have decreased
    EXPECT_LT(pid.integral, integral_after_positive);
}

TEST(PIDTest, DerivativeZeroWhenErrorConstant)
{
    // D term should be zero when error doesn't change
    auto pid = make_pid(/*Kp=*/0.0f, /*Ki=*/0.0f, /*Kd=*/10.0f,
                        -1e9f, 1e9f, /*alpha=*/0.5f);
    auto st = make_state(50.0f, 0.0f);  // error = 50

    // First step initializes last_error
    pid.post_step(st, 0.01f);
    float first_output = st.across[2];

    // Second step with same error → derivative should be ~0
    pid.post_step(st, 0.01f);
    float second_output = st.across[2];

    // D-term should decay to near zero (due to filtering)
    EXPECT_LT(std::abs(second_output), std::abs(first_output));
}

TEST(PIDTest, FullPID_StepResponse)
{
    // Classic step response test with all three terms
    auto pid = make_pid(/*Kp=*/1.0f, /*Ki=*/0.5f, /*Kd=*/0.1f,
                        /*out_min=*/-100.0f, /*out_max=*/100.0f, /*alpha=*/0.2f);
    auto st = make_state(/*sp=*/10.0f, /*fb=*/0.0f);

    // Initial step
    pid.post_step(st, 0.016f);
    float first_output = st.across[2];

    // Run for more steps
    for (int i = 0; i < 50; ++i) {
        // Feedback approaches setpoint (simple first-order system simulation)
        float feedback = st.across[2] * 0.1f;  // Simple plant model
        st.across[1] = feedback;
        pid.post_step(st, 0.016f);
    }

    // Output should be bounded
    EXPECT_GE(st.across[2], -100.0f);
    EXPECT_LE(st.across[2], 100.0f);

    // Integral should have accumulated
    EXPECT_GT(pid.integral, 0.0f);
}

TEST(PIDTest, MultipleResets_WithSameInitialState)
{
    // Test that resetting state produces consistent results
    auto pid1 = make_pid(/*Kp=*/2.0f, /*Ki=*/0.1f);
    auto pid2 = make_pid(/*Kp=*/2.0f, /*Ki=*/0.1f);

    auto st = make_state(10.0f, 0.0f);

    for (int i = 0; i < 10; ++i) {
        pid1.post_step(st, 0.016f);
    }

    for (int i = 0; i < 10; ++i) {
        pid2.post_step(st, 0.016f);
    }

    // Same parameters + same inputs = same outputs
    EXPECT_FLOAT_EQ(pid1.integral, pid2.integral);
    EXPECT_FLOAT_EQ(st.across[2], st.across[2]);
}

// =============================================================================
// P component tests
// =============================================================================

static P<JitProvider> make_p(float Kp = 1.0f, float out_min = -1000.0f, float out_max = 1000.0f)
{
    P<JitProvider> comp;
    comp.Kp = Kp;
    comp.output_min = out_min;
    comp.output_max = out_max;
    comp.provider.set(PortNames::setpoint, 0);
    comp.provider.set(PortNames::feedback, 1);
    comp.provider.set(PortNames::output,   2);
    return comp;
}

TEST(PTest, BasicProportional)
{
    auto p  = make_p(3.0f);
    auto st = make_state(10.0f, 4.0f);

    p.post_step(st, 0.016f);

    // output = 3 * (10 - 4) = 18
    EXPECT_FLOAT_EQ(st.across[2], 18.0f);
}

TEST(PTest, OutputClamped)
{
    auto p  = make_p(100.0f, -5.0f, 5.0f);
    auto st = make_state(10.0f, 0.0f);

    p.post_step(st, 0.016f);

    EXPECT_FLOAT_EQ(st.across[2], 5.0f);
}

TEST(PTest, ZeroError)
{
    auto p  = make_p(10.0f);
    auto st = make_state(7.0f, 7.0f);

    p.post_step(st, 0.016f);

    EXPECT_FLOAT_EQ(st.across[2], 0.0f);
}

TEST(PTest, SolveElectricalConductance)
{
    auto p  = make_p();
    auto st = make_state(0.0f, 0.0f);

    p.solve_electrical(st, 0.016f);

    EXPECT_GT(st.conductance[2], 0.0f);
    EXPECT_LT(st.conductance[2], 1e-4f);
}

// =============================================================================
// PD component tests
// =============================================================================

static PD<JitProvider> make_pd(float Kp = 1.0f, float Kd = 0.0f,
                                float out_min = -1000.0f, float out_max = 1000.0f,
                                float alpha = 0.2f)
{
    PD<JitProvider> comp;
    comp.Kp = Kp;
    comp.Kd = Kd;
    comp.output_min = out_min;
    comp.output_max = out_max;
    comp.filter_alpha = alpha;
    comp.provider.set(PortNames::setpoint, 0);
    comp.provider.set(PortNames::feedback, 1);
    comp.provider.set(PortNames::output,   2);
    return comp;
}

TEST(PDTest, ProportionalTerm)
{
    auto pd = make_pd(2.0f, 0.0f);
    auto st = make_state(10.0f, 0.0f);

    pd.post_step(st, 0.016f);

    EXPECT_FLOAT_EQ(st.across[2], 20.0f);
}

TEST(PDTest, DerivativeReducesOvershoot)
{
    // Error decreasing rapidly → D-term should oppose P-term
    auto pd = make_pd(1.0f, 1.0f, -1e9f, 1e9f, 1.0f);  // alpha=1: no filter lag
    auto st = make_state(10.0f, 0.0f);

    // Step 1: error = 10, last_error = 0 → d_raw = 10/dt > 0 → adds to output
    pd.post_step(st, 0.01f);
    float out1 = st.across[2];

    // Step 2: error drops to 5 (feedback approaching setpoint)
    st.across[1] = 5.0f;
    pd.post_step(st, 0.01f);
    float out2 = st.across[2];

    // D-term should be negative (error decreasing), reducing output vs P-only
    EXPECT_LT(out2, 5.0f);  // P-only would give exactly 5.0
}

TEST(PDTest, NoIntegral)
{
    // PD has no integral state — repeated same error should give same output
    auto pd = make_pd(1.0f, 0.0f);
    auto st = make_state(10.0f, 0.0f);

    pd.post_step(st, 0.016f);
    float out1 = st.across[2];

    pd.post_step(st, 0.016f);
    float out2 = st.across[2];

    // Same error, no integral → same P output (D decays to ~0 on constant error)
    EXPECT_FLOAT_EQ(out1, out2);
}

TEST(PDTest, OutputClamped)
{
    auto pd = make_pd(100.0f, 0.0f, 0.0f, 10.0f);
    auto st = make_state(100.0f, 0.0f);

    pd.post_step(st, 0.016f);

    EXPECT_FLOAT_EQ(st.across[2], 10.0f);
}

// =============================================================================
// PI component tests
// =============================================================================

static PI<JitProvider> make_pi(float Kp = 1.0f, float Ki = 0.0f,
                                float out_min = -1000.0f, float out_max = 1000.0f)
{
    PI<JitProvider> comp;
    comp.Kp = Kp;
    comp.Ki = Ki;
    comp.output_min = out_min;
    comp.output_max = out_max;
    comp.provider.set(PortNames::setpoint, 0);
    comp.provider.set(PortNames::feedback, 1);
    comp.provider.set(PortNames::output,   2);
    return comp;
}

TEST(PITest, ProportionalTerm)
{
    auto pi = make_pi(3.0f, 0.0f);
    auto st = make_state(10.0f, 0.0f);

    pi.post_step(st, 0.016f);

    EXPECT_FLOAT_EQ(st.across[2], 30.0f);
}

TEST(PITest, IntegralAccumulates)
{
    auto pi = make_pi(0.0f, 1.0f);
    auto st = make_state(5.0f, 0.0f);

    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 60; ++i) {
        pi.post_step(st, dt);
    }

    // integral ≈ 5 * 1.0s = 5
    EXPECT_NEAR(st.across[2], 5.0f, 1e-3f);
}

TEST(PITest, IntegralTimeInvariance)
{
    auto pi60  = make_pi(0.0f, 1.0f);
    auto pi144 = make_pi(0.0f, 1.0f);

    SimulationState st60  = make_state(5.0f, 0.0f);
    SimulationState st144 = make_state(5.0f, 0.0f);

    for (int i = 0; i < 60;  ++i) pi60 .post_step(st60,  1.0f / 60.0f);
    for (int i = 0; i < 144; ++i) pi144.post_step(st144, 1.0f / 144.0f);

    EXPECT_NEAR(st60.across[2], st144.across[2], 5e-3f);
}

TEST(PITest, AntiWindup)
{
    auto pi = make_pi(0.0f, 100.0f, -5.0f, 5.0f);
    auto st = make_state(100.0f, 0.0f);

    for (int i = 0; i < 1000; ++i) {
        pi.post_step(st, 0.016f);
    }

    EXPECT_LE(st.across[2], 5.0f);
    EXPECT_GE(st.across[2], -5.0f);
}

TEST(PITest, NoDerivative)
{
    // Step change in error should not cause derivative spike
    auto pi = make_pi(0.0f, 1.0f);
    auto st = make_state(0.0f, 0.0f);

    // Sudden step: error jumps from 0 to 100
    st.across[0] = 100.0f;
    pi.post_step(st, 0.01f);

    // Output = Ki * integral = 1.0 * 100 * 0.01 = 1.0 (no D spike)
    EXPECT_NEAR(st.across[2], 1.0f, 1e-4f);
}
