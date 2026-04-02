#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/state.h"
#include <cmath>

// =============================================================================
// Test Helpers
// =============================================================================

template <typename Comp>
void step_component(Comp& comp, SimulationState& st, double dt) {
    comp.execute(st, dt);
    comp.commit(st, dt);
}

/// Port layout: [0]=v_in, [1]=v_out, [2]=i_out
static CurrentSense<JitProvider> make_current_sense(float g = 1000.0f) {
    CurrentSense<JitProvider> comp;
    comp.conductance = g;
    comp.provider.set(PortNames::v_in, 0);
    comp.provider.set(PortNames::v_out, 1);
    comp.provider.set(PortNames::i_out, 2);
    return comp;
}

static SimulationState make_state(size_t n = 3) {
    SimulationState st;
    st.values.resize(n, 0.0f);
    st.signal_types.resize(n, {Domain::Electrical, false});
    st.dynamic_signals_count = static_cast<uint32_t>(n);
    return st;
}

// =============================================================================
// Batch 6: Solved Current Tests
// CurrentSense now reads solved branch current from electrical runtime state.
// Old fake formula (i_out = (v_in - v_out) * conductance) has been removed.
// =============================================================================

TEST(CurrentSense, ReportsSolvedCurrent) {
    // CurrentSense should read from electrical runtime state
    auto comp = make_current_sense(1000.0f);
    comp.electrical_handle = {0, 0, 2};  // valid handle with component_index=2

    auto st = make_state();
    st.electrical_rt = new ElectricalRuntimeState();
    st.electrical_rt->branch_currents = {0.0f, 0.0f, 7.5f};  // index 2 = 7.5A

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[2], 7.5f);
    delete st.electrical_rt;
}

TEST(CurrentSense, NoHandleOutputsZero) {
    // CurrentSense with invalid handle outputs 0
    auto comp = make_current_sense(1000.0f);
    comp.electrical_handle = {UINT32_MAX, UINT32_MAX, UINT32_MAX};  // invalid handle

    auto st = make_state();
    st.electrical_rt = new ElectricalRuntimeState();
    st.electrical_rt->branch_currents = {0.0f, 0.0f, 7.5f};

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[2], 0.0f);
    delete st.electrical_rt;
}

TEST(CurrentSense, NoElectricalRtOutputsZero) {
    // CurrentSense without electrical_rt outputs 0
    auto comp = make_current_sense(1000.0f);
    comp.electrical_handle = {0, 0, 2};  // valid handle

    auto st = make_state();
    st.electrical_rt = nullptr;  // no electrical runtime

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[2], 0.0f);
}

TEST(CurrentSense, OutOfRangeComponentIndexOutputsZero) {
    // CurrentSense with component_index beyond branch_currents size outputs 0
    auto comp = make_current_sense(1000.0f);
    comp.electrical_handle = {0, 0, 100};  // valid handle but index 100

    auto st = make_state();
    st.electrical_rt = new ElectricalRuntimeState();
    st.electrical_rt->branch_currents = {0.0f, 0.0f};  // only 3 elements

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[2], 0.0f);
    delete st.electrical_rt;
}

TEST(CurrentSense, ReadsZeroCurrentWhenNoDischarge) {
    // When battery is not discharging, solver reports 0 branch current
    auto comp = make_current_sense(1000.0f);
    comp.electrical_handle = {0, 0, 1};

    auto st = make_state();
    st.electrical_rt = new ElectricalRuntimeState();
    st.electrical_rt->branch_currents = {0.0f, 0.0f};  // 0A discharge

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[2], 0.0f);
    delete st.electrical_rt;
}

TEST(CurrentSense, ReadsNegativeCurrentForCharging) {
    // When battery is charging (current flowing into positive terminal),
    // branch current is negative
    auto comp = make_current_sense(1000.0f);
    comp.electrical_handle = {0, 0, 1};

    auto st = make_state();
    st.electrical_rt = new ElectricalRuntimeState();
    st.electrical_rt->branch_currents = {0.0f, -5.0f};  // -5A (charging)

    step_component(comp, st, 1.0 / 60.0);

    EXPECT_FLOAT_EQ(st.values[2], -5.0f);
    delete st.electrical_rt;
}
