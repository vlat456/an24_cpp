#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/state.h"


// =============================================================================
// Test Helpers
// =============================================================================

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
    st.across.resize(3, 0.0f);
    st.through.resize(3, 0.0f);
    st.conductance.resize(3, 0.0f);
    st.across[0] = pos_a;
    st.across[1] = pos_b;
    st.across[2] = 0.0f;
    return st;
}

// =============================================================================
// Spring Tests
// =============================================================================

TEST(SpringTest, AtRestLength_ZeroForce)
{
    auto comp = make_spring(1000.0f, 10.0f, 0.1f, true);
    auto st = make_state_spring(0.1f, 0.0f);  // pos_a - pos_b = 0.1 = rest_length

    comp.solve_mechanical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 0.0f);
}

TEST(SpringTest, Compression_GeneratesForce)
{
    // Spring compressed by 0.05m: force = k * delta_x = 1000 * 0.05 = 50N
    auto comp = make_spring(1000.0f, 10.0f, 0.1f, true);
    auto st = make_state_spring(0.05f, 0.0f);  // delta = 0.05 - 0.1 = -0.05 (compressed)

    comp.solve_mechanical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 50.0f);
}

TEST(SpringTest, CompressionOnly_Stretching_NoForce)
{
    // compression_only = true, so stretching should not generate force
    auto comp = make_spring(1000.0f, 10.0f, 0.1f, true);
    auto st = make_state_spring(0.2f, 0.0f);  // delta = 0.2 - 0.1 = 0.1 (stretched)

    comp.solve_mechanical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 0.0f);  // No force in compression-only mode
}

TEST(SpringTest, NotCompressionOnly_Stretching_GeneratesForce)
{
    // compression_only = false, so stretching should generate force
    auto comp = make_spring(1000.0f, 10.0f, 0.1f, false);
    auto st = make_state_spring(0.2f, 0.0f);  // delta = 0.2 - 0.1 = 0.1 (stretched)

    comp.solve_mechanical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 100.0f);  // Force = 1000 * 0.1 = 100N
}

TEST(SpringTest, NotCompressionOnly_Compression_GeneratesForce)
{
    // compression_only = false, compression should still work
    auto comp = make_spring(1000.0f, 10.0f, 0.1f, false);
    auto st = make_state_spring(0.05f, 0.0f);  // delta = -0.05 (compressed)

    comp.solve_mechanical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 50.0f);
}

TEST(SpringTest, DifferentStiffness_ForceScales)
{
    // k = 5000 N/m, compressed by 0.02m: force = 5000 * 0.02 = 100N
    auto comp = make_spring(5000.0f, 10.0f, 0.1f, true);
    auto st = make_state_spring(0.08f, 0.0f);  // delta = 0.08 - 0.1 = -0.02

    comp.solve_mechanical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 100.0f);
}

TEST(SpringTest, DifferentRestLength_Comparison)
{
    // rest_length = 0.2m, at 0.15m: delta = 0.15 - 0.2 = -0.05 (compressed)
    // force = 1000 * 0.05 = 50N
    auto comp = make_spring(1000.0f, 10.0f, 0.2f, true);
    auto st = make_state_spring(0.15f, 0.0f);

    comp.solve_mechanical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 50.0f);
}

TEST(SpringTest, ZeroRestLength_Compression)
{
    // rest_length = 0, compression when pos_a < pos_b
    // pos_a = 0.0, pos_b = 0.05: delta = 0.0 - 0.05 - 0.0 = -0.05 (compressed)
    auto comp = make_spring(1000.0f, 10.0f, 0.0f, true);
    auto st = make_state_spring(0.0f, 0.05f);

    comp.solve_mechanical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 50.0f);  // 1000 * abs(-0.05) = 50N
}

TEST(SpringTest, LargeCompression_LargeForce)
{
    // k = 10000 N/m, compressed by 0.1m: force = 10000 * 0.1 = 1000N
    auto comp = make_spring(10000.0f, 10.0f, 0.1f, true);
    auto st = make_state_spring(0.0f, 0.0f);  // delta = 0 - 0.1 = -0.1

    comp.solve_mechanical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 1000.0f);
}

TEST(SpringTest, NegativePositions_Compression)
{
    // Both positions negative: pos_a = -0.05, pos_b = 0.0
    // delta = -0.05 - 0.0 - 0.1 = -0.15 (compressed)
    auto comp = make_spring(1000.0f, 10.0f, 0.1f, true);
    auto st = make_state_spring(-0.05f, 0.0f);

    comp.solve_mechanical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 150.0f);
}

TEST(SpringTest, RealWorld_RUG82_Governor)
{
    // RUG-82 carbon governor spring
    // Stiff spring, compression only, short rest length
    auto comp = make_spring(50000.0f, 100.0f, 0.05f, true);
    auto st = make_state_spring(0.04f, 0.0f);  // delta = 0.04 - 0.05 = -0.01

    comp.solve_mechanical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 500.0f);  // 50000 * 0.01 = 500N
}

TEST(SpringTest, RealWorld_SuspensionSpring)
{
    // Aircraft landing gear suspension
    // Softer spring, works both ways
    auto comp = make_spring(50000.0f, 500.0f, 0.3f, false);
    auto st = make_state_spring(0.25f, 0.0f);  // delta = 0.25 - 0.3 = -0.05 (compression)

    comp.solve_mechanical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 2500.0f);  // 50000 * 0.05 = 2500N
}

TEST(SpringTest, RealWorld_ValveSpring)
{
    // Small valve spring, compression only
    auto comp = make_spring(2000.0f, 5.0f, 0.02f, true);
    auto st = make_state_spring(0.015f, 0.0f);  // delta = 0.015 - 0.02 = -0.005

    comp.solve_mechanical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 10.0f);  // 2000 * 0.005 = 10N
}

TEST(SpringTest, Branchless_CompressionOnly_True)
{
    // Verify branchless behavior: force should be 0 for stretching
    auto comp = make_spring(1000.0f, 10.0f, 0.1f, true);

    // Compression - should produce force
    auto st1 = make_state_spring(0.05f, 0.0f);
    comp.solve_mechanical(st1, 1.0f / 60.0f);
    EXPECT_GT(st1.across[2], 0.0f);

    // Stretching - should produce NO force in compression-only mode
    auto st2 = make_state_spring(0.15f, 0.0f);
    comp.solve_mechanical(st2, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st2.across[2], 0.0f);
}

TEST(SpringTest, Branchless_CompressionOnly_False)
{
    // Verify branchless behavior: force should be produced for both compression and stretching
    auto comp = make_spring(1000.0f, 10.0f, 0.1f, false);

    // Compression - should produce force
    auto st1 = make_state_spring(0.05f, 0.0f);
    comp.solve_mechanical(st1, 1.0f / 60.0f);
    EXPECT_GT(st1.across[2], 0.0f);

    // Stretching - should ALSO produce force when compression_only = false
    auto st2 = make_state_spring(0.15f, 0.0f);
    comp.solve_mechanical(st2, 1.0f / 60.0f);
    EXPECT_GT(st2.across[2], 0.0f);
}

TEST(SpringTest, VariableInput_ForceChanges)
{
    auto comp = make_spring(1000.0f, 10.0f, 0.1f, true);
    auto st = make_state_spring(0.1f, 0.0f);

    // At rest
    comp.solve_mechanical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.across[2], 0.0f);

    // Compress
    st.across[0] = 0.05f;
    comp.solve_mechanical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.across[2], 50.0f);

    // Compress more
    st.across[0] = 0.0f;
    comp.solve_mechanical(st, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(st.across[2], 100.0f);
}

TEST(SpringTest, SymmetricAboutRestLength_CompressionOnly)
{
    // When compression_only, behavior is NOT symmetric
    auto comp = make_spring(1000.0f, 10.0f, 0.1f, true);

    // Compress by 0.05
    auto st1 = make_state_spring(0.05f, 0.0f);
    comp.solve_mechanical(st1, 1.0f / 60.0f);
    float force_compression = st1.across[2];

    // Stretch by 0.05 (should give 0 force in compression-only mode)
    auto st2 = make_state_spring(0.15f, 0.0f);
    comp.solve_mechanical(st2, 1.0f / 60.0f);
    float force_stretch = st2.across[2];

    EXPECT_GT(force_compression, 0.0f);
    EXPECT_FLOAT_EQ(force_stretch, 0.0f);
}

TEST(SpringTest, VeryStiff_SmallDeflection)
{
    // Very stiff spring (k = 100000 N/m), small deflection
    // Used in precision mechanisms
    auto comp = make_spring(100000.0f, 10.0f, 0.1f, true);
    auto st = make_state_spring(0.099f, 0.0f);  // delta = 0.099 - 0.1 = -0.001

    comp.solve_mechanical(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[2], 100.0f, 0.001f);  // 100000 * 0.001 = 100N (with tolerance)
}

TEST(SpringTest, ZeroStiffness_NoForce)
{
    // Degenerate case: k = 0 (no stiffness)
    auto comp = make_spring(0.0f, 10.0f, 0.1f, true);
    auto st = make_state_spring(0.05f, 0.0f);

    comp.solve_mechanical(st, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st.across[2], 0.0f);
}

TEST(SpringTest, CompressionOnly_PosDeltaX_NoForce)
{
    // Verify that positive delta_x (stretching) produces zero force when compression_only
    auto comp = make_spring(1000.0f, 10.0f, 0.1f, true);
    auto st = make_state_spring(0.15f, 0.0f);  // delta_x = 0.15 - 0.0 - 0.1 = 0.05 > 0

    comp.solve_mechanical(st, 1.0f / 60.0f);

    // delta_x > 0 means stretching, which should produce zero force in compression-only mode
    EXPECT_FLOAT_EQ(st.across[2], 0.0f);
}

// =============================================================================
// Regression Tests
// =============================================================================

TEST(SpringTest, Regression_DampingParam_HasNoEffect)
{
    // c (damping) is declared but not yet implemented.
    // Verify that force depends only on k, not c.
    auto comp_lo = make_spring(1000.0f, /*c=*/0.0f, 0.1f, true);
    auto comp_hi = make_spring(1000.0f, /*c=*/9999.0f, 0.1f, true);

    auto st1 = make_state_spring(0.05f, 0.0f);
    auto st2 = make_state_spring(0.05f, 0.0f);

    comp_lo.solve_mechanical(st1, 1.0f / 60.0f);
    comp_hi.solve_mechanical(st2, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(st1.across[2], st2.across[2]);
}

TEST(SpringTest, Regression_ForceAlwaysNonNegative)
{
    // force_out is always >= 0 regardless of mode.
    // Compression-only:
    auto comp1 = make_spring(1000.0f, 10.0f, 0.1f, true);
    auto st1 = make_state_spring(0.05f, 0.0f);
    comp1.solve_mechanical(st1, 1.0f / 60.0f);
    EXPECT_GE(st1.across[2], 0.0f);

    // Bidirectional, stretching:
    auto comp2 = make_spring(1000.0f, 10.0f, 0.1f, false);
    auto st2 = make_state_spring(0.2f, 0.0f);
    comp2.solve_mechanical(st2, 1.0f / 60.0f);
    EXPECT_GE(st2.across[2], 0.0f);

    // Bidirectional, compression:
    auto st3 = make_state_spring(0.05f, 0.0f);
    comp2.solve_mechanical(st3, 1.0f / 60.0f);
    EXPECT_GE(st3.across[2], 0.0f);
}

TEST(SpringTest, Regression_DtIgnored)
{
    // dt is currently unused (pure algebraic Hooke's law).
    auto comp = make_spring(1000.0f, 10.0f, 0.1f, true);

    auto st1 = make_state_spring(0.05f, 0.0f);
    auto st2 = make_state_spring(0.05f, 0.0f);

    comp.solve_mechanical(st1, 1.0f / 60.0f);
    comp.solve_mechanical(st2, 1.0f);

    EXPECT_FLOAT_EQ(st1.across[2], st2.across[2]);
}
