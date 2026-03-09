#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/all.cpp"
#include "jit_solver/components/port_registry.h"

using namespace an24;

// =============================================================================
// Helpers
// =============================================================================

static LUT<JitProvider> make_lut(const std::string& table_str, SimulationState& st) {
    LUT<JitProvider> comp;
    comp.provider.set(PortNames::input, 0);
    comp.provider.set(PortNames::output, 1);

    std::vector<float> keys, values;
    LUT<JitProvider>::parse_table(table_str, keys, values);
    comp.table_offset = static_cast<uint32_t>(st.lut_keys.size());
    comp.table_size   = static_cast<uint16_t>(keys.size());
    st.lut_keys.insert(st.lut_keys.end(), keys.begin(), keys.end());
    st.lut_values.insert(st.lut_values.end(), values.begin(), values.end());
    return comp;
}

static SimulationState make_state() {
    SimulationState st;
    st.across.resize(2, 0.0f);
    return st;
}

// =============================================================================
// parse_table
// =============================================================================

TEST(LUTParseTest, BasicParse) {
    std::vector<float> keys, values;
    bool ok = LUT<JitProvider>::parse_table("0:0; 100:50; 200:100", keys, values);
    ASSERT_TRUE(ok);
    ASSERT_EQ(keys.size(), 3u);
    EXPECT_FLOAT_EQ(keys[0], 0.0f);
    EXPECT_FLOAT_EQ(keys[1], 100.0f);
    EXPECT_FLOAT_EQ(keys[2], 200.0f);
    EXPECT_FLOAT_EQ(values[0], 0.0f);
    EXPECT_FLOAT_EQ(values[1], 50.0f);
    EXPECT_FLOAT_EQ(values[2], 100.0f);
}

TEST(LUTParseTest, NegativeValues) {
    std::vector<float> keys, values;
    bool ok = LUT<JitProvider>::parse_table("-10:-5; 0:0; 10:5", keys, values);
    ASSERT_TRUE(ok);
    ASSERT_EQ(keys.size(), 3u);
    EXPECT_FLOAT_EQ(keys[0], -10.0f);
    EXPECT_FLOAT_EQ(values[0], -5.0f);
}

TEST(LUTParseTest, EmptyString) {
    std::vector<float> keys, values;
    bool ok = LUT<JitProvider>::parse_table("", keys, values);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(keys.empty());
}

TEST(LUTParseTest, SingleEntry) {
    std::vector<float> keys, values;
    bool ok = LUT<JitProvider>::parse_table("42:99", keys, values);
    ASSERT_TRUE(ok);
    ASSERT_EQ(keys.size(), 1u);
    EXPECT_FLOAT_EQ(keys[0], 42.0f);
    EXPECT_FLOAT_EQ(values[0], 99.0f);
}

TEST(LUTParseTest, ExtraWhitespace) {
    std::vector<float> keys, values;
    bool ok = LUT<JitProvider>::parse_table("  0 : 0 ;  100 : 50 ;  200 : 100  ", keys, values);
    ASSERT_TRUE(ok);
    ASSERT_EQ(keys.size(), 3u);
    EXPECT_FLOAT_EQ(keys[1], 100.0f);
    EXPECT_FLOAT_EQ(values[1], 50.0f);
}

// =============================================================================
// Interpolation via solve_logical
// =============================================================================

TEST(LUTSolveTest, ExactBreakpoint) {
    auto st = make_state();
    auto comp = make_lut("0:0; 100:50; 200:100", st);
    st.across[0] = 100.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 50.0f, 0.001f);
}

TEST(LUTSolveTest, LinearInterpolation_Midpoint) {
    auto st = make_state();
    auto comp = make_lut("0:0; 100:100", st);
    st.across[0] = 50.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 50.0f, 0.001f);
}

TEST(LUTSolveTest, LinearInterpolation_Quarter) {
    auto st = make_state();
    auto comp = make_lut("0:0; 100:100", st);
    st.across[0] = 25.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 25.0f, 0.001f);
}

TEST(LUTSolveTest, ClampBelow) {
    auto st = make_state();
    auto comp = make_lut("100:10; 200:20", st);
    st.across[0] = 50.0f;  // below first breakpoint
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 10.0f, 0.001f);  // clamp to first value
}

TEST(LUTSolveTest, ClampAbove) {
    auto st = make_state();
    auto comp = make_lut("100:10; 200:20", st);
    st.across[0] = 300.0f;  // above last breakpoint
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 20.0f, 0.001f);  // clamp to last value
}

TEST(LUTSolveTest, SingleBreakpoint_AlwaysReturnsValue) {
    auto st = make_state();
    auto comp = make_lut("50:99", st);
    st.across[0] = 0.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 99.0f, 0.001f);

    st.across[0] = 1000.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 99.0f, 0.001f);
}

TEST(LUTSolveTest, EmptyTable_ReturnsZero) {
    auto st = make_state();
    LUT<JitProvider> comp;
    comp.provider.set(PortNames::input, 0);
    comp.provider.set(PortNames::output, 1);
    comp.table_offset = 0;
    comp.table_size = 0;
    st.across[0] = 42.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 0.0f, 0.001f);
}

// =============================================================================
// Arena: multiple LUTs sharing contiguous memory
// =============================================================================

TEST(LUTArenaTest, MultipleLUTs_ShareArena) {
    SimulationState st;
    st.across.resize(4, 0.0f);

    // LUT 1: input=0, output=1, table: RPM→torque
    LUT<JitProvider> lut1;
    lut1.provider.set(PortNames::input, 0);
    lut1.provider.set(PortNames::output, 1);
    {
        std::vector<float> keys, values;
        LUT<JitProvider>::parse_table("0:0; 8000:120; 16000:200", keys, values);
        lut1.table_offset = static_cast<uint32_t>(st.lut_keys.size());
        lut1.table_size   = static_cast<uint16_t>(keys.size());
        st.lut_keys.insert(st.lut_keys.end(), keys.begin(), keys.end());
        st.lut_values.insert(st.lut_values.end(), values.begin(), values.end());
    }

    // LUT 2: input=2, output=3, table: temp→resistance
    LUT<JitProvider> lut2;
    lut2.provider.set(PortNames::input, 2);
    lut2.provider.set(PortNames::output, 3);
    {
        std::vector<float> keys, values;
        LUT<JitProvider>::parse_table("0:100; 50:80; 100:60", keys, values);
        lut2.table_offset = static_cast<uint32_t>(st.lut_keys.size());
        lut2.table_size   = static_cast<uint16_t>(keys.size());
        st.lut_keys.insert(st.lut_keys.end(), keys.begin(), keys.end());
        st.lut_values.insert(st.lut_values.end(), values.begin(), values.end());
    }

    // Arena should have 6 entries total (3 + 3), contiguous
    ASSERT_EQ(st.lut_keys.size(), 6u);
    ASSERT_EQ(st.lut_values.size(), 6u);

    // LUT1: x=4000 → linear interp between 0:0 and 8000:120 → 60
    st.across[0] = 4000.0f;
    lut1.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 60.0f, 0.001f);

    // LUT2: x=25 → linear interp between 0:100 and 50:80 → 90
    st.across[2] = 25.0f;
    lut2.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[3], 90.0f, 0.001f);
}

// =============================================================================
// Realistic: An-24 engine RPM→torque curve
// =============================================================================

TEST(LUTSolveTest, RealisticEngineCurve) {
    auto st = make_state();
    auto comp = make_lut("0:0; 1000:5; 5000:12; 10000:20; 16000:28", st);

    // At 0 RPM
    st.across[0] = 0.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 0.0f, 0.001f);

    // At 3000 RPM → between 1000:5 and 5000:12 → 5 + (2000/4000) * 7 = 8.5
    st.across[0] = 3000.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 8.5f, 0.01f);

    // At 16000 RPM (max)
    st.across[0] = 16000.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 28.0f, 0.001f);
}

// =============================================================================
// Edge: parse_table robustness
// =============================================================================

TEST(LUTParseEdge, TrailingSemicolon) {
    std::vector<float> keys, values;
    bool ok = LUT<JitProvider>::parse_table("0:10; 100:20;", keys, values);
    ASSERT_TRUE(ok);
    ASSERT_EQ(keys.size(), 2u);
    EXPECT_FLOAT_EQ(keys[1], 100.0f);
    EXPECT_FLOAT_EQ(values[1], 20.0f);
}

TEST(LUTParseEdge, LeadingSemicolons) {
    std::vector<float> keys, values;
    bool ok = LUT<JitProvider>::parse_table(";;; 10:5; 20:10", keys, values);
    ASSERT_TRUE(ok);
    ASSERT_EQ(keys.size(), 2u);
    EXPECT_FLOAT_EQ(keys[0], 10.0f);
}

TEST(LUTParseEdge, MultipleSemicolonsBetweenEntries) {
    std::vector<float> keys, values;
    bool ok = LUT<JitProvider>::parse_table("0:0;;; 100:50", keys, values);
    ASSERT_TRUE(ok);
    ASSERT_EQ(keys.size(), 2u);
}

TEST(LUTParseEdge, NoColonReturnsEmpty) {
    std::vector<float> keys, values;
    bool ok = LUT<JitProvider>::parse_table("hello world", keys, values);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(keys.empty());
}

TEST(LUTParseEdge, PartialParse_ValidBeforeGarbage) {
    std::vector<float> keys, values;
    bool ok = LUT<JitProvider>::parse_table("0:10; garbage; 100:50", keys, values);
    // Should parse first entry, then stop at garbage
    ASSERT_TRUE(ok);
    ASSERT_EQ(keys.size(), 1u);
    EXPECT_FLOAT_EQ(keys[0], 0.0f);
    EXPECT_FLOAT_EQ(values[0], 10.0f);
}

TEST(LUTParseEdge, DecimalValues) {
    std::vector<float> keys, values;
    bool ok = LUT<JitProvider>::parse_table("0.5:1.5; 2.75:3.25", keys, values);
    ASSERT_TRUE(ok);
    ASSERT_EQ(keys.size(), 2u);
    EXPECT_FLOAT_EQ(keys[0], 0.5f);
    EXPECT_FLOAT_EQ(values[0], 1.5f);
    EXPECT_FLOAT_EQ(keys[1], 2.75f);
    EXPECT_FLOAT_EQ(values[1], 3.25f);
}

TEST(LUTParseEdge, ScientificNotation) {
    std::vector<float> keys, values;
    bool ok = LUT<JitProvider>::parse_table("1e3:5e2; 2e3:1e3", keys, values);
    ASSERT_TRUE(ok);
    ASSERT_EQ(keys.size(), 2u);
    EXPECT_FLOAT_EQ(keys[0], 1000.0f);
    EXPECT_FLOAT_EQ(values[0], 500.0f);
    EXPECT_FLOAT_EQ(keys[1], 2000.0f);
    EXPECT_FLOAT_EQ(values[1], 1000.0f);
}

TEST(LUTParseEdge, VeryLargeTable) {
    // Build a table with 200 entries: "0:0; 1:1; 2:4; ... 199:39601"
    std::string table;
    for (int i = 0; i < 200; ++i) {
        if (i > 0) table += "; ";
        table += std::to_string(i) + ":" + std::to_string(i * i);
    }
    std::vector<float> keys, values;
    bool ok = LUT<JitProvider>::parse_table(table, keys, values);
    ASSERT_TRUE(ok);
    ASSERT_EQ(keys.size(), 200u);
    EXPECT_FLOAT_EQ(keys[0], 0.0f);
    EXPECT_FLOAT_EQ(keys[199], 199.0f);
    EXPECT_FLOAT_EQ(values[199], 199.0f * 199.0f);
}

TEST(LUTParseEdge, ZeroKeyAndZeroValue) {
    std::vector<float> keys, values;
    bool ok = LUT<JitProvider>::parse_table("0:0", keys, values);
    ASSERT_TRUE(ok);
    ASSERT_EQ(keys.size(), 1u);
    EXPECT_FLOAT_EQ(keys[0], 0.0f);
    EXPECT_FLOAT_EQ(values[0], 0.0f);
}

TEST(LUTParseEdge, NegativeOnlyKeys) {
    std::vector<float> keys, values;
    bool ok = LUT<JitProvider>::parse_table("-100:-50; -50:-25; -10:-5", keys, values);
    ASSERT_TRUE(ok);
    ASSERT_EQ(keys.size(), 3u);
    EXPECT_FLOAT_EQ(keys[0], -100.0f);
    EXPECT_FLOAT_EQ(keys[2], -10.0f);
    EXPECT_FLOAT_EQ(values[2], -5.0f);
}

// =============================================================================
// Edge: interpolation boundary conditions
// =============================================================================

TEST(LUTInterpolationEdge, TwoEntryTable_AllPositions) {
    auto st = make_state();
    auto comp = make_lut("0:0; 100:200", st);

    // Below first breakpoint -> clamp to first value
    st.across[0] = -50.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 0.0f, 0.001f);

    // Exactly at first breakpoint
    st.across[0] = 0.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 0.0f, 0.001f);

    // Quarter
    st.across[0] = 25.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 50.0f, 0.001f);

    // Midpoint
    st.across[0] = 50.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 100.0f, 0.001f);

    // Three quarters
    st.across[0] = 75.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 150.0f, 0.001f);

    // Exactly at last breakpoint
    st.across[0] = 100.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 200.0f, 0.001f);

    // Above last breakpoint -> clamp to last value
    st.across[0] = 150.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 200.0f, 0.001f);
}

TEST(LUTInterpolationEdge, EveryBreakpointExact) {
    auto st = make_state();
    auto comp = make_lut("0:100; 10:200; 20:50; 30:300; 40:0", st);

    float expected[] = {100.0f, 200.0f, 50.0f, 300.0f, 0.0f};
    for (int i = 0; i < 5; ++i) {
        st.across[0] = static_cast<float>(i * 10);
        comp.solve_logical(st, 1.0f / 60.0f);
        EXPECT_NEAR(st.across[1], expected[i], 0.001f)
            << "Failed at breakpoint x=" << i * 10;
    }
}

TEST(LUTInterpolationEdge, EverySegmentMidpoint) {
    // Table with non-uniform slopes: check midpoint of each segment
    auto st = make_state();
    auto comp = make_lut("0:0; 10:100; 30:200; 60:500", st);

    // Midpoint of [0,10]: x=5 -> 0 + 0.5*(100-0) = 50
    st.across[0] = 5.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 50.0f, 0.01f);

    // Midpoint of [10,30]: x=20 -> 100 + 0.5*(200-100) = 150
    st.across[0] = 20.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 150.0f, 0.01f);

    // Midpoint of [30,60]: x=45 -> 200 + 0.5*(500-200) = 350
    st.across[0] = 45.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 350.0f, 0.01f);
}

TEST(LUTInterpolationEdge, NonMonotonicValues) {
    // Values that go up then down (sawtooth) — keys must be monotonic, values can be anything
    auto st = make_state();
    auto comp = make_lut("0:0; 50:100; 100:0", st);

    st.across[0] = 25.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 50.0f, 0.01f);

    st.across[0] = 75.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 50.0f, 0.01f);
}

TEST(LUTInterpolationEdge, EqualAdjacentKeys) {
    // Guard against division by zero when two keys are equal
    auto st = make_state();
    auto comp = make_lut("10:5; 10:15; 20:25", st);

    // At the duplicate key, denom=0 -> t=0, should return first value
    st.across[0] = 10.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    // Should not crash/NaN — just return some valid value
    EXPECT_FALSE(std::isnan(st.across[1]));
    EXPECT_FALSE(std::isinf(st.across[1]));
}

TEST(LUTInterpolationEdge, VeryLargeInput) {
    auto st = make_state();
    auto comp = make_lut("0:10; 100:20", st);

    st.across[0] = 1e10f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 20.0f, 0.001f);  // clamp to last

    st.across[0] = -1e10f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 10.0f, 0.001f);  // clamp to first
}

TEST(LUTInterpolationEdge, ConstantValueTable) {
    // All values the same — interpolation should always return that value
    auto st = make_state();
    auto comp = make_lut("0:42; 50:42; 100:42", st);

    for (float x = -10.0f; x <= 110.0f; x += 10.0f) {
        st.across[0] = x;
        comp.solve_logical(st, 1.0f / 60.0f);
        EXPECT_NEAR(st.across[1], 42.0f, 0.001f) << "Failed at x=" << x;
    }
}

TEST(LUTInterpolationEdge, DecreasingValues) {
    // Values decrease as keys increase
    auto st = make_state();
    auto comp = make_lut("0:100; 50:50; 100:0", st);

    st.across[0] = 25.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 75.0f, 0.01f);

    st.across[0] = 75.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 25.0f, 0.01f);
}

TEST(LUTInterpolationEdge, LargeTable_Accuracy) {
    // 200-entry linear table: y = 2*x. Test at many points.
    std::string table;
    for (int i = 0; i <= 199; ++i) {
        if (i > 0) table += "; ";
        table += std::to_string(i) + ":" + std::to_string(i * 2);
    }
    auto st = make_state();
    auto comp = make_lut(table, st);

    // Test exact breakpoints
    for (int i = 0; i <= 199; ++i) {
        st.across[0] = static_cast<float>(i);
        comp.solve_logical(st, 1.0f / 60.0f);
        EXPECT_NEAR(st.across[1], static_cast<float>(i * 2), 0.01f)
            << "Breakpoint " << i;
    }

    // Test midpoints between breakpoints
    for (int i = 0; i < 199; ++i) {
        st.across[0] = static_cast<float>(i) + 0.5f;
        comp.solve_logical(st, 1.0f / 60.0f);
        float expected = static_cast<float>(i * 2) + 1.0f;  // midpoint of 2i and 2(i+1)
        EXPECT_NEAR(st.across[1], expected, 0.01f)
            << "Midpoint between " << i << " and " << (i + 1);
    }
}

TEST(LUTInterpolationEdge, NegativeKeysInterpolation) {
    auto st = make_state();
    auto comp = make_lut("-100:0; -50:25; 0:50; 50:75; 100:100", st);

    st.across[0] = -75.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 12.5f, 0.01f);

    st.across[0] = -25.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 37.5f, 0.01f);

    st.across[0] = 25.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 62.5f, 0.01f);
}

// =============================================================================
// Arena: stress tests
// =============================================================================

TEST(LUTArenaStress, ManyLUTs_CorrectIsolation) {
    SimulationState st;
    // 50 LUTs, each with unique table, each using 2 signals
    const int N = 50;
    st.across.resize(N * 2, 0.0f);

    std::vector<LUT<JitProvider>> luts(N);
    for (int i = 0; i < N; ++i) {
        luts[i].provider.set(PortNames::input, i * 2);
        luts[i].provider.set(PortNames::output, i * 2 + 1);

        std::vector<float> keys, values;
        // Each LUT: y = (i+1) * x, table from 0 to 100
        std::string table = "0:0; 100:" + std::to_string((i + 1) * 100);
        LUT<JitProvider>::parse_table(table, keys, values);
        luts[i].table_offset = static_cast<uint32_t>(st.lut_keys.size());
        luts[i].table_size   = static_cast<uint16_t>(keys.size());
        st.lut_keys.insert(st.lut_keys.end(), keys.begin(), keys.end());
        st.lut_values.insert(st.lut_values.end(), values.begin(), values.end());
    }

    // Arena should have N*2 entries total
    ASSERT_EQ(st.lut_keys.size(), static_cast<size_t>(N * 2));

    // Each LUT with input=50 should produce (i+1)*50
    for (int i = 0; i < N; ++i) {
        st.across[i * 2] = 50.0f;
        luts[i].solve_logical(st, 1.0f / 60.0f);
        EXPECT_NEAR(st.across[i * 2 + 1], static_cast<float>((i + 1) * 50), 0.01f)
            << "LUT #" << i << " isolation failure";
    }
}

TEST(LUTArenaStress, ArenaOffsets_NoOverlap) {
    SimulationState st;
    const int N = 20;
    st.across.resize(N * 2, 0.0f);

    struct LutInfo { uint32_t offset; uint16_t size; };
    std::vector<LutInfo> infos;

    for (int i = 0; i < N; ++i) {
        std::vector<float> keys, values;
        // Variable-sized tables: 3 to 22 entries
        std::string table;
        int entries = 3 + i;
        for (int j = 0; j < entries; ++j) {
            if (j > 0) table += "; ";
            table += std::to_string(j * 10) + ":" + std::to_string(j * 5);
        }
        LUT<JitProvider>::parse_table(table, keys, values);
        uint32_t offset = static_cast<uint32_t>(st.lut_keys.size());
        uint16_t size = static_cast<uint16_t>(keys.size());
        infos.push_back({offset, size});
        st.lut_keys.insert(st.lut_keys.end(), keys.begin(), keys.end());
        st.lut_values.insert(st.lut_values.end(), values.begin(), values.end());
    }

    // Verify no overlaps: each LUT's range [offset, offset+size) is disjoint
    for (int i = 0; i < N; ++i) {
        for (int j = i + 1; j < N; ++j) {
            uint32_t a_start = infos[i].offset, a_end = infos[i].offset + infos[i].size;
            uint32_t b_start = infos[j].offset, b_end = infos[j].offset + infos[j].size;
            EXPECT_TRUE(a_end <= b_start || b_end <= a_start)
                << "LUT #" << i << " [" << a_start << "," << a_end << ") overlaps with "
                << "LUT #" << j << " [" << b_start << "," << b_end << ")";
        }
    }

    // Verify total arena size is sum of all individual sizes
    uint32_t total = 0;
    for (const auto& info : infos) total += info.size;
    EXPECT_EQ(st.lut_keys.size(), total);
    EXPECT_EQ(st.lut_values.size(), total);
}

TEST(LUTArenaStress, LargeTableInArena) {
    // Single LUT with 1000 entries
    std::string table;
    for (int i = 0; i < 1000; ++i) {
        if (i > 0) table += "; ";
        table += std::to_string(i) + ":" + std::to_string(std::sin(static_cast<float>(i) * 0.01f));
    }
    auto st = make_state();
    auto comp = make_lut(table, st);
    ASSERT_EQ(comp.table_size, 1000);
    ASSERT_EQ(st.lut_keys.size(), 1000u);

    // Spot check: x=500 should interpolate between entries 500 and 501
    st.across[0] = 500.5f;
    comp.solve_logical(st, 1.0f / 60.0f);
    float expected = (std::sin(500.0f * 0.01f) + std::sin(501.0f * 0.01f)) / 2.0f;
    EXPECT_NEAR(st.across[1], expected, 0.01f);
}

// =============================================================================
// Regression: ensure solved output doesn't bleed between calls
// =============================================================================

TEST(LUTRegression, OutputNotStale) {
    auto st = make_state();
    auto comp = make_lut("0:0; 100:100", st);

    // First solve
    st.across[0] = 50.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 50.0f, 0.001f);

    // Change input, solve again
    st.across[0] = 80.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 80.0f, 0.001f);

    // Change back to 0
    st.across[0] = 0.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 0.0f, 0.001f);
}

TEST(LUTRegression, MultipleStepsStable) {
    // Simulate multiple time steps with same input — output must be stable
    auto st = make_state();
    auto comp = make_lut("0:0; 50:100; 100:75", st);

    st.across[0] = 25.0f;
    for (int i = 0; i < 100; ++i) {
        comp.solve_logical(st, 1.0f / 60.0f);
        EXPECT_NEAR(st.across[1], 50.0f, 0.001f) << "Unstable at step " << i;
    }
}

TEST(LUTRegression, ZeroInputZeroTable) {
    auto st = make_state();
    auto comp = make_lut("0:0; 100:0", st);
    st.across[0] = 50.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    EXPECT_NEAR(st.across[1], 0.0f, 0.001f);
}

TEST(LUTRegression, TableOffsetRespected) {
    // Manually prepend junk data to arena, then create LUT at offset
    SimulationState st;
    st.across.resize(2, 0.0f);

    // Junk prefix
    st.lut_keys = {999.0f, 888.0f, 777.0f};
    st.lut_values = {111.0f, 222.0f, 333.0f};

    // Real table at offset 3
    std::vector<float> keys, values;
    LUT<JitProvider>::parse_table("0:0; 100:50", keys, values);

    LUT<JitProvider> comp;
    comp.provider.set(PortNames::input, 0);
    comp.provider.set(PortNames::output, 1);
    comp.table_offset = static_cast<uint32_t>(st.lut_keys.size());
    comp.table_size   = static_cast<uint16_t>(keys.size());
    st.lut_keys.insert(st.lut_keys.end(), keys.begin(), keys.end());
    st.lut_values.insert(st.lut_values.end(), values.begin(), values.end());

    st.across[0] = 50.0f;
    comp.solve_logical(st, 1.0f / 60.0f);
    // Must use the real table at offset 3, not the junk prefix
    EXPECT_NEAR(st.across[1], 25.0f, 0.001f);
}
