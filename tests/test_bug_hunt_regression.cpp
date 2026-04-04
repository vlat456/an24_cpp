/// Regression tests for bugs found during comprehensive code review.
///
/// BUG-AotDefault: AotProvider::get() returned 0 for unmapped ports (should be UINT32_MAX)
/// BUG-TransformerSignLoss: Transformer reflected voltage lost sign for negative ratio
///
/// NOTE: BUG-CodegenFinalizePhase and BUG-CodegenRefNode tests removed - they test
/// AOT codegen behavior which still uses the legacy iterative model. These will be restored
/// once the codegen is migrated to the push model.

#include <gtest/gtest.h>
#include <cmath>
#include "jit_solver/components/provider.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/components/all.h"
#include "jit_solver/state.h"

// =============================================================================
// BUG-AotDefault: AotProvider::get() must return UINT32_MAX for unmapped ports
// =============================================================================

TEST(BugAotDefault, UnmappedPortReturnsUINT32MAX) {
    // Previously returned 0 — valid index — silently corrupting signal[0].
    using Prov = AotProvider<
        Binding<PortNames::v_in, 5>,
        Binding<PortNames::v_out, 10>
    >;

    // Mapped ports return correct indices
    EXPECT_EQ(Prov::get(PortNames::v_in), 5u);
    EXPECT_EQ(Prov::get(PortNames::v_out), 10u);

    // Unmapped port must NOT return 0 — it must return sentinel UINT32_MAX
    EXPECT_EQ(Prov::get(PortNames::control), UINT32_MAX)
        << "AotProvider::get() must return UINT32_MAX for unmapped ports, not 0";
}

TEST(BugAotDefault, EmptyProviderReturnsUINT32MAX) {
    // Edge case: AotProvider with no bindings at all
    using Prov = AotProvider<>;
    EXPECT_EQ(Prov::get(PortNames::v_in), UINT32_MAX);
}

TEST(BugAotDefault, IndexZeroIsStillValidMapping) {
    // Critical: index 0 must be distinguishable from "unmapped".
    // Old code returned 0 for BOTH "mapped to 0" and "unmapped".
    using Prov = AotProvider<
        Binding<PortNames::v_in, 0>
    >;

    EXPECT_EQ(Prov::get(PortNames::v_in), 0u)
        << "Index 0 is a valid signal mapping";
    EXPECT_EQ(Prov::get(PortNames::v_out), UINT32_MAX)
        << "Unmapped port must return UINT32_MAX, not 0";
}

TEST(BugAotDefault, AotAndJitSentinelsMatch) {
    // AotProvider and JitProvider must use the same sentinel value
    using Prov = AotProvider<>;
    EXPECT_EQ(Prov::get(PortNames::v_in), JitProvider::UNMAPPED);
}

// =============================================================================
// BUG-TransformerSignLoss: Transformer must preserve ratio sign
// =============================================================================

// Helper: set up a 2-signal state and JitProvider-based Transformer for testing.
// Signal 0 = primary, signal 1 = secondary.
static Transformer<JitProvider> make_transformer_test(SimulationState& st,
                                                       float v_primary, float v_secondary,
                                                       float ratio) {
    st.values.resize(2, 0.0f);
    st.values[0] = v_primary;
    st.values[1] = v_secondary;
    st.signal_types.resize(2, {Domain::Electrical, false});
    st.dynamic_signals_count = 2;

    Transformer<JitProvider> xfmr;
    xfmr.ratio = ratio;
    xfmr.provider.set(PortNames::primary, 0);
    xfmr.provider.set(PortNames::secondary, 1);
    return xfmr;
}

// Note: In push model, Transformer simply computes the secondary voltage based on
// primary voltage and ratio. The 'through' array no longer exists.
// The push-visible behavior is that v_secondary = v_primary * ratio (for ideal transformer).

template <typename Comp>
void step_component(Comp& comp, SimulationState& st, double dt) {
    comp.execute(st, dt);
    comp.commit(st, dt);
}

TEST(BugTransformerSignLoss, NegativeRatioInvertsVoltage) {
    // A transformer with ratio = -1 should invert the voltage.
    // Push model: v_secondary = v_primary * ratio = 10 * -1 = -10V.

    SimulationState st;
    auto xfmr = make_transformer_test(st, 10.0f, 0.0f, -1.0f);

    step_component(xfmr, st, 1.0f / 60.0f);

    float secondary = st.values[1];
    EXPECT_FLOAT_EQ(secondary, -10.0f)
        << "Negative ratio must invert: v_secondary = v_primary * ratio";
}

TEST(BugTransformerSignLoss, PositiveRatioScalesVoltage) {
    // Positive ratio should scale the voltage.
    // Push model: v_secondary = v_primary * ratio = 24 * 0.5 = 12V.

    SimulationState st;
    auto xfmr = make_transformer_test(st, 24.0f, 0.0f, 0.5f);

    step_component(xfmr, st, 1.0f / 60.0f);

    float secondary = st.values[1];
    EXPECT_FLOAT_EQ(secondary, 12.0f)
        << "Positive ratio must scale: v_secondary = v_primary * ratio";
}

TEST(BugTransformerSignLoss, NearZeroRatioDoesNotCrash) {
    // ratio very close to zero: should not cause division by zero
    SimulationState st;
    auto xfmr = make_transformer_test(st, 0.0f, 10.0f, 0.0f);

    // Should not crash or produce NaN/Inf
    EXPECT_NO_FATAL_FAILURE(step_component(xfmr, st, 1.0f / 60.0f))
        << "Zero ratio must not cause division by zero";

    // Results must be finite (no inf/NaN)
    EXPECT_TRUE(std::isfinite(st.values[0])) << "Primary value must be finite";
    EXPECT_TRUE(std::isfinite(st.values[1])) << "Secondary value must be finite";
}
