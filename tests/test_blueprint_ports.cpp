#include <gtest/gtest.h>
#include "core/solvers/jit/state.h"
#include "core/solvers/jit/components/all.h"
#include "core/solvers/jit/components/port_registry.h"

template <typename Comp>
void step_component(Comp& comp, SimulationState& st, double dt) {
    comp.execute(st, dt);
    comp.commit(st, dt);
}

TEST(BlueprintInput, PassThroughLikeBus) {
    // BlueprintInput should behave like Bus (no-op component)
    // Union-find will collapse port to connected signal

    SimulationState st;
    st.values.resize(2, 0.0f);
    st.signal_types.resize(2, {Domain::Electrical, false});

    BlueprintInput<JitProvider> input;
    input.provider.set(PortNames::v, 0);

    // Should not crash, should not modify state
    ASSERT_NO_THROW(step_component(input, st, 0.016));

    // State should remain unchanged (no stamping in push model)
    EXPECT_EQ(st.values[0], 0.0f);
    EXPECT_EQ(st.values[1], 0.0f);
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
    st.values.resize(2, 0.0f);
    st.signal_types.resize(2, {Domain::Electrical, false});

    BlueprintOutput<JitProvider> output;
    output.provider.set(PortNames::v, 0);

    // Should not crash, should not modify state
    ASSERT_NO_THROW(step_component(output, st, 0.016f));

    // State should remain unchanged (no stamping in push model)
    EXPECT_EQ(st.values[0], 0.0f);
    EXPECT_EQ(st.values[1], 0.0f);
}

TEST(BlueprintOutput, ExposedPortParameters) {
    // BlueprintOutput stores exposed port metadata for parent blueprint

    BlueprintOutput<JitProvider> output;
    output.exposed_type_str = "V";
    output.exposed_direction_str = "Out";

    EXPECT_EQ(output.exposed_type_str, "V");
    EXPECT_EQ(output.exposed_direction_str, "Out");
}
