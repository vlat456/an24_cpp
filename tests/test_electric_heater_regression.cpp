/// Regression tests for [BUG-23]: ElectricHeater::solve_thermal() incorrect
/// thermal power formula.
///
/// The old code computed:
///   heat_power = v_in * v_in * efficiency
/// This is wrong because v_in is the raw bus voltage (e.g. 28V), giving
/// unbounded thermal output (28² * 0.9 = 705.6W) regardless of the
/// max_power parameter.
///
/// The correct formula must match the actual electrical power dissipated:
///   g = max_power / (v² + 0.01)
///   heat_power = v² * g * efficiency = max_power * v²/(v²+0.01) * efficiency
///
/// This ensures thermal output is bounded by max_power * efficiency.

#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/all.cpp"
#include "jit_solver/components/port_registry.h"
#include <cmath>

// =============================================================================
// Test Helpers
// =============================================================================

static ElectricHeater<JitProvider> make_heater(float max_p = 500.0f, float eff = 0.9f) {
    ElectricHeater<JitProvider> comp;
    comp.max_power = max_p;
    comp.efficiency = eff;
    comp.provider.set(PortNames::power, 0);
    comp.provider.set(PortNames::heat_out, 1);
    return comp;
}

static SimulationState make_state(size_t n = 4) {
    SimulationState st;
    st.across.resize(n, 0.0f);
    st.through.resize(n, 0.0f);
    st.conductance.resize(n, 0.0f);
    st.inv_conductance.resize(n, 0.0f);
    return st;
}

// =============================================================================
// Core Regression: Thermal output bounded by max_power
// =============================================================================

TEST(ElectricHeaterRegression, ThermalOutput_BoundedByMaxPower) {
    // With max_power = 500W and efficiency = 0.9, thermal output should
    // never exceed 500 * 0.9 = 450W, regardless of input voltage.
    auto comp = make_heater(500.0f, 0.9f);
    auto st = make_state();
    st.across[0] = 28.0f;  // typical 28V bus

    comp.solve_thermal(st, 1.0f);

    float max_thermal = 500.0f * 0.9f;
    EXPECT_LE(st.through[1], max_thermal + 0.1f)
        << "Thermal output must be bounded by max_power * efficiency";

    // Old bug: v² * eff = 28² * 0.9 = 705.6W, exceeding max_power
    EXPECT_LT(st.through[1], 500.0f)
        << "Thermal output exceeds max_power — old bug still present";
}

TEST(ElectricHeaterRegression, ThermalOutput_ApproachesMaxPower) {
    // At rated voltage, thermal output should be close to max_power * efficiency
    auto comp = make_heater(500.0f, 0.9f);
    auto st = make_state();
    st.across[0] = 28.0f;

    comp.solve_thermal(st, 1.0f);

    // At 28V: g = 500 / (784 + 0.01), heat = 784 * g * 0.9 ≈ 500 * 0.9 ≈ 449.99
    float expected = 500.0f * (28.0f * 28.0f) / (28.0f * 28.0f + 0.01f) * 0.9f;
    EXPECT_NEAR(st.through[1], expected, 0.01f);
}

TEST(ElectricHeaterRegression, ThermalOutput_ScalesWithVoltage) {
    // At half voltage, thermal output should be significantly less
    auto comp = make_heater(500.0f, 0.9f);
    auto st = make_state();
    st.across[0] = 14.0f;  // half voltage

    comp.solve_thermal(st, 1.0f);

    // At 14V: g = 500 / (196 + 0.01), heat = 196 * g * 0.9 ≈ 500 * 0.9 * 196/196.01
    float v_sq = 14.0f * 14.0f;
    float expected = 500.0f * v_sq / (v_sq + 0.01f) * 0.9f;
    EXPECT_NEAR(st.through[1], expected, 0.01f);
    EXPECT_LT(st.through[1], 500.0f * 0.9f)
        << "At half voltage, thermal output should be less than at rated";
}

TEST(ElectricHeaterRegression, ThermalOutput_ZeroVoltage) {
    // At zero voltage, thermal output should be zero (or near-zero)
    auto comp = make_heater(500.0f, 0.9f);
    auto st = make_state();
    st.across[0] = 0.0f;

    comp.solve_thermal(st, 1.0f);

    // At 0V: g = 500 / 0.01 = 50000, heat = 0 * 50000 * 0.9 = 0
    EXPECT_NEAR(st.through[1], 0.0f, 0.01f)
        << "Zero voltage should produce zero thermal output";
}

TEST(ElectricHeaterRegression, ThermalOutput_HighVoltage_StillBounded) {
    // Even at unrealistically high voltage (e.g. 100V), thermal output
    // should approach but not exceed max_power * efficiency
    auto comp = make_heater(500.0f, 0.9f);
    auto st = make_state();
    st.across[0] = 100.0f;  // abnormally high

    comp.solve_thermal(st, 1.0f);

    float max_thermal = 500.0f * 0.9f;
    EXPECT_NEAR(st.through[1], max_thermal, 0.01f)
        << "At high voltage, thermal output should saturate at max_power * eff";
}

// =============================================================================
// Consistency: Electrical and thermal power match
// =============================================================================

TEST(ElectricHeaterRegression, ElectricalAndThermal_SameConductance) {
    // Both solve_electrical and solve_thermal should use the same conductance
    // formula: g = max_power / (v² + 0.01)
    auto comp = make_heater(500.0f, 0.9f);

    auto st_elec = make_state();
    st_elec.across[0] = 28.0f;
    comp.solve_electrical(st_elec, 1.0f / 60.0f);

    auto st_therm = make_state();
    st_therm.across[0] = 28.0f;
    comp.solve_thermal(st_therm, 1.0f);

    // Electrical power dissipated: P_elec = v² * G
    float v_sq = 28.0f * 28.0f;
    float g_elec = st_elec.conductance[0];
    float p_elec = v_sq * g_elec;

    // Thermal output should be P_elec * efficiency
    float expected_thermal = p_elec * 0.9f;
    EXPECT_NEAR(st_therm.through[1], expected_thermal, 0.1f)
        << "Thermal output should equal electrical power * efficiency";
}

// =============================================================================
// Different max_power values
// =============================================================================

TEST(ElectricHeaterRegression, DifferentMaxPower_ProperlyCapped) {
    // With max_power = 100W at 28V, thermal should be ~90W (100 * 0.9)
    auto comp = make_heater(100.0f, 0.9f);
    auto st = make_state();
    st.across[0] = 28.0f;

    comp.solve_thermal(st, 1.0f);

    EXPECT_NEAR(st.through[1], 100.0f * 0.9f, 0.01f)
        << "100W heater at rated voltage should output ~90W thermal";
}
