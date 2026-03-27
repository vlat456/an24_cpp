/// Regression tests for bugs found during comprehensive code review.
///
/// BUG-AotDefault: AotProvider::get() returned 0 for unmapped ports (should be UINT32_MAX)
/// BUG-ConvergenceOverrun: save_convergence_state() could overrun convergence_buffer
/// BUG-ConvergenceReadOOB: get_max_change()/has_converged() read OOB convergence_buffer
/// BUG-CodegenFinalizePhase: AOT codegen missing finalize_step for GidroAccumulator/FuelTank/RUG82
/// BUG-CodegenRefNode: AOT codegen skipped RefNode.solve_electrical() (no longer a no-op)
/// BUG-TransformerSignLoss: Transformer reflected voltage lost sign for negative ratio

#include <gtest/gtest.h>
#include <cmath>
#include "jit_solver/components/provider.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/components/all.h"
#include "jit_solver/state.h"
#include "codegen/codegen.h"
#include "test_execution_phases.h"

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
// BUG-ConvergenceOverrun: save_convergence_state() must not overrun buffer
// =============================================================================

TEST(BugConvergenceOverrun, SaveDoesNotOverrunWhenBufferIsSmaller) {
    SimulationState st;
    // Allocate 5 dynamic signals
    for (int i = 0; i < 5; ++i) {
        (void)st.allocate_signal(static_cast<float>(i), {Domain::Electrical, false});
    }
    // Resize convergence buffer to only 3 (simulating resize_buffers called early)
    st.resize_buffers(3);

    // This must NOT crash or corrupt memory
    // Old code did memcpy(buf, across, across.size() * sizeof(float))
    // which would write 5 floats into a 3-float buffer.
    EXPECT_NO_FATAL_FAILURE(st.save_convergence_state());
}

TEST(BugConvergenceOverrun, SaveCopiesOnlyDynamicSignalCount) {
    SimulationState st;
    // Allocate 3 dynamic + 2 fixed signals
    for (int i = 0; i < 3; ++i) {
        (void)st.allocate_signal(static_cast<float>(i + 1), {Domain::Electrical, false});
    }
    for (int i = 0; i < 2; ++i) {
        (void)st.allocate_signal(99.0f, {Domain::Electrical, true});
    }

    // Buffer for exactly dynamic_signals_count
    st.resize_buffers(st.dynamic_signals_count);

    st.save_convergence_state();

    // Only dynamic signals should be copied
    for (uint32_t i = 0; i < st.dynamic_signals_count; ++i) {
        EXPECT_FLOAT_EQ(st.convergence_buffer[i], st.across[i]);
    }
}

TEST(BugConvergenceOverrun, GetMaxChangeUsesOnlyDynamicRange) {
    SimulationState st;
    (void)st.allocate_signal(10.0f, {Domain::Electrical, false});
    (void)st.allocate_signal(20.0f, {Domain::Electrical, false});
    (void)st.allocate_signal(100.0f, {Domain::Electrical, true}); // fixed — must be ignored

    st.resize_buffers(st.dynamic_signals_count);
    st.save_convergence_state();

    // Change only the fixed signal — get_max_change must NOT see it
    st.across[2] = 200.0f;
    EXPECT_FLOAT_EQ(st.get_max_change(), 0.0f)
        << "get_max_change must only iterate dynamic signals";
}

// =============================================================================
// BUG-CodegenFinalizePhase: AOT codegen must emit finalize_step for ALL components
// =============================================================================

static auto make_devices_with_finalize_step_components() {
    std::vector<DeviceInstance> devices;
    std::unordered_map<std::string, uint32_t> port_to_signal;
    uint32_t next_sig = 0;

    auto add_device = [&](const std::string& name, const std::string& cls,
                          const std::string& domain,
                          std::initializer_list<std::string> port_names) {
        DeviceInstance dev;
        dev.name = name;
        dev.classname = cls;
        dev.params["domain"] = domain;
        for (const auto& p : port_names) {
            dev.ports[p] = {PortDirection::InOut, PortType::V, std::nullopt};
            port_to_signal[name + "." + p] = next_sig++;
        }
        dev.execution = test_exec::for_class(cls);
        devices.push_back(std::move(dev));
    };

    // Components that were missing from codegen's has_finalize_step set
    add_device("ga1", "GidroAccumulator", "Hydraulic", {"p_in", "p_out"});
    add_device("ft1", "FuelTank", "Hydraulic", {"flow_out", "level_out"});
    add_device("rug1", "RUG82", "Electrical", {"v_gen", "k_mod"});

    // Also verify existing ones still work
    add_device("sw1", "Switch", "Electrical", {"v_in", "v_out", "control", "state"});

    struct Result {
        std::vector<DeviceInstance> devices;
        std::vector<Connection> connections;
        std::unordered_map<std::string, uint32_t> port_to_signal;
        uint32_t signal_count;
    };
    return Result{std::move(devices), {}, std::move(port_to_signal), next_sig};
}

TEST(BugCodegenFinalizePhase, GidroAccumulatorFinalizePhaseEmitted) {
    auto [devices, connections, port_to_signal, signal_count] = make_devices_with_finalize_step_components();
    std::string source = CodeGen::generate_source(
        "test.h", devices, connections, port_to_signal, signal_count);

    EXPECT_NE(source.find("ga1.finalize_step"), std::string::npos)
        << "AOT codegen must emit finalize_step for GidroAccumulator (gas volume update)";
}

TEST(BugCodegenFinalizePhase, FuelTankFinalizePhaseEmitted) {
    auto [devices, connections, port_to_signal, signal_count] = make_devices_with_finalize_step_components();
    std::string source = CodeGen::generate_source(
        "test.h", devices, connections, port_to_signal, signal_count);

    EXPECT_NE(source.find("ft1.finalize_step"), std::string::npos)
        << "AOT codegen must emit finalize_step for FuelTank (fuel consumption)";
}

TEST(BugCodegenFinalizePhase, RUG82FinalizePhaseEmitted) {
    auto [devices, connections, port_to_signal, signal_count] = make_devices_with_finalize_step_components();
    std::string source = CodeGen::generate_source(
        "test.h", devices, connections, port_to_signal, signal_count);

    EXPECT_NE(source.find("rug1.finalize_step"), std::string::npos)
        << "AOT codegen must emit finalize_step for RUG82 (voltage regulator integration)";
}

TEST(BugCodegenFinalizePhase, SwitchFinalizePhaseStillEmitted) {
    auto [devices, connections, port_to_signal, signal_count] = make_devices_with_finalize_step_components();
    std::string source = CodeGen::generate_source(
        "test.h", devices, connections, port_to_signal, signal_count);

    // After control-commit migration, Switch should be emitted in commit phase
    // and must not rely on finalize finalize_step emission.
    EXPECT_NE(source.find("sw1.commit_control"), std::string::npos)
        << "Switch must be emitted in control-commit phase";
}

// =============================================================================
// BUG-CodegenRefNode: AOT codegen must call RefNode.solve_electrical()
// =============================================================================

TEST(BugCodegenRefNode, RefNodeSolveElectricalEmitted) {
    // After BUG-RefNode fix, RefNode::solve_electrical() stamps Norton residual.
    // Codegen must NOT skip it as a no-op.
    std::vector<DeviceInstance> devices;
    std::unordered_map<std::string, uint32_t> port_to_signal;

    DeviceInstance ref;
    ref.name = "gnd";
    ref.classname = "RefNode";
    ref.params["value"] = "0";
    ref.ports["v"] = {PortDirection::Out, PortType::V, std::nullopt};
    ref.execution = test_exec::electrical_passive();
    port_to_signal["gnd.v"] = 0;
    devices.push_back(std::move(ref));

    DeviceInstance bat;
    bat.name = "bat1";
    bat.classname = "Battery";
    bat.params["v_nominal"] = "24";
    bat.params["internal_r"] = "0.1";
    bat.ports["v_in"] = {PortDirection::In, PortType::V, std::nullopt};
    bat.ports["v_out"] = {PortDirection::Out, PortType::V, std::nullopt};
    bat.execution = test_exec::electrical_passive();
    port_to_signal["bat1.v_in"] = 1;
    port_to_signal["bat1.v_out"] = 2;
    devices.push_back(std::move(bat));

    std::string source = CodeGen::generate_source(
        "test.h", devices, {}, port_to_signal, 3);

    EXPECT_NE(source.find("gnd.solve_electrical"), std::string::npos)
        << "RefNode.solve_electrical() must be called (stamps Norton residual since BUG-RefNode fix)";
}

TEST(BugCodegenRefNode, BusStillSkipped) {
    // Bus is truly a no-op — verify it's still skipped
    std::vector<DeviceInstance> devices;
    std::unordered_map<std::string, uint32_t> port_to_signal;

    DeviceInstance bus;
    bus.name = "bus1";
    bus.classname = "Bus";
    bus.ports["v"] = {PortDirection::InOut, PortType::V, std::nullopt};
    bus.execution = test_exec::bus();
    port_to_signal["bus1.v"] = 0;
    devices.push_back(std::move(bus));

    std::string source = CodeGen::generate_source(
        "test.h", devices, {}, port_to_signal, 1);

    EXPECT_EQ(source.find("bus1.solve_electrical"), std::string::npos)
        << "Bus.solve_electrical() is a no-op and should be skipped in codegen";
}

// =============================================================================
// BUG-ConvergenceReadOOB: get_max_change()/has_converged() must not read OOB
// =============================================================================

TEST(BugConvergenceReadOOB, GetMaxChangeWithSmallerBuffer) {
    // If convergence_buffer.size() < dynamic_signals_count (e.g., buffer resized
    // before new signals were allocated), get_max_change() must NOT read past
    // convergence_buffer bounds.
    SimulationState st;

    // Allocate 5 dynamic signals
    for (int i = 0; i < 5; ++i) {
        (void)st.allocate_signal(static_cast<float>(i + 1), {Domain::Electrical, false});
    }
    // dynamic_signals_count == 5, but only allocate buffer for 3
    st.resize_buffers(3);
    st.save_convergence_state();

    // Mutate a signal within the buffer range — should be detected
    st.across[1] = 999.0f;
    float max_change = st.get_max_change();
    EXPECT_GT(max_change, 0.0f)
        << "get_max_change must detect changes within the buffer range";

    // Mutate a signal OUTSIDE the buffer range — must NOT be detected (OOB read)
    // Reset
    st.across[1] = 2.0f; // restore original
    st.save_convergence_state();
    st.across[4] = 999.0f; // signal at index 4, but buffer only has 3 entries
    max_change = st.get_max_change();
    EXPECT_FLOAT_EQ(max_change, 0.0f)
        << "get_max_change must NOT read past convergence_buffer bounds";
}

TEST(BugConvergenceReadOOB, HasConvergedWithSmallerBuffer) {
    // Same scenario for has_converged()
    SimulationState st;
    for (int i = 0; i < 5; ++i) {
        (void)st.allocate_signal(10.0f, {Domain::Electrical, false});
    }
    st.resize_buffers(3);
    st.save_convergence_state();

    // No changes — should converge
    EXPECT_TRUE(st.has_converged(0.01f));

    // Change within buffer range — should NOT converge
    st.across[2] = 999.0f;
    EXPECT_FALSE(st.has_converged(0.01f))
        << "has_converged must detect divergence within buffer range";

    // Change only outside buffer range — must still report converged (not read OOB)
    st.across[2] = 10.0f; // restore
    st.save_convergence_state();
    st.across[3] = 999.0f; // index 3, outside 3-element buffer
    EXPECT_TRUE(st.has_converged(0.01f))
        << "has_converged must NOT read past convergence_buffer bounds";
}

TEST(BugConvergenceReadOOB, EmptyBufferDoesNotCrash) {
    // Edge case: convergence_buffer is empty but dynamic_signals_count > 0
    SimulationState st;
    (void)st.allocate_signal(5.0f, {Domain::Electrical, false});
    // Don't call resize_buffers — convergence_buffer is empty

    EXPECT_NO_FATAL_FAILURE(st.save_convergence_state());
    EXPECT_FLOAT_EQ(st.get_max_change(), 0.0f);
    EXPECT_TRUE(st.has_converged(1.0f));
}

// =============================================================================
// BUG-TransformerSignLoss: Transformer must preserve ratio sign
// =============================================================================

// Helper: set up a 2-signal state and JitProvider-based Transformer for testing.
// Signal 0 = primary, signal 1 = secondary.
static Transformer<JitProvider> make_transformer_test(SimulationState& st,
                                                       float v_primary, float v_secondary,
                                                       float ratio) {
    (void)st.allocate_signal(v_primary, {Domain::Electrical, false});   // idx 0: primary
    (void)st.allocate_signal(v_secondary, {Domain::Electrical, false}); // idx 1: secondary

    st.resize_buffers(st.dynamic_signals_count);
    st.conductance.assign(st.across.size(), 0.0f);
    st.through.assign(st.across.size(), 0.0f);

    Transformer<JitProvider> xfmr;
    xfmr.ratio = ratio;
    xfmr.provider.set(PortNames::primary, 0);
    xfmr.provider.set(PortNames::secondary, 1);
    return xfmr;
}

TEST(BugTransformerSignLoss, NegativeRatioInvertsReflectedVoltage) {
    // A transformer with ratio = -1 should invert the voltage.
    // The reflected primary voltage V_primary_reflected = V_secondary / ratio.
    // For ratio = -1, if V_secondary = 10V, reflected should be -10V.
    //
    // Old code: safe_ratio = max(abs(ratio), 1e-6) = 1.0 (lost negative sign!)
    // This made V_primary_reflected = 10 / 1 = 10V (WRONG — should be -10V)

    SimulationState st;
    auto xfmr = make_transformer_test(st, 0.0f, 10.0f, -1.0f);
    xfmr.solve_electrical(st, 1.0f / 60.0f);

    // Primary through should be NEGATIVE (reflected voltage is negative,
    // pushing primary voltage DOWN from 0V).
    // Norton residual on primary = (V_secondary/ratio - V_primary) * g_primary
    //   = (10 / -1 - 0) * 1.0 = -10.0
    EXPECT_LT(st.through[0], 0.0f)
        << "Negative-ratio transformer must produce negative reflected voltage on primary";

    // Secondary side: v_primary * ratio = 0 * -1 = 0, so i_secondary = (0 - 10) * 1 = -10
    EXPECT_LT(st.through[1], 0.0f)
        << "Secondary should see negative residual driving it toward target";
}

TEST(BugTransformerSignLoss, PositiveRatioUnchanged) {
    // Positive ratio should work as before
    SimulationState st;
    auto xfmr = make_transformer_test(st, 24.0f, 0.0f, 0.5f);
    xfmr.solve_electrical(st, 1.0f / 60.0f);

    // Secondary target = 24 * 0.5 = 12V. Residual = (12 - 0) * 1 = 12 (positive)
    EXPECT_GT(st.through[1], 0.0f)
        << "Positive ratio should produce positive residual driving secondary toward target";
}

TEST(BugTransformerSignLoss, NearZeroRatioDoesNotDivideByZero) {
    // ratio very close to zero: safe_ratio must clamp to ±1e-6, not zero
    SimulationState st;
    auto xfmr = make_transformer_test(st, 0.0f, 10.0f, 0.0f);
    EXPECT_NO_FATAL_FAILURE(xfmr.solve_electrical(st, 1.0f / 60.0f))
        << "Zero ratio must not cause division by zero";

    // Results must be finite (no inf/NaN)
    EXPECT_TRUE(std::isfinite(st.through[0])) << "Primary through must be finite";
    EXPECT_TRUE(std::isfinite(st.through[1])) << "Secondary through must be finite";
    EXPECT_TRUE(std::isfinite(st.conductance[0])) << "Primary conductance must be finite";
    EXPECT_TRUE(std::isfinite(st.conductance[1])) << "Secondary conductance must be finite";
}
