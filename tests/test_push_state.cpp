#include <gtest/gtest.h>

#include "core/solvers/jit/state.h"

template <typename T>
concept HasThrough = requires(T t) { t.through; };

template <typename T>
concept HasConductance = requires(T t) { t.conductance; };

template <typename T>
concept HasInvConductance = requires(T t) { t.inv_conductance; };

template <typename T>
concept HasConvergenceBuffer = requires(T t) { t.convergence_buffer; };

TEST(push_state, ValuesArrayExists) {
    SimulationState st;
    EXPECT_TRUE(st.values.empty());
}

TEST(push_state, AllocateSignalWritesToValues) {
    SimulationState st;
    const uint32_t idx = st.allocate_signal(28.0f, {Domain::Electrical, false});
    EXPECT_EQ(idx, 0u);
    EXPECT_FLOAT_EQ(st.values[idx], 28.0f);
}

TEST(push_state, AllocateMultipleSignals) {
    SimulationState st;
    const uint32_t a = st.allocate_signal(28.0f, {Domain::Electrical, false});
    const uint32_t b = st.allocate_signal(0.0f, {Domain::Electrical, false});
    const uint32_t c = st.allocate_signal(115.0f, {Domain::Electrical, true});

    EXPECT_EQ(a, 0u);
    EXPECT_EQ(b, 1u);
    EXPECT_EQ(c, 2u);
    EXPECT_EQ(st.dynamic_signals_count, 2u);
    EXPECT_EQ(st.values.size(), 3u);
    EXPECT_FLOAT_EQ(st.values[0], 28.0f);
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
    EXPECT_FLOAT_EQ(st.values[2], 115.0f);
}

TEST(push_state, DynamicInsertedBeforeFixed) {
    SimulationState st;
    const uint32_t fixed0 = st.allocate_signal(115.0f, {Domain::Electrical, true});
    const uint32_t dyn0 = st.allocate_signal(28.0f, {Domain::Electrical, false});
    const uint32_t dyn1 = st.allocate_signal(12.0f, {Domain::Electrical, false});

    EXPECT_EQ(fixed0, 0u);
    EXPECT_EQ(dyn0, 1u);
    EXPECT_EQ(dyn1, 2u);
    EXPECT_EQ(st.dynamic_signals_count, 2u);
    EXPECT_FLOAT_EQ(st.values[0], 115.0f);
    EXPECT_FLOAT_EQ(st.values[1], 28.0f);
    EXPECT_FLOAT_EQ(st.values[2], 12.0f);
}

TEST(push_state, NoLegacySorArrays) {
    EXPECT_FALSE(HasThrough<SimulationState>);
    EXPECT_FALSE(HasConductance<SimulationState>);
    EXPECT_FALSE(HasInvConductance<SimulationState>);
    EXPECT_FALSE(HasConvergenceBuffer<SimulationState>);
}

TEST(push_state, LutArenaPreserved) {
    SimulationState st;
    st.lut_keys.push_back(0.0f);
    st.lut_keys.push_back(1.0f);
    st.lut_values.push_back(0.0f);
    st.lut_values.push_back(100.0f);

    EXPECT_EQ(st.lut_keys.size(), 2u);
    EXPECT_EQ(st.lut_values.size(), 2u);
}
