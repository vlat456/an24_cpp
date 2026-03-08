#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/state.h"
#include <algorithm>
#include <cmath>

using namespace an24;

// Helper: build JIT PD with ports wired to: [0]=setpoint, [1]=feedback, [2]=output
static PD<JitProvider> make_pd(float Kp = 1.0f, float Kd = 0.0f,
                               float out_min = -1000.0f, float out_max = 1000.0f,
                               float alpha = 0.2f)
{
    PD<JitProvider> pd;
    pd.Kp = Kp;
    pd.Kd = Kd;
    pd.output_min = out_min;
    pd.output_max = out_max;
    pd.filter_alpha = alpha;
    pd.provider.set(PortNames::setpoint, 0);
    pd.provider.set(PortNames::feedback, 1);
    pd.provider.set(PortNames::output,   2);
    return pd;
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

// ─── P only tests ────────────────────────────────────────────────────────────

TEST(PDTest, ProportionalOnly_Basic)
{
    auto pd = make_pd(/*Kp=*/2.0f);
    auto st = make_state(/*sp=*/10.0f, /*fb=*/0.0f);

    pd.post_step(st, 0.016f);

    // output = Kp * (sp - fb) = 2 * 10 = 20
    EXPECT_FLOAT_EQ(st.across[2], 20.0f);
}

TEST(PDTest, ProportionalOnly_NegativeError)
{
    auto pd = make_pd(/*Kp=*/1.0f);
    auto st = make_state(/*sp=*/-5.0f, /*fb=*/5.0f);

    pd.post_step(st, 0.016f);

    // output = 1 * (-5 - 5) = -10
    EXPECT_FLOAT_EQ(st.across[2], -10.0f);
}

TEST(PDTest, ProportionalOnly_ZeroError)
{
    auto pd = make_pd(/*Kp=*/5.0f);
    auto st = make_state(/*sp=*/10.0f, /*fb=*/10.0f);

    pd.post_step(st, 0.016f);

    // output = 5 * (10 - 10) = 0
    EXPECT_FLOAT_EQ(st.across[2], 0.0f);
}

TEST(PDTest, ProportionalOnly_HighGain)
{
    auto pd = make_pd(/*Kp=*/100.0f);
    auto st = make_state(/*sp=*/1.0f, /*fb=*/0.0f);

    pd.post_step(st, 0.016f);

    // output = 100 * 1 = 100
    EXPECT_FLOAT_EQ(st.across[2], 100.0f);
}

// ─── D only tests ────────────────────────────────────────────────────────────

TEST(PDTest, DerivativeOnly_PositiveRateOfChange)
{
    auto pd = make_pd(/*Kp=*/0.0f, /*Kd=*/1.0f, -1e9f, 1e9f, /*alpha=*/1.0f);
    auto st = make_state(/*sp=*/0.0f, /*fb=*/0.0f);

    // First step: error = 0
    pd.post_step(st, 0.01f);

    // Second step: error becomes 10 (feedback drops to -10)
    st.across[1] = -10.0f;
    pd.post_step(st, 0.01f);

    // d_raw = (10 - 0) / 0.01 = 1000
    // With alpha=1.0, d_filtered = d_raw
    // output = Kd * d_filtered = 1 * 1000 = 1000
    EXPECT_NEAR(st.across[2], 1000.0f, 1.0f);
}

TEST(PDTest, DerivativeOnly_NegativeRateOfChange)
{
    auto pd = make_pd(/*Kp=*/0.0f, /*Kd=*/1.0f, -1e9f, 1e9f, /*alpha=*/1.0f);
    auto st = make_state(/*sp=*/0.0f, /*fb=*/0.0f);

    // First step: error = 0
    pd.post_step(st, 0.01f);

    // Second step: error becomes -10 (feedback rises to 10)
    st.across[1] = 10.0f;
    pd.post_step(st, 0.01f);

    // d_raw = (-10 - 0) / 0.01 = -1000
    EXPECT_NEAR(st.across[2], -1000.0f, 1.0f);
}

TEST(PDTest, DerivativeOnly_ConstantError)
{
    auto pd = make_pd(/*Kp=*/0.0f, /*Kd=*/10.0f, -1e9f, 1e9f, /*alpha=*/0.5f);
    auto st = make_state(/*sp=*/50.0f, /*fb=*/0.0f);  // error = 50

    // First step initializes last_error
    pd.post_step(st, 0.01f);
    float first_output = st.across[2];

    // Second step with same error (derivative = 0)
    pd.post_step(st, 0.01f);
    float second_output = st.across[2];

    // D-term should decay (LERP: d_filtered += alpha * (0 - d_filtered))
    // With alpha=0.5: d_filtered = d_filtered * 0.5
    EXPECT_LT(std::abs(second_output), std::abs(first_output));
    // After one decay step with alpha=0.5, should be approximately half
    EXPECT_NEAR(second_output, first_output * 0.5f, first_output * 0.1f);
}

TEST(PDTest, DerivativeFilterReducesNoise)
{
    // Kd=1, alpha=0.2 — filtered amplitude must be < raw d amplitude
    auto pd = make_pd(/*Kp=*/0.0f, /*Kd=*/1.0f, -1e9f, 1e9f, /*alpha=*/0.2f);

    const float dt = 0.01f;
    float max_filtered = 0.0f;
    float max_raw      = 0.0f;

    auto st = make_state(0.0f, 0.0f);

    // High-frequency noise: alternate +1 / -1 every step
    for (int i = 0; i < 100; ++i) {
        float noise = (i % 2 == 0) ? 1.0f : -1.0f;
        st.across[1] = noise;   // feedback with noise
        pd.post_step(st, dt);

        // Calculate raw derivative for comparison
        float prev_noise = (i > 0) ? ((i - 1) % 2 == 0 ? 1.0f : -1.0f) : 0.0f;
        float raw = std::abs((noise - prev_noise) / dt);
        max_raw      = std::max(max_raw, raw);
        max_filtered = std::max(max_filtered, std::abs(st.across[2]));
    }

    EXPECT_LT(max_filtered, max_raw);
}

// ─── Combined P + D tests ──────────────────────────────────────────────────────

TEST(PDTest, ProportionalAndDerivative_StepResponse)
{
    auto pd = make_pd(/*Kp=*/1.0f, /*Kd=*/0.1f, -1e9f, 1e9f, /*alpha=*/0.5f);
    auto st = make_state(/*sp=*/10.0f, /*fb=*/0.0f);

    pd.post_step(st, 0.01f);

    // P term = 1 * 10 = 10
    // D term reacts to initial step (error goes 0→10)
    // Output should be > 10 (P + D kick)
    EXPECT_GT(st.across[2], 10.0f);
}

TEST(PDTest, ProportionalAndDerivative_SteadyState)
{
    auto pd = make_pd(/*Kp=*/2.0f, /*Kd=*/0.5f, -1e9f, 1e9f, /*alpha=*/0.3f);
    auto st = make_state(/*sp=*/5.0f, /*fb=*/0.0f);

    // First step
    pd.post_step(st, 0.01f);

    // Run until steady (error constant)
    for (int i = 0; i < 50; ++i) {
        pd.post_step(st, 0.01f);
    }

    // At steady state, D term → 0, only P term remains
    // output ≈ Kp * error = 2 * 5 = 10
    EXPECT_NEAR(st.across[2], 10.0f, 1.0f);
}

TEST(PDTest, NegativeGains_InvertControl)
{
    // Negative gains are valid (e.g., for cooling)
    auto pd = make_pd(/*Kp=*/-2.0f, /*Kd=*/-0.5f);
    auto st = make_state(/*sp=*/10.0f, /*fb=*/0.0f);

    pd.post_step(st, 0.01f);

    // P term = -2 * 10 = -20 (negative)
    // D term adds negative kick (error increasing)
    EXPECT_LT(st.across[2], -20.0f);
}

// ─── Output saturation tests ───────────────────────────────────────────────────

TEST(PDTest, OutputSaturation_PositiveClamp)
{
    auto pd = make_pd(/*Kp=*/10.0f, /*Kd=*/0.0f,
                      /*out_min=*/-5.0f, /*out_max=*/5.0f);
    auto st = make_state(/*sp=*/100.0f, /*fb=*/0.0f);

    pd.post_step(st, 0.016f);

    // P-only: output = 10 * 100 = 1000, should clamp to 5
    EXPECT_FLOAT_EQ(st.across[2], 5.0f);
}

TEST(PDTest, OutputSaturation_NegativeClamp)
{
    auto pd = make_pd(/*Kp=*/10.0f, /*Kd=*/0.0f,
                      /*out_min=*/-5.0f, /*out_max=*/5.0f);
    auto st = make_state(/*sp=*/0.0f, /*fb=*/100.0f);

    pd.post_step(st, 0.016f);

    // P-only: output = 10 * (-100) = -1000, should clamp to -5
    EXPECT_FLOAT_EQ(st.across[2], -5.0f);
}

TEST(PDTest, OutputSaturation_AsymmetricLimits)
{
    // PWM-style limits (0-100%)
    auto pd = make_pd(/*Kp=*/10.0f, /*Kd=*/0.0f,
                      /*out_min=*/0.0f, /*out_max=*/100.0f);
    auto st_neg = make_state(/*sp=*/0.0f, /*fb=*/10.0f);   // negative error
    auto st_pos = make_state(/*sp=*/10.0f, /*fb=*/0.0f);   // positive error

    pd.post_step(st_neg, 0.016f);
    EXPECT_FLOAT_EQ(st_neg.across[2], 0.0f);  // Clamped to min

    pd.post_step(st_pos, 0.016f);
    EXPECT_FLOAT_EQ(st_pos.across[2], 100.0f); // Clamped to max
}

TEST(PDTest, OutputSaturation_WithDerivativeKick)
{
    // D term can push output beyond limits
    auto pd = make_pd(/*Kp=*/1.0f, /*Kd=*/100.0f,
                      /*out_min=*/-10.0f, /*out_max=*/10.0f, /*alpha=*/1.0f);
    auto st = make_state(/*sp=*/0.0f, /*fb=*/0.0f);

    // Initialize
    pd.post_step(st, 0.001f);

    // Large error step (should cause large D kick)
    st.across[1] = -10.0f;  // error = 10
    pd.post_step(st, 0.001f);

    // Should be clamped despite large D term
    EXPECT_LE(st.across[2], 10.0f);
    EXPECT_GE(st.across[2], -10.0f);
}

// ─── Time invariance tests ─────────────────────────────────────────────────────

TEST(PDTest, TimeInvariance_DifferentDtSameOutput)
{
    // PD at different frame rates should produce similar output
    auto pd60  = make_pd(/*Kp=*/1.0f, /*Kd=*/0.1f, -1e9f, 1e9f, /*alpha=*/0.5f);
    auto pd144 = make_pd(/*Kp=*/1.0f, /*Kd=*/0.1f, -1e9f, 1e9f, /*alpha=*/0.5f);

    auto st60  = make_state(10.0f, 0.0f);
    auto st144 = make_state(10.0f, 0.0f);

    // Run at 60Hz for 0.1s
    for (int i = 0; i < 6; ++i) {
        pd60.post_step(st60, 1.0f / 60.0f);
    }

    // Run at 144Hz for 0.1s
    for (int i = 0; i < 14; ++i) {
        pd144.post_step(st144, 1.0f / 144.0f);
    }

    // Outputs should be close (P term identical, D term similar)
    EXPECT_NEAR(st60.across[2], st144.across[2], 2.0f);
}

// ─── Edge cases ────────────────────────────────────────────────────────────────

TEST(PDTest, ZeroGains_ZeroOutput)
{
    // Kp=Kd=0 → output should stay at 0
    auto pd = make_pd(/*Kp=*/0.0f, /*Kd=*/0.0f);
    auto st = make_state(/*sp=*/100.0f, /*fb=*/0.0f);

    pd.post_step(st, 0.016f);

    EXPECT_FLOAT_EQ(st.across[2], 0.0f);
}

TEST(PDTest, ExtremeDt_ClampedToMax)
{
    // dt > 0.1s should be clamped
    auto pd = make_pd(/*Kp=*/0.0f, /*Kd=*/1.0f, -1e9f, 1e9f, /*alpha=*/1.0f);
    auto st = make_state(10.0f, 10.0f);  // Initial: error = 0

    // Initialize (sets last_error = 0)
    pd.post_step(st, 0.01f);

    // Large error change with extreme dt
    st.across[1] = 0.0f;  // error changes from 0 to 10
    pd.post_step(st, 10.0f);

    // D term calculated with clamped dt=0.1, not 10.0
    float expected_d = (10.0f - 0.0f) / 0.1f;  // using safe_dt
    EXPECT_NEAR(st.across[2], expected_d, 1.0f);
}

TEST(PDTest, TinyDt_ClampedToMin)
{
    // dt < 1e-6 should be clamped
    auto pd = make_pd(/*Kp=*/0.0f, /*Kd=*/1.0f, -1e9f, 1e9f, /*alpha=*/1.0f);
    auto st = make_state(10.0f, 0.0f);

    pd.post_step(st, 1e-9f);

    // Should not crash or produce inf/nan
    EXPECT_FALSE(std::isinf(st.across[2]));
    EXPECT_FALSE(std::isnan(st.across[2]));
}

TEST(PDTest, FilterAlphaZero_NoFiltering)
{
    // alpha=0 means d_filtered stays at 0
    auto pd = make_pd(/*Kp=*/0.0f, /*Kd=*/1.0f, -1e9f, 1e9f, /*alpha=*/0.0f);
    auto st = make_state(0.0f, 0.0f);

    pd.post_step(st, 0.01f);
    EXPECT_FLOAT_EQ(pd.d_filtered, 0.0f);

    // Even with error change
    st.across[1] = 10.0f;
    pd.post_step(st, 0.01f);
    EXPECT_FLOAT_EQ(pd.d_filtered, 0.0f);
}

TEST(PDTest, FilterAlphaOne_InstantTracking)
{
    // alpha=1 means d_filtered = d_raw instantly
    auto pd = make_pd(/*Kp=*/0.0f, /*Kd=*/1.0f, -1e9f, 1e9f, /*alpha=*/1.0f);
    auto st = make_state(10.0f, 10.0f);  // Initial: error = 0

    pd.post_step(st, 0.01f);

    // Step error: setpoint=10, feedback=0, so error = 10
    st.across[1] = 0.0f;
    pd.post_step(st, 0.01f);

    // error changed from 0 to 10, so d_raw = (10 - 0) / 0.01 = 1000
    float expected_d_raw = (10.0f - 0.0f) / 0.01f;
    EXPECT_FLOAT_EQ(pd.d_filtered, expected_d_raw);
}

TEST(PDTest, VeryLargeKd_WithSmallDt_Stability)
{
    // Large Kd + small dt can cause instability
    // Test that filter prevents explosion
    auto pd = make_pd(/*Kp=*/0.0f, /*Kd=*/1000.0f, -1e9f, 1e9f, /*alpha=*/0.1f);
    auto st = make_state(0.0f, 0.0f);

    for (int i = 0; i < 100; ++i) {
        float noise = (i % 2 == 0) ? 1.0f : -1.0f;
        st.across[1] = noise;
        pd.post_step(st, 0.001f);

        EXPECT_FALSE(std::isinf(st.across[2]));
        EXPECT_FALSE(std::isnan(st.across[2]));
        EXPECT_LT(std::abs(st.across[2]), 1e6f);
    }
}

TEST(PDTest, MultipleInstances_IndependentState)
{
    // Each PD should maintain independent state
    auto pd1 = make_pd(/*Kp=*/2.0f, /*Kd=*/0.1f);
    auto pd2 = make_pd(/*Kp=*/2.0f, /*Kd=*/0.1f);

    auto st1 = make_state(10.0f, 0.0f);
    auto st2 = make_state(5.0f, 0.0f);

    for (int i = 0; i < 10; ++i) {
        pd1.post_step(st1, 0.016f);
        pd2.post_step(st2, 0.016f);
    }

    // States should be different (different errors)
    EXPECT_NE(pd1.last_error, pd2.last_error);
    EXPECT_NE(pd1.d_filtered, pd2.d_filtered);
}

// ─── solve_electrical test ─────────────────────────────────────────────────────

TEST(PDTest, SolveElectricalStampsConductance)
{
    auto pd = make_pd();
    auto st = make_state(0.0f, 0.0f);

    pd.solve_electrical(st, 0.016f);

    // Output node gets a small conductance to ground (high-impedance stamp)
    EXPECT_GT(st.conductance[2], 0.0f);
    EXPECT_LT(st.conductance[2], 1e-4f);
}

// ─── PD vs PID comparison ──────────────────────────────────────────────────────

TEST(PDTest, ComparedToPID_NoIntegral)
{
    // PD should behave like PID with Ki=0
    auto pd = make_pd(/*Kp=*/1.0f, /*Kd=*/0.1f, -1e9f, 1e9f, /*alpha=*/0.5f);

    // Create PID manually for comparison
    PID<JitProvider> pid;
    pid.Kp = 1.0f;
    pid.Ki = 0.0f;
    pid.Kd = 0.1f;
    pid.output_min = -1e9f;
    pid.output_max = 1e9f;
    pid.filter_alpha = 0.5f;
    pid.provider.set(PortNames::setpoint, 0);
    pid.provider.set(PortNames::feedback, 1);
    pid.provider.set(PortNames::output, 2);

    auto st_pd  = make_state(10.0f, 0.0f);
    auto st_pid = make_state(10.0f, 0.0f);

    pd.post_step(st_pd, 0.01f);
    pid.post_step(st_pid, 0.01f);

    // Should produce identical output
    EXPECT_FLOAT_EQ(st_pd.across[2], st_pid.across[2]);
}
