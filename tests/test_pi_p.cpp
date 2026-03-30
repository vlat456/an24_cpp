#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/state.h"
#include <algorithm>
#include <cmath>


// =============================================================================
// Test Helpers
// =============================================================================

template <typename Comp>
void step_component(Comp& comp, SimulationState& st, float dt) {
    comp.execute(st, dt);
    comp.commit(st, dt);
}

// ─── Helper functions ───────────────────────────────────────────────────────────

static PI<JitProvider> make_pi(float Kp = 1.0f, float Ki = 0.0f,
                               float out_min = -1000.0f, float out_max = 1000.0f)
{
    PI<JitProvider> pi;
    pi.Kp = Kp;
    pi.Ki = Ki;
    pi.output_min = out_min;
    pi.output_max = out_max;
    pi.provider.set(PortNames::setpoint, 0);
    pi.provider.set(PortNames::feedback, 1);
    pi.provider.set(PortNames::output,   2);
    return pi;
}

static P<JitProvider> make_p(float Kp = 1.0f,
                              float out_min = -1000.0f, float out_max = 1000.0f)
{
    P<JitProvider> p;
    p.Kp = Kp;
    p.output_min = out_min;
    p.output_max = out_max;
    p.provider.set(PortNames::setpoint, 0);
    p.provider.set(PortNames::feedback, 1);
    p.provider.set(PortNames::output,   2);
    return p;
}

static SimulationState make_state(float sp, float fb)
{
    SimulationState st;
    st.values.resize(3, 0.0f);
    st.values[0] = sp;
    st.values[1] = fb;
    st.values[2] = 0.0f;
    return st;
}

// ─── PI Tests: Proportional-only (Ki=0) ───────────────────────────────────────────

TEST(PITest, ProportionalOnly_Basic)
{
    auto pi = make_pi(/*Kp=*/2.0f, /*Ki=*/0.0f);
    auto st = make_state(/*sp=*/10.0f, /*fb=*/0.0f);

    step_component(pi, st, 0.016f);

    // output = Kp * (sp - fb) = 2 * 10 = 20
    EXPECT_FLOAT_EQ(st.values[2], 20.0f);
}

TEST(PITest, ProportionalOnly_NegativeError)
{
    auto pi = make_pi(/*Kp=*/1.0f, /*Ki=*/0.0f);
    auto st = make_state(/*sp=*/-5.0f, /*fb=*/5.0f);

    step_component(pi, st, 0.016f);

    // output = 1 * (-5 - 5) = -10
    EXPECT_FLOAT_EQ(st.values[2], -10.0f);
}

TEST(PITest, ProportionalOnly_ZeroError)
{
    auto pi = make_pi(/*Kp=*/5.0f, /*Ki=*/0.0f);
    auto st = make_state(/*sp=*/10.0f, /*fb=*/10.0f);

    step_component(pi, st, 0.016f);

    // output = 5 * (10 - 10) = 0
    EXPECT_FLOAT_EQ(st.values[2], 0.0f);
}

// ─── PI Tests: Integral-only (Kp=0) ───────────────────────────────────────────────

TEST(PITest, IntegralOnly_Accumulates)
{
    auto pi = make_pi(/*Kp=*/0.0f, /*Ki=*/1.0f);
    auto st = make_state(/*sp=*/10.0f, /*fb=*/0.0f);

    step_component(pi, st, 0.1f);
    EXPECT_FLOAT_EQ(pi.integral, 1.0f);  // 10 * 0.1

    step_component(pi, st, 0.1f);
    EXPECT_FLOAT_EQ(pi.integral, 2.0f);  // 10 * 0.1 + 10 * 0.1
}

TEST(PITest, IntegralOnly_DecreasesWhenErrorChangesSign)
{
    auto pi = make_pi(/*Kp=*/0.0f, /*Ki=*/1.0f);
    auto st = make_state(/*sp=*/10.0f, /*fb=*/0.0f);

    // Accumulate positive integral
    for (int i = 0; i < 10; ++i) {
        step_component(pi, st, 0.01f);
    }
    float integral_after_positive = pi.integral;
    EXPECT_GT(integral_after_positive, 0.0f);

    // Reverse error
    st.values[0] = 0.0f;   // setpoint = 0
    st.values[1] = 10.0f;  // feedback = 10, error = -10
    for (int i = 0; i < 5; ++i) {
        step_component(pi, st, 0.01f);
    }

    // Integral should have decreased
    EXPECT_LT(pi.integral, integral_after_positive);
}

// ─── PI Tests: Combined P + I ─────────────────────────────────────────────────────

TEST(PITest, ProportionalAndIntegral_StepResponse)
{
    auto pi = make_pi(/*Kp=*/1.0f, /*Ki=*/0.5f, -1e9f, 1e9f);
    auto st = make_state(/*sp=*/10.0f, /*fb=*/0.0f);

    step_component(pi, st, 0.01f);

    // P term = 1 * 10 = 10
    // I term = 0.5 * (10 * 0.01) = 0.05
    // Total = 10.05
    EXPECT_NEAR(st.values[2], 10.05f, 0.01f);
}

TEST(PITest, ProportionalAndIntegral_SteadyStateZeroError)
{
    auto pi = make_pi(/*Kp=*/2.0f, /*Ki=*/1.0f, -1e9f, 1e9f);
    auto st = make_state(/*sp=*/10.0f, /*fb=*/10.0f);  // Zero error

    step_component(pi, st, 0.01f);

    // P = 2 * 0 = 0
    // I = 1 * (0 * 0.01) = 0
    // Output = 0 (integral doesn't change when error is zero)
    EXPECT_FLOAT_EQ(st.values[2], 0.0f);
}

TEST(PITest, NegativeGains_InvertControl)
{
    auto pi = make_pi(/*Kp=*/-2.0f, /*Ki=*/-0.5f, -1e9f, 1e9f);
    auto st = make_state(/*sp=*/10.0f, /*fb=*/0.0f);

    step_component(pi, st, 0.01f);

    // P term = -2 * 10 = -20
    // I term = -0.5 * (10 * 0.01) = -0.05
    // Total = -20.05
    EXPECT_NEAR(st.values[2], -20.05f, 0.01f);
}

// ─── PI Tests: Time Invariance ────────────────────────────────────────────────────

TEST(PITest, IntegralTimeInvariance)
{
    // Same conditions at 60 Hz and 144 Hz should yield comparable integral
    auto pi60  = make_pi(/*Kp=*/0.0f, /*Ki=*/1.0f);
    auto pi144 = make_pi(/*Kp=*/0.0f, /*Ki=*/1.0f);

    auto st60  = make_state(5.0f, 0.0f);
    auto st144 = make_state(5.0f, 0.0f);

    for (int i = 0; i < 60;  ++i) step_component(pi60, st60,  1.0f / 60.0f);
    for (int i = 0; i < 144; ++i) step_component(pi144, st144, 1.0f / 144.0f);

    // After 1 second, integrals should match
    EXPECT_NEAR(st60.values[2], st144.values[2], 1e-3f);
}

// ─── PI Tests: Anti-Windup ────────────────────────────────────────────────────────

TEST(PITest, AntiWindupCapsOutput)
{
    // Large error, large Ki, tight output cap
    auto pi = make_pi(/*Kp=*/1.0f, /*Ki=*/10.0f, /*out_min=*/-10.0f, /*out_max=*/10.0f);
    auto st = make_state(/*sp=*/100.0f, /*fb=*/0.0f);

    for (int i = 0; i < 200; ++i) {
        step_component(pi, st, 0.016f);
    }

    EXPECT_LE(st.values[2], 10.0f);
    EXPECT_GE(st.values[2], -10.0f);
}

TEST(PITest, AntiWindupIntegralClamped)
{
    // Integral contribution alone must not push output beyond [min, max]
    auto pi = make_pi(/*Kp=*/0.0f, /*Ki=*/100.0f, /*out_min=*/-5.0f, /*out_max=*/5.0f);
    auto st = make_state(10.0f, 0.0f);

    for (int i = 0; i < 1000; ++i) {
        step_component(pi, st, 0.016f);
    }

    EXPECT_LE(st.values[2], 5.0f);
    EXPECT_GE(st.values[2], -5.0f);
}

// ─── PI Tests: Edge Cases ────────────────────────────────────────────────────────

TEST(PITest, ZeroGains_ZeroOutput)
{
    auto pi = make_pi(/*Kp=*/0.0f, /*Ki=*/0.0f);
    auto st = make_state(/*sp=*/100.0f, /*fb=*/0.0f);

    step_component(pi, st, 0.016f);

    EXPECT_FLOAT_EQ(st.values[2], 0.0f);
}

TEST(PITest, ExtremeDt_ClampedToMax)
{
    auto pi = make_pi(/*Kp=*/0.0f, /*Ki=*/1.0f);
    auto st = make_state(10.0f, 0.0f);

    // Run with extremely large dt
    step_component(pi, st, 10.0f);

    // integral should grow as if dt was 0.1s (clamped), not 10s
    EXPECT_FLOAT_EQ(pi.integral, 10.0f * 0.1f);
}

TEST(PITest, TinyDt_ClampedToMin)
{
    auto pi = make_pi(/*Kp=*/0.0f, /*Ki=*/1.0f);
    auto st = make_state(10.0f, 0.0f);

    step_component(pi, st, 1e-9f);

    // integral should grow as if dt was 1e-6
    EXPECT_FLOAT_EQ(pi.integral, 10.0f * 1e-6f);
}

TEST(PITest, OutputSaturation_PositiveClamp)
{
    auto pi = make_pi(/*Kp=*/10.0f, /*Ki=*/0.0f, /*out_min=*/-5.0f, /*out_max=*/5.0f);
    auto st = make_state(/*sp=*/100.0f, /*fb=*/0.0f);

    step_component(pi, st, 0.016f);

    // P-only: output = 10 * 100 = 1000, should clamp to 5
    EXPECT_FLOAT_EQ(st.values[2], 5.0f);
}

TEST(PITest, OutputSaturation_NegativeClamp)
{
    auto pi = make_pi(/*Kp=*/10.0f, /*Ki=*/0.0f, /*out_min=*/-5.0f, /*out_max=*/5.0f);
    auto st = make_state(/*sp=*/0.0f, /*fb=*/100.0f);

    step_component(pi, st, 0.016f);

    // P-only: output = 10 * (-100) = -1000, should clamp to -5
    EXPECT_FLOAT_EQ(st.values[2], -5.0f);
}

TEST(PITest, MultipleInstances_IndependentState)
{
    auto pi1 = make_pi(/*Kp=*/2.0f, /*Ki=*/0.1f);
    auto pi2 = make_pi(/*Kp=*/2.0f, /*Ki=*/0.1f);

    auto st1 = make_state(10.0f, 0.0f);
    auto st2 = make_state(5.0f, 0.0f);

    for (int i = 0; i < 10; ++i) {
        step_component(pi1, st1, 0.016f);
        step_component(pi2, st2, 0.016f);
    }

    // States should be different (different errors)
    EXPECT_NE(pi1.integral, pi2.integral);
}

TEST(PITest, ComparedToPID_NoDerivative)
{
    // PI should behave like PID with Kd=0
    auto pi = make_pi(/*Kp=*/1.0f, /*Ki=*/0.5f, -1e9f, 1e9f);

    PID<JitProvider> pid;
    pid.Kp = 1.0f;
    pid.Ki = 0.5f;
    pid.Kd = 0.0f;
    pid.output_min = -1e9f;
    pid.output_max = 1e9f;
    pid.filter_alpha = 0.2f;
    pid.provider.set(PortNames::setpoint, 0);
    pid.provider.set(PortNames::feedback, 1);
    pid.provider.set(PortNames::output, 2);

    auto st_pi  = make_state(10.0f, 0.0f);
    auto st_pid = make_state(10.0f, 0.0f);

    step_component(pi, st_pi, 0.01f);
    step_component(pid, st_pid, 0.01f);

    // Should produce identical output
    EXPECT_FLOAT_EQ(st_pi.values[2], st_pid.values[2]);
}

// ─── P Tests: Basic Proportional Control ───────────────────────────────────────────

TEST(PTest, ProportionalOnly_Basic)
{
    auto p = make_p(/*Kp=*/2.0f);
    auto st = make_state(/*sp=*/10.0f, /*fb=*/0.0f);

    step_component(p, st, 0.016f);

    // output = Kp * (sp - fb) = 2 * 10 = 20
    EXPECT_FLOAT_EQ(st.values[2], 20.0f);
}

TEST(PTest, ProportionalOnly_NegativeError)
{
    auto p = make_p(/*Kp=*/1.0f);
    auto st = make_state(/*sp=*/-5.0f, /*fb=*/5.0f);

    step_component(p, st, 0.016f);

    // output = 1 * (-5 - 5) = -10
    EXPECT_FLOAT_EQ(st.values[2], -10.0f);
}

TEST(PTest, ProportionalOnly_ZeroError)
{
    auto p = make_p(/*Kp=*/5.0f);
    auto st = make_state(/*sp=*/10.0f, /*fb=*/10.0f);

    step_component(p, st, 0.016f);

    // output = 5 * (10 - 10) = 0
    EXPECT_FLOAT_EQ(st.values[2], 0.0f);
}

TEST(PTest, HighGain)
{
    auto p = make_p(/*Kp=*/100.0f);
    auto st = make_state(/*sp=*/1.0f, /*fb=*/0.0f);

    step_component(p, st, 0.016f);

    // output = 100 * 1 = 100
    EXPECT_FLOAT_EQ(st.values[2], 100.0f);
}

// ─── P Tests: Edge Cases ──────────────────────────────────────────────────────────

TEST(PTest, ZeroGain_ZeroOutput)
{
    auto p = make_p(/*Kp=*/0.0f);
    auto st = make_state(/*sp=*/100.0f, /*fb=*/0.0f);

    step_component(p, st, 0.016f);

    EXPECT_FLOAT_EQ(st.values[2], 0.0f);
}

TEST(PTest, NegativeGain_InvertsControl)
{
    auto p = make_p(/*Kp=*/-2.0f);
    auto st = make_state(/*sp=*/10.0f, /*fb=*/0.0f);

    step_component(p, st, 0.016f);

    // output = -2 * 10 = -20
    EXPECT_FLOAT_EQ(st.values[2], -20.0f);
}

TEST(PTest, OutputSaturation_PositiveClamp)
{
    auto p = make_p(/*Kp=*/10.0f, /*out_min=*/-5.0f, /*out_max=*/5.0f);
    auto st = make_state(/*sp=*/100.0f, /*fb=*/0.0f);

    step_component(p, st, 0.016f);

    // output = 10 * 100 = 1000, should clamp to 5
    EXPECT_FLOAT_EQ(st.values[2], 5.0f);
}

TEST(PTest, OutputSaturation_NegativeClamp)
{
    auto p = make_p(/*Kp=*/10.0f, /*out_min=*/-5.0f, /*out_max=*/5.0f);
    auto st = make_state(/*sp=*/0.0f, /*fb=*/100.0f);

    step_component(p, st, 0.016f);

    // output = 10 * (-100) = -1000, should clamp to -5
    EXPECT_FLOAT_EQ(st.values[2], -5.0f);
}

TEST(PTest, OutputSaturation_AsymmetricLimits)
{
    // PWM-style limits (0-100%)
    auto p = make_p(/*Kp=*/10.0f, /*out_min=*/0.0f, /*out_max=*/100.0f);
    auto st_neg = make_state(/*sp=*/0.0f, /*fb=*/10.0f);   // negative error
    auto st_pos = make_state(/*sp=*/10.0f, /*fb=*/0.0f);   // positive error

    step_component(p, st_neg, 0.016f);
    EXPECT_FLOAT_EQ(st_neg.values[2], 0.0f);  // Clamped to min

    step_component(p, st_pos, 0.016f);
    EXPECT_FLOAT_EQ(st_pos.values[2], 100.0f); // Clamped to max
}

// ─── P Tests: Time Independence ───────────────────────────────────────────────────

TEST(PTest, DtDoesNotAffectOutput)
{
    // P controller output should be independent of dt
    auto p = make_p(/*Kp=*/2.0f);

    auto st1 = make_state(10.0f, 0.0f);
    auto st2 = make_state(10.0f, 0.0f);

    step_component(p, st1, 0.001f);  // 1kHz
    step_component(p, st2, 0.1f);    // 10Hz

    // Same output regardless of dt
    EXPECT_FLOAT_EQ(st1.values[2], st2.values[2]);
}

// ─── P Tests: Memoryless Property ────────────────────────────────────────────────

TEST(PTest, Memoryless_NoStateAccumulation)
{
    // P controller has no state - output depends only on current error
    auto p = make_p(/*Kp=*/2.0f);
    auto st = make_state(10.0f, 0.0f);

    // First step
    step_component(p, st, 0.01f);
    float output1 = st.values[2];

    // Same error again
    step_component(p, st, 0.01f);
    float output2 = st.values[2];

    // Output should be identical (no memory)
    EXPECT_FLOAT_EQ(output1, output2);
}

TEST(PTest, Memoryless_ErrorChangeImmediate)
{
    auto p = make_p(/*Kp=*/1.0f);
    auto st = make_state(10.0f, 0.0f);

    // Error = 10
    step_component(p, st, 0.01f);
    EXPECT_FLOAT_EQ(st.values[2], 10.0f);

    // Error changes to 5 (feedback catches up)
    st.values[1] = 5.0f;
    step_component(p, st, 0.01f);
    EXPECT_FLOAT_EQ(st.values[2], 5.0f);

    // Error changes to -5 (overshoot)
    st.values[1] = 15.0f;
    step_component(p, st, 0.01f);
    EXPECT_FLOAT_EQ(st.values[2], -5.0f);
}

// ─── P Tests: Comparison with Other Controllers ───────────────────────────────────

TEST(PTest, ComparedToPI_KiZero)
{
    // P should behave like PI with Ki=0
    auto p = make_p(/*Kp=*/2.0f, -1e9f, 1e9f);

    PI<JitProvider> pi;
    pi.Kp = 2.0f;
    pi.Ki = 0.0f;
    pi.output_min = -1e9f;
    pi.output_max = 1e9f;
    pi.provider.set(PortNames::setpoint, 0);
    pi.provider.set(PortNames::feedback, 1);
    pi.provider.set(PortNames::output, 2);

    auto st_p  = make_state(10.0f, 0.0f);
    auto st_pi = make_state(10.0f, 0.0f);

    step_component(p, st_p, 0.01f);
    step_component(pi, st_pi, 0.01f);

    // Should produce identical output
    EXPECT_FLOAT_EQ(st_p.values[2], st_pi.values[2]);
}

TEST(PTest, ComparedToPID_KiKdZero)
{
    // P should behave like PID with Ki=0, Kd=0
    auto p = make_p(/*Kp=*/2.0f, -1e9f, 1e9f);

    PID<JitProvider> pid;
    pid.Kp = 2.0f;
    pid.Ki = 0.0f;
    pid.Kd = 0.0f;
    pid.output_min = -1e9f;
    pid.output_max = 1e9f;
    pid.filter_alpha = 0.2f;
    pid.provider.set(PortNames::setpoint, 0);
    pid.provider.set(PortNames::feedback, 1);
    pid.provider.set(PortNames::output, 2);

    auto st_p   = make_state(10.0f, 0.0f);
    auto st_pid = make_state(10.0f, 0.0f);

    step_component(p, st_p, 0.01f);
    step_component(pid, st_pid, 0.01f);

    // Should produce identical output
    EXPECT_FLOAT_EQ(st_p.values[2], st_pid.values[2]);
}
