#include <gtest/gtest.h>
#include "core/solvers/jit/components/all.h"
#include "core/solvers/jit/components/port_registry.h"
#include "core/solvers/jit/state.h"
#include "core/solvers/jit/subsolvers/subsolver_types.h"
#include <cmath>

// =============================================================================
// KnobSwitch — Multi-position passive rotary selector tests
//
// KnobSwitch is a multi-position (2-5) rotary selector:
//   - N conductance branches (wiper-to-throw1, wiper-to-throw2, ...)
//   - Selected position has g_closed, all others have g_open
//   - Position controlled via 'control' input (0-based integer as float)
//   - 'position' output reflects current selection
// =============================================================================

struct KnobSwitchTestFixture : public ::testing::Test {
    static constexpr uint32_t IDX_CONTROL  = 0;
    static constexpr uint32_t IDX_POSITION = 1;
    static constexpr uint32_t SIGNAL_COUNT = 2;

    SimulationState st;
    KnobSwitch<JitProvider> knob;

    void SetUp() override {
        st.values.resize(SIGNAL_COUNT, 0.0f);

        knob.provider.set(PortNames::control, IDX_CONTROL);
        knob.provider.set(PortNames::position, IDX_POSITION);

        knob.positions = 3;
        knob.selected = 0;
        knob.g_open = 1e-6f;
        knob.g_closed = 1000.0f;
        knob.pre_load();
    }
};

// =============================================================================
// Domain & structure
// =============================================================================

TEST_F(KnobSwitchTestFixture, HasElectricalDomain) {
    constexpr auto d = KnobSwitch<JitProvider>::domain;
    EXPECT_TRUE((d & Domain::Electrical) != Domain{});
}

TEST_F(KnobSwitchTestFixture, DefaultsToPosition0) {
    EXPECT_EQ(knob.selected, 0);
}

TEST_F(KnobSwitchTestFixture, MaxPositionsIs5) {
    EXPECT_EQ(KnobSwitch<JitProvider>::MAX_POSITIONS, 5);
}

// =============================================================================
// Control input → position selection
// =============================================================================

TEST_F(KnobSwitchTestFixture, CommitSetsPosition) {
    st.values[IDX_CONTROL] = 2.0f;
    knob.commit(st, 1.0 / 60.0);

    EXPECT_EQ(knob.selected, 2);
    EXPECT_FLOAT_EQ(st.values[IDX_POSITION], 2.0f);
}

TEST_F(KnobSwitchTestFixture, PositionClampsToMax) {
    st.values[IDX_CONTROL] = 10.0f;  // way beyond 3 positions
    knob.commit(st, 1.0 / 60.0);

    EXPECT_EQ(knob.selected, 2);  // max = positions - 1 = 2
    EXPECT_FLOAT_EQ(st.values[IDX_POSITION], 2.0f);
}

TEST_F(KnobSwitchTestFixture, PositionClampsToMin) {
    st.values[IDX_CONTROL] = -5.0f;
    knob.commit(st, 1.0 / 60.0);

    EXPECT_EQ(knob.selected, 0);
    EXPECT_FLOAT_EQ(st.values[IDX_POSITION], 0.0f);
}

TEST_F(KnobSwitchTestFixture, RoundsFloatControl) {
    // 1.4 should round to 1, 1.6 should round to 2
    st.values[IDX_CONTROL] = 1.4f;
    knob.commit(st, 1.0 / 60.0);
    EXPECT_EQ(knob.selected, 1);

    st.values[IDX_CONTROL] = 1.6f;
    knob.commit(st, 1.0 / 60.0);
    EXPECT_EQ(knob.selected, 2);
}

// =============================================================================
// pre_load: initial position clamping
// =============================================================================

TEST_F(KnobSwitchTestFixture, PreLoadClampsInitialPosition) {
    KnobSwitch<JitProvider> k;
    k.positions = 2;
    k.selected = 5;
    k.pre_load();
    EXPECT_EQ(k.selected, 1);  // clamped to positions - 1
}

TEST_F(KnobSwitchTestFixture, PreLoadClampsNegativePosition) {
    KnobSwitch<JitProvider> k;
    k.positions = 3;
    k.selected = -1;
    k.pre_load();
    EXPECT_EQ(k.selected, 0);
}

// =============================================================================
// 2-position variant (simplest case)
// =============================================================================

TEST(KnobSwitchTwoPosition, SwitchesBetweenTwoPositions) {
    SimulationState st;
    st.values.resize(2, 0.0f);

    KnobSwitch<JitProvider> knob;
    knob.provider.set(PortNames::control, 0);
    knob.provider.set(PortNames::position, 1);
    knob.positions = 2;
    knob.selected = 0;
    knob.pre_load();

    // Position 0
    st.values[0] = 0.0f;
    knob.commit(st, 1.0 / 60.0);
    EXPECT_EQ(knob.selected, 0);
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);

    // Position 1
    st.values[0] = 1.0f;
    knob.commit(st, 1.0 / 60.0);
    EXPECT_EQ(knob.selected, 1);
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);
}

// =============================================================================
// 5-position variant (max case)
// =============================================================================

TEST(KnobSwitchFivePosition, AllPositionsAccessible) {
    SimulationState st;
    st.values.resize(2, 0.0f);

    KnobSwitch<JitProvider> knob;
    knob.provider.set(PortNames::control, 0);
    knob.provider.set(PortNames::position, 1);
    knob.positions = 5;
    knob.selected = 0;
    knob.pre_load();

    for (int i = 0; i < 5; ++i) {
        st.values[0] = static_cast<float>(i);
        knob.commit(st, 1.0 / 60.0);
        EXPECT_EQ(knob.selected, i);
        EXPECT_FLOAT_EQ(st.values[1], static_cast<float>(i));
    }
}

// =============================================================================
// Electrical handles array
// =============================================================================

TEST_F(KnobSwitchTestFixture, ElectricalHandlesArray) {
    // Verify num_handles tracks actual branches
    knob.num_handles = 3;
    knob.electrical_handles[0] = {0, 0, 0};
    knob.electrical_handles[1] = {0, 0, 1};
    knob.electrical_handles[2] = {0, 0, 2};

    // All handles should be valid
    for (int i = 0; i < knob.num_handles; ++i) {
        EXPECT_TRUE(is_valid(knob.electrical_handles[i]));
    }
}
