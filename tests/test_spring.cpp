#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/state.h"


// =============================================================================
// Test Helpers
// =============================================================================

template <typename Comp>
void step_component(Comp& comp, SimulationState& st, double dt) {
    comp.execute(st, dt);
    comp.commit(st, dt);
}

static Spring<JitProvider> make_spring(float k = 1000.0f, float c = 10.0f, float rest_length = 0.1f, bool compression_only = true)
{
    Spring<JitProvider> comp;
    comp.k = k;
    comp.c = c;
    comp.rest_length = rest_length;
    comp.compression_only = compression_only;
    comp.provider.set(PortNames::pos_a, 0);
    comp.provider.set(PortNames::pos_b, 1);
    comp.provider.set(PortNames::force_out, 2);
    return comp;
}

static SimulationState make_state_spring(float pos_a, float pos_b)
{
    SimulationState st;
    st.values.resize(3, 0.0f);
    st.values[0] = pos_a;
    st.values[1] = pos_b;
    st.values[2] = 0.0f;
    return st;
}

// =============================================================================
// Spring Tests
// =============================================================================

TEST(SpringTest, AtRestLength_ZeroForce)
{
    auto comp = make_spring(1000.0f, 10.0f, 0.1f, true);
    auto st = make_state_spring(0.1f, 0.0f);  // pos_a - pos_b = 0.1 = rest_length

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[2], 0.0f);
}

TEST(SpringTest, Compression_GeneratesForce)
{
    // Spring compressed by 0.05m: force = k * delta_x = 1000 * 0.05 = 50N
    auto comp = make_spring(1000.0f, 10.0f, 0.1f, true);
    auto st = make_state_spring(0.05f, 0.0f);  // delta = 0.05 - 0.1 = -0.05 (compressed)

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[2], 50.0f);
}

TEST(SpringTest, CompressionOnly_Stretching_NoForce)
{
    // compression_only = true, so stretching should not generate force
    auto comp = make_spring(1000.0f, 10.0f, 0.1f, true);
    auto st = make_state_spring(0.2f, 0.0f);  // delta = 0.2 - 0.1 = 0.1 (stretched)

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[2], 0.0f);  // No force in compression-only mode
}

TEST(SpringTest, NotCompressionOnly_Stretching_GeneratesForce)
{
    // compression_only = false, so stretching should generate force
    auto comp = make_spring(1000.0f, 10.0f, 0.1f, false);
    auto st = make_state_spring(0.2f, 0.0f);  // delta = 0.2 - 0.1 = 0.1 (stretched)

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[2], 100.0f);  // Force = 1000 * 0.1 = 100N
}

TEST(SpringTest, NotCompressionOnly_Compression_GeneratesForce)
{
    // compression_only = false, compression should still work
    auto comp = make_spring(1000.0f, 10.0f, 0.1f, false);
    auto st = make_state_spring(0.05f, 0.0f);  // delta = -0.05 (compressed)

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[2], 50.0f);
}

TEST(SpringTest, DifferentStiffness_ForceScales)
{
    // k = 5000 N/m, compressed by 0.02m: force = 5000 * 0.02 = 100N
    auto comp = make_spring(5000.0f, 10.0f, 0.1f, true);
    auto st = make_state_spring(0.08f, 0.0f);  // delta = 0.08 - 0.1 = -0.02

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[2], 100.0f);
}

TEST(SpringTest, DifferentRestLength_Comparison)
{
    // rest_length = 0.2m, at 0.15m: delta = 0.15 - 0.2 = -0.05 (compressed)
    // force = 1000 * 0.05 = 50N
    auto comp = make_spring(1000.0f, 10.0f, 0.2f, true);
    auto st = make_state_spring(0.15f, 0.0f);

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[2], 50.0f);
}

TEST(SpringTest, ZeroRestLength_Compression)
{
    // rest_length = 0, compression when pos_a < pos_b
    // pos_a = 0.0, pos_b = 0.05: delta = 0.0 - 0.05 - 0.0 = -0.05 (compressed)
    auto comp = make_spring(1000.0f, 10.0f, 0.0f, true);
    auto st = make_state_spring(0.0f, 0.05f);

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[2], 50.0f);  // 1000 * abs(-0.05) = 50N
}

TEST(SpringTest, LargeCompression_LargeForce)
{
    // k = 10000 N/m, compressed by 0.1m: force = 10000 * 0.1 = 1000N
    auto comp = make_spring(10000.0f, 10.0f, 0.1f, true);
    auto st = make_state_spring(0.0f, 0.0f);  // delta = 0 - 0.1 = -0.1

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[2], 1000.0f);
}

TEST(SpringTest, NegativePositions_Compression)
{
    // Both positions negative: pos_a = -0.05, pos_b = 0.0
    // delta = -0.05 - 0.0 - 0.1 = -0.15 (compressed)
    auto comp = make_spring(1000.0f, 10.0f, 0.1f, true);
    auto st = make_state_spring(-0.05f, 0.0f);

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[2], 150.0f);
}

TEST(SpringTest, RealWorld_RUG82_Governor)
{
    // RUG-82 carbon governor spring
    // Stiff spring, compression only, short rest length
    auto comp = make_spring(50000.0f, 100.0f, 0.05f, true);
    auto st = make_state_spring(0.04f, 0.0f);  // delta = 0.04 - 0.05 = -0.01

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[2], 500.0f);  // 50000 * 0.01 = 500N
}

TEST(SpringTest, RealWorld_SuspensionSpring)
{
    // Aircraft landing gear suspension
    // Softer spring, works both ways
    auto comp = make_spring(50000.0f, 500.0f, 0.3f, false);
    auto st = make_state_spring(0.25f, 0.0f);  // delta = 0.25 - 0.3 = -0.05 (compression)

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[2], 2500.0f);  // 50000 * 0.05 = 2500N
}

TEST(SpringTest, RealWorld_ValveSpring)
{
    // Small valve spring, compression only
    auto comp = make_spring(2000.0f, 5.0f, 0.02f, true);
    auto st = make_state_spring(0.015f, 0.0f);  // delta = 0.015 - 0.02 = -0.005

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[2], 10.0f);  // 2000 * 0.005 = 10N
}

TEST(SpringTest, Branchless_CompressionOnly_True)
{
    // Verify branchless behavior: force should be 0 for stretching
    auto comp = make_spring(1000.0f, 10.0f, 0.1f, true);

    // Compression - should produce force
    auto st1 = make_state_spring(0.05f, 0.0f);
    step_component(comp, st1, 1.0f / 60.0f);
    EXPECT_GT(st1.values[2], 0.0f);

    // Stretching - should produce NO force in compression-only mode
    auto st2 = make_state_spring(0.15f, 0.0f);
    step_component(comp, st2, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st2.values[2], 0.0f);
}

TEST(SpringTest, Branchless_CompressionOnly_False)
{
    // Verify branchless behavior: force should be produced for both compression and stretching
    auto comp = make_spring(1000.0f, 10.0f, 0.1f, false);

    // Compression - should produce force
    auto st1 = make_state_spring(0.05f, 0.0f);
    step_component(comp, st1, 1.0f / 60.0f);
    EXPECT_GT(st1.values[2], 0.0f);

    // Stretching - should ALSO produce force when compression_only = false
    auto st2 = make_state_spring(0.15f, 0.0f);
    step_component(comp, st2, 1.0f / 60.0f);
    EXPECT_GT(st2.values[2], 0.0f);
}

TEST(SpringTest, VariableInput_ForceChanges)
{
    // Use c=0 to test pure Hooke's law force response to position changes
    auto comp = make_spring(1000.0f, /*c=*/0.0f, 0.1f, true);
    auto st = make_state_spring(0.1f, 0.0f);

    // At rest
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[2], 0.0f);

    // Compress
    st.values[0] = 0.05f;
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[2], 50.0f);

    // Compress more
    st.values[0] = 0.0f;
    step_component(comp, st, 1.0 / 60.0);
    EXPECT_FLOAT_EQ(st.values[2], 100.0f);
}

TEST(SpringTest, SymmetricAboutRestLength_CompressionOnly)
{
    // When compression_only, behavior is NOT symmetric
    auto comp = make_spring(1000.0f, 10.0f, 0.1f, true);

    // Compress by 0.05
    auto st1 = make_state_spring(0.05f, 0.0f);
    step_component(comp, st1, 1.0f / 60.0f);
    float force_compression = st1.values[2];

    // Stretch by 0.05 (should give 0 force in compression-only mode)
    auto st2 = make_state_spring(0.15f, 0.0f);
    step_component(comp, st2, 1.0f / 60.0f);
    float force_stretch = st2.values[2];

    EXPECT_GT(force_compression, 0.0f);
    EXPECT_FLOAT_EQ(force_stretch, 0.0f);
}

TEST(SpringTest, VeryStiff_SmallDeflection)
{
    // Very stiff spring (k = 100000 N/m), small deflection
    // Used in precision mechanisms
    auto comp = make_spring(100000.0f, 10.0f, 0.1f, true);
    auto st = make_state_spring(0.099f, 0.0f);  // delta = 0.099 - 0.1 = -0.001

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_NEAR(st.values[2], 100.0f, 0.001f);  // 100000 * 0.001 = 100N (with tolerance)
}

TEST(SpringTest, ZeroStiffness_NoForce)
{
    // Degenerate case: k = 0 (no stiffness)
    auto comp = make_spring(0.0f, 10.0f, 0.1f, true);
    auto st = make_state_spring(0.05f, 0.0f);

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[2], 0.0f);
}

TEST(SpringTest, CompressionOnly_PosDeltaX_NoForce)
{
    // Verify that positive delta_x (stretching) produces zero force when compression_only
    auto comp = make_spring(1000.0f, 10.0f, 0.1f, true);
    auto st = make_state_spring(0.15f, 0.0f);  // delta_x = 0.15 - 0.0 - 0.1 = 0.05 > 0

    step_component(comp, st, 1.0 / 60.0);

    // delta_x > 0 means stretching, which should produce zero force in compression-only mode
    EXPECT_FLOAT_EQ(st.values[2], 0.0f);
}

// =============================================================================
// Regression Tests
// =============================================================================

TEST(SpringTest, Regression_DampingAffectsForce)
{
    // Damping now adds viscous force proportional to velocity.
    // On the first frame (cold start), velocity is zero so damping has no effect.
    // On the second frame with changed position, damping should add force.
    double dt = 1.0 / 60.0;

    // Spring with zero damping
    auto comp_lo = make_spring(1000.0f, /*c=*/0.0f, 0.1f, false);
    auto st1 = make_state_spring(0.05f, 0.0f);
    step_component(comp_lo, st1, dt);  // First frame: cold start
    float force_no_damp_frame1 = st1.values[2];

    // Change position for second frame to create velocity
    st1.values[0] = 0.04f;
    step_component(comp_lo, st1, dt);
    float force_no_damp_frame2 = st1.values[2];

    // Spring with high damping
    auto comp_hi = make_spring(1000.0f, /*c=*/9999.0f, 0.1f, false);
    auto st2 = make_state_spring(0.05f, 0.0f);
    step_component(comp_hi, st2, dt);  // First frame: cold start

    // Same position change
    st2.values[0] = 0.04f;
    step_component(comp_hi, st2, dt);
    float force_hi_damp_frame2 = st2.values[2];

    // On second frame with velocity, high damping should produce different force
    EXPECT_NE(force_no_damp_frame2, force_hi_damp_frame2);
}

TEST(SpringTest, Regression_FirstFrameDampingIsZero)
{
    // On the first frame, the cold start initializes prev_delta_x = delta_x,
    // so velocity = 0 and damping force = 0. Only spring force matters.
    double dt = 1.0 / 60.0;

    auto comp_nodamp = make_spring(1000.0f, /*c=*/0.0f, 0.1f, true);
    auto comp_damp   = make_spring(1000.0f, /*c=*/500.0f, 0.1f, true);

    auto st1 = make_state_spring(0.05f, 0.0f);
    auto st2 = make_state_spring(0.05f, 0.0f);

    step_component(comp_nodamp, st1, dt);
    step_component(comp_damp, st2, dt);

    // First frame: both should produce identical force (no velocity yet)
    EXPECT_FLOAT_EQ(st1.values[2], st2.values[2]);
}

TEST(SpringTest, Regression_ForceAlwaysNonNegative)
{
    // force_out is always >= 0 regardless of mode.
    // Compression-only:
    auto comp1 = make_spring(1000.0f, 10.0f, 0.1f, true);
    auto st1 = make_state_spring(0.05f, 0.0f);
    step_component(comp1, st1, 1.0f / 60.0f);
    EXPECT_GE(st1.values[2], 0.0f);

    // Bidirectional, stretching:
    auto comp2 = make_spring(1000.0f, 10.0f, 0.1f, false);
    auto st2 = make_state_spring(0.2f, 0.0f);
    step_component(comp2, st2, 1.0f / 60.0f);
    EXPECT_GE(st2.values[2], 0.0f);

    // Bidirectional, compression:
    auto st3 = make_state_spring(0.05f, 0.0f);
    step_component(comp2, st3, 1.0f / 60.0f);
    EXPECT_GE(st3.values[2], 0.0f);
}

TEST(SpringTest, Regression_DtAffectsDampingForce)
{
    // dt now matters for damping: velocity = d(delta_x)/dt.
    // On the first frame (cold start), velocity is 0 regardless of dt.
    // With c=0 (no damping), dt should not matter at all.
    auto comp = make_spring(1000.0f, /*c=*/0.0f, 0.1f, true);

    auto st1 = make_state_spring(0.05f, 0.0f);
    auto st2 = make_state_spring(0.05f, 0.0f);

    step_component(comp, st1, 1.0f / 60.0f);

    // Reset the component state for a fresh cold start
    auto comp2 = make_spring(1000.0f, /*c=*/0.0f, 0.1f, true);
    step_component(comp2, st2, 1.0f);

    // With zero damping, dt doesn't affect the result (pure Hooke's law)
    EXPECT_FLOAT_EQ(st1.values[2], st2.values[2]);
}

// =============================================================================
// Viscous Damping Tests
// =============================================================================

TEST(SpringTest, Damping_CorrectForceValue)
{
    // Verify F_damp = c * velocity, where velocity = d(delta_x)/dt.
    // Frame 1: pos_a = 0.05, delta_x = 0.05 - 0.1 = -0.05 (cold start, velocity = 0)
    // Frame 2: pos_a = 0.04, delta_x = 0.04 - 0.1 = -0.06
    //   velocity = (-0.06 - (-0.05)) / dt = -0.01 / (1/60) = -0.6 m/s
    //   F_spring = 1000 * (-0.06) = -60
    //   F_damp = 100 * (-0.6) = -60
    //   total = -60 + (-60) = -120
    //   |total| = 120, compression_only=false → output = 120

    double dt = 1.0 / 60.0;
    auto comp = make_spring(1000.0f, /*c=*/100.0f, 0.1f, false);

    // Frame 1: cold start
    auto st = make_state_spring(0.05f, 0.0f);
    step_component(comp, st, dt);

    // Frame 2: position changed
    st.values[0] = 0.04f;
    step_component(comp, st, dt);

    float expected_spring = 60.0f;   // |1000 * -0.06|
    float expected_damp = 60.0f;     // |100 * -0.6|
    float expected_total = expected_spring + expected_damp; // 120.0
    EXPECT_NEAR(st.values[2], expected_total, 0.01f);
}

TEST(SpringTest, Damping_OpposesDampingForce)
{
    // Moving spring in opposite direction (releasing compression) should reduce total force.
    // Frame 1: delta_x = -0.05 (compressed)
    // Frame 2: delta_x = -0.04 (releasing)
    //   velocity = (-0.04 - (-0.05)) / dt = 0.01 / (1/60) = 0.6 m/s (positive = extending)
    //   F_spring = 1000 * (-0.04) = -40
    //   F_damp = 100 * 0.6 = 60
    //   total = -40 + 60 = 20
    //   |total| = 20 < 40 (spring-only would be 40)

    double dt = 1.0 / 60.0;
    auto comp = make_spring(1000.0f, /*c=*/100.0f, 0.1f, false);

    auto st = make_state_spring(0.05f, 0.0f);
    step_component(comp, st, dt);  // cold start

    st.values[0] = 0.06f;
    step_component(comp, st, dt);

    // Spring-only would give |1000 * -0.04| = 40
    // Damping opposes the spring (release velocity), so total < 40
    EXPECT_LT(st.values[2], 40.0f);
    EXPECT_GE(st.values[2], 0.0f);
}

TEST(SpringTest, Damping_ZeroDamping_PureHooke)
{
    // With c=0, behavior should be identical to pure Hooke's law
    double dt = 1.0 / 60.0;
    auto comp = make_spring(1000.0f, /*c=*/0.0f, 0.1f, false);

    auto st = make_state_spring(0.05f, 0.0f);
    step_component(comp, st, dt);

    // Force = |1000 * (0.05 - 0.1)| = |1000 * -0.05| = 50
    EXPECT_FLOAT_EQ(st.values[2], 50.0f);

    // Second frame with different position
    st.values[0] = 0.04f;
    step_component(comp, st, dt);

    // Force = |1000 * (0.04 - 0.1)| = |1000 * -0.06| = 60
    EXPECT_FLOAT_EQ(st.values[2], 60.0f);
}
