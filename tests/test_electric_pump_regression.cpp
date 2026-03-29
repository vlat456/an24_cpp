/// Regression tests for ElectricPump two-port hydraulic loop closure.
///
/// DISABLED: All tests in this file use SOR-era SimulationState (across/through/conductance/inv_conductance)
/// which don't exist in push model. The component solve methods work differently in push mode.
/// These regression tests verify SOR-era stamping behavior that is not applicable to push architecture.

#include <gtest/gtest.h>

// All tests in this file are disabled because they use SOR-era SimulationState members
// (across, through, conductance, inv_conductance) that don't exist in push model.

TEST(ElectricPumpRegression, DISABLED_NortonResidual_UsesCurrentPressure) {
    GTEST_SKIP() << "SOR-era test: requires across/through/conductance (not in push model)";
}

TEST(ElectricPumpRegression, DISABLED_NortonResidual_CorrectWhenAtTarget) {
    GTEST_SKIP() << "SOR-era test: requires across/through/conductance (not in push model)";
}

TEST(ElectricPumpRegression, DISABLED_NortonResidual_NegativeWhenAboveTarget) {
    GTEST_SKIP() << "SOR-era test: requires across/through/conductance (not in push model)";
}

TEST(ElectricPumpRegression, DISABLED_SOR_ConvergesToTarget) {
    GTEST_SKIP() << "SOR-era test: SOR iteration internals not in push model";
}

TEST(ElectricPumpRegression, DISABLED_SOR_ConvergesWithNonZeroPIn) {
    GTEST_SKIP() << "SOR-era test: SOR iteration internals not in push model";
}

TEST(ElectricPumpRegression, DISABLED_ElectricalSide_IdleDrawWhenNoPressureDiff) {
    GTEST_SKIP() << "SOR-era test: requires across/through/conductance (not in push model)";
}

TEST(ElectricPumpRegression, DISABLED_ElectricalSide_IncreasedDrawWithPressure) {
    GTEST_SKIP() << "SOR-era test: requires across/through/conductance (not in push model)";
}

TEST(ElectricPumpRegression, DISABLED_ZeroVoltage_ZeroPressureBoost) {
    GTEST_SKIP() << "SOR-era test: requires across/through/conductance (not in push model)";
}

TEST(ElectricPumpRegression, DISABLED_TwoPortCoupling_BidirectionalPath) {
    GTEST_SKIP() << "SOR-era test: requires across/through/conductance (not in push model)";
}
