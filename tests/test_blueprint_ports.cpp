#include <gtest/gtest.h>
#include "jit_solver/state.h"
#include "jit_solver/components/all.h"

using namespace an24;

TEST(BlueprintInput, PassThroughLikeBus) {
    // BlueprintInput should behave like Bus (no-op component)
    // Union-find will collapse port to connected signal

    SimulationState st;
    st.across.resize(2, 0.0f);
    st.through.resize(2, 0.0f);
    st.conductance.resize(2, 0.0f);
    st.inv_conductance.resize(2, 0.0f);
    st.signal_types.resize(2, SignalType{Domain::Electrical, false});
    st.convergence_buffer.resize(2, 0.0f);

    BlueprintInput<JitProvider> input;

    // Should not crash, should not modify state
    ASSERT_NO_THROW(input.solve_electrical(st, 0.016f));

    // State should remain unchanged (no stamping)
    EXPECT_EQ(st.through[0], 0.0f);
    EXPECT_EQ(st.through[1], 0.0f);
    EXPECT_EQ(st.conductance[0], 0.0f);
    EXPECT_EQ(st.conductance[1], 0.0f);
}

TEST(BlueprintInput, ExposedPortParameters) {
    // BlueprintInput stores exposed port metadata for parent blueprint

    BlueprintInput<JitProvider> input;
    input.exposed_type_str = "V";
    input.exposed_direction_str = "In";

    EXPECT_EQ(input.exposed_type_str, "V");
    EXPECT_EQ(input.exposed_direction_str, "In");
}

TEST(BlueprintOutput, PassThroughLikeBus) {
    // BlueprintOutput should behave like Bus (no-op component)
    // Union-find will collapse port to connected signal

    SimulationState st;
    st.across.resize(2, 0.0f);
    st.through.resize(2, 0.0f);
    st.conductance.resize(2, 0.0f);
    st.inv_conductance.resize(2, 0.0f);
    st.signal_types.resize(2, SignalType{Domain::Electrical, false});
    st.convergence_buffer.resize(2, 0.0f);

    BlueprintOutput<JitProvider> output;

    // Should not crash, should not modify state
    ASSERT_NO_THROW(output.solve_electrical(st, 0.016f));

    // State should remain unchanged (no stamping)
    EXPECT_EQ(st.through[0], 0.0f);
    EXPECT_EQ(st.through[1], 0.0f);
    EXPECT_EQ(st.conductance[0], 0.0f);
    EXPECT_EQ(st.conductance[1], 0.0f);
}

TEST(BlueprintOutput, ExposedPortParameters) {
    // BlueprintOutput stores exposed port metadata for parent blueprint

    BlueprintOutput<JitProvider> output;
    output.exposed_type_str = "V";
    output.exposed_direction_str = "Out";

    EXPECT_EQ(output.exposed_type_str, "V");
    EXPECT_EQ(output.exposed_direction_str, "Out");
}

