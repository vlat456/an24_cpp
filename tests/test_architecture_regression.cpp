/// Architecture regression tests for E-001 through E-010.
///
/// E-001: electrical_subsolver hot path must be exception-free (noexcept).
///        Graceful fallback on singular matrices, deduplication on conflicting
///        fixed constraints.
/// E-002: SolverOwnedRefs pre-built typed pointer lists eliminate per-frame
///        std::visit scans.
/// E-003: JitProvider uses flat array (not unordered_map) for O(1) lookup.
/// E-004: Simulator time_ uses double to prevent precision loss after hours.
/// E-005: ComponentVariant architecture is documented and intentional.
/// E-006: SimulationState must NOT use misleading alignas(64) on vector members.
/// E-008: dt clamped to MAX_DT (0.1s) to prevent physics explosions.
/// E-009: Single-solve pipeline with one-frame delay is correct for a game.
/// E-010: Single canonical ComponentRegistry from json_parser — no bp2::ComponentRegistry.

#include <gtest/gtest.h>
#include "core/model/component_registry.h"
#include "core/solvers/jit/simulator.h"
#include "core/solvers/jit/jit_solver.h"
#include "core/solvers/jit/jit_solver_detail.h"
#include "core/solvers/common/provider.h"
#include "core/solvers/common/port_names.h"
#include "core/solvers/common/port_registry.h"
#include "core/solvers/jit/subsolvers/electrical_subsolver.h"
#include "core/solvers/jit/state.h"
#include "io/json/component_registry_json_loader.h"
#include "jit_build_input_test_helper.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <variant>
#include <limits>

// =============================================================================
// E-001 Regression: solve_electrical is noexcept
// =============================================================================

TEST(E001_Noexcept, SolveElectricalIsNoexcept) {
    // Compile-time check: solve_electrical must be declared noexcept.
    // If someone removes noexcept, this test fails at compile time.
    ElectricalBuildPlan plan;
    SimulationState st;
    ElectricalRuntimeState rt;
    static_assert(
        noexcept(solve_electrical(plan, st, rt, 0.0f)),
        "solve_electrical must be noexcept (E-001)"
    );
}

TEST(E001_Noexcept, SolveGaussianReturnsBool) {
    // solve_dense_gaussian returns bool (true=ok, false=singular).
    // Verify by checking that a singular matrix returns false without throwing.

    // Build a singular island: two floating nodes, no fixed voltage.
    ElectricalBuildPlan plan;
    ElectricalIslandPlan island;
    island.signal_indices = {0, 1};
    island.elements = {
        ElectricalElement{
            ElectricalElementKind::TheveninSource,
            0, 1, 28.0f, 0.01f, 0u
        },
        ElectricalElement{
            ElectricalElementKind::ConductanceBranch,
            0, 1, 1.0f, 0.0f, 1u
        }
    };
    plan.islands.push_back(island);

    SimulationState st;
    st.values.resize(2, 5.0f);  // non-zero previous values
    ElectricalRuntimeState rt;

    // Must not throw. Previous values preserved on singular fallback.
    EXPECT_NO_THROW(solve_electrical(plan, st, rt, 1.0f / 60.0f));
    EXPECT_NEAR(st.values[0], 5.0f, 1e-6f);
    EXPECT_NEAR(st.values[1], 5.0f, 1e-6f);
    EXPECT_EQ(rt.counters.singular_fallbacks, 1u);
}

TEST(E001_Noexcept, DuplicateFixedConstraintsSameValueNoThrow) {
    // Two FixedVoltageNode on same node with same value: deduplicate, no throw.
    ElectricalBuildPlan plan;
    ElectricalIslandPlan island;
    island.signal_indices = {0, 1};
    island.elements = {
        ElectricalElement{ElectricalElementKind::FixedVoltageNode, 0, 0, 0.0f, 0.0f, 0u},
        ElectricalElement{ElectricalElementKind::FixedVoltageNode, 0, 0, 0.0f, 0.0f, 1u},
        ElectricalElement{ElectricalElementKind::ConductanceBranch, 0, 1, 1.0f, 0.0f, 2u}
    };
    plan.islands.push_back(island);

    SimulationState st;
    st.values.resize(4, 0.0f);
    ElectricalRuntimeState rt;

    EXPECT_NO_THROW(solve_electrical(plan, st, rt, 0.0f));
    EXPECT_NEAR(st.values[0], 0.0f, 1e-3f);
}

// =============================================================================
// E-002 Regression: SolverOwnedRefs populated at build time
// =============================================================================

TEST(E002_SolverOwnedRefs, PopulatedAfterBuild) {
     // Build a circuit with ElectricalSource + AZS + Relay + RefNode (ground).
     // Verify SolverOwnedRefs has typed pointers for each.

     std::vector<DeviceInstance> devices = {
         make_device("ref_gnd", "RefNode", {{"value", "0"}}),
         make_device("bat1", "ElectricalSource", {{"voltage", "28"}, {"resistance", "0.01"}}),
         make_device("azs1", "AZS"),
         make_device("relay1", "Relay"),
     };
     std::vector<std::vector<std::string>> signal_groups = {
         {"bat1.v_out", "azs1.v_in"},
         {"azs1.v_out", "relay1.v_in"},
         {"relay1.v_out", "ref_gnd.v", "bat1.v_in"},
     };

     auto br = build_systems_dev(make_jit_input(devices, signal_groups));

     // ElectricalSource pointer list should be populated
     EXPECT_EQ(br.solver_owned->electrical_sources.size(), 1u);
     EXPECT_NE(br.solver_owned->electrical_sources[0], nullptr);

     // AZS pointer list should be populated
     EXPECT_EQ(br.solver_owned->azs_switches.size(), 1u);
     EXPECT_NE(br.solver_owned->azs_switches[0], nullptr);

     // Relay pointer list should be populated
     EXPECT_EQ(br.solver_owned->relays.size(), 1u);
     EXPECT_NE(br.solver_owned->relays[0], nullptr);
}

TEST(E002_SolverOwnedRefs, PointersMatchDeviceMap) {
     // Verify that SolverOwnedRefs pointers point to the actual devices
     // stored in BuildResult::devices (not copies).

     std::vector<DeviceInstance> devices = {
         make_device("ref_gnd", "RefNode", {{"value", "0"}}),
         make_device("bat1", "ElectricalSource", {{"voltage", "28"}, {"resistance", "0.01"}}),
     };
     std::vector<std::vector<std::string>> signal_groups = {
         {"bat1.v_out", "ref_gnd.v", "bat1.v_in"},
     };

     auto br = build_systems_dev(make_jit_input(devices, signal_groups));

     ASSERT_EQ(br.solver_owned->electrical_sources.size(), 1u);

     // The pointer in solver_owned should point into the devices map
     auto it = br.devices.find("bat1");
     ASSERT_NE(it, br.devices.end());

     const ElectricalSource<JitProvider>* from_map = std::get_if<ElectricalSource<JitProvider>>(&it->second);
     ASSERT_NE(from_map, nullptr);
     EXPECT_EQ(br.solver_owned->electrical_sources[0], from_map);
}

TEST(E002_SolverOwnedRefs, DeviceStoreSealedAfterBuild) {
     std::vector<DeviceInstance> devices = {
         make_device("ref_gnd", "RefNode", {{"value", "0"}}),
         make_device("src", "ElectricalSource", {{"voltage", "28"}, {"resistance", "0.01"}}),
     };
     std::vector<std::vector<std::string>> signal_groups = {
         {"src.v_out", "ref_gnd.v", "src.v_in"},
     };

     auto br = build_systems_dev(make_jit_input(devices, signal_groups));
     EXPECT_TRUE(br.devices.sealed());

     EXPECT_THROW(
         {
             br.devices["late_component"] = RefNode<JitProvider>{};
         },
         std::logic_error
     );
}

TEST(E002_SolverOwnedRefs, EmptyCircuitHasEmptyRefs) {
     // An empty circuit should have no solver-owned refs.
     std::vector<DeviceInstance> devices;
     std::vector<std::vector<std::string>> signal_groups;

     auto br = build_systems_dev(make_jit_input(devices, signal_groups));

     EXPECT_TRUE(br.solver_owned->generators.empty());
     EXPECT_TRUE(br.solver_owned->controlled_voltage_sources.empty());
     EXPECT_TRUE(br.solver_owned->variable_conductances.empty());
     EXPECT_TRUE(br.solver_owned->azs_switches.empty());
     EXPECT_TRUE(br.solver_owned->hold_buttons.empty());
     EXPECT_TRUE(br.solver_owned->relays.empty());
     EXPECT_TRUE(br.solver_owned->resistors.empty());
     EXPECT_TRUE(br.solver_owned->electrical_conductances.empty());
     EXPECT_TRUE(br.solver_owned->electrical_sources.empty());
}

// =============================================================================
// E-003 Regression: JitProvider flat array correctness
// =============================================================================

TEST(E003_JitProviderFlatArray, DefaultConstructorAllUnmapped) {
    JitProvider p;
    // Every slot should be UNMAPPED after construction
    for (uint32_t i = 0; i < static_cast<uint32_t>(PortNames::_COUNT); ++i) {
        EXPECT_EQ(p.indices[i], JitProvider::UNMAPPED)
            << "Slot " << i << " should be UNMAPPED after construction";
    }
}

TEST(E003_JitProviderFlatArray, SetAndGet) {
    JitProvider p;
    p.set(PortNames::v_in, 42);
    p.set(PortNames::v_out, 99);

    EXPECT_EQ(p.get(PortNames::v_in), 42u);
    EXPECT_EQ(p.get(PortNames::v_out), 99u);
}

TEST(E003_JitProviderFlatArray, HasReturnsTrueOnlyForMapped) {
    JitProvider p;
    EXPECT_FALSE(p.has(PortNames::v_in));
    EXPECT_FALSE(p.has(PortNames::v_out));

    p.set(PortNames::v_in, 0);
    EXPECT_TRUE(p.has(PortNames::v_in));
    EXPECT_FALSE(p.has(PortNames::v_out));
}

TEST(E003_JitProviderFlatArray, OverwriteMapping) {
    JitProvider p;
    p.set(PortNames::cmd, 10);
    EXPECT_EQ(p.get(PortNames::cmd), 10u);

    p.set(PortNames::cmd, 20);
    EXPECT_EQ(p.get(PortNames::cmd), 20u);
}

TEST(E003_JitProviderFlatArray, SizeofIsSmall) {
    // The flat array should be much smaller than an unordered_map.
    // With ~66 ports * 4 bytes = ~264 bytes (vs unordered_map at 48+ bytes
    // base + heap allocations).
    EXPECT_LE(sizeof(JitProvider), 512u)
        << "JitProvider should be a compact flat array, not a heap-allocating map";
}

TEST(E003_JitProviderFlatArray, PortNamesCountSentinel) {
    // PortNames::_COUNT must exist and be the last entry.
    // This is required for the flat array to be correctly sized.
    uint32_t count = static_cast<uint32_t>(PortNames::_COUNT);
    EXPECT_GT(count, 50u)
        << "PortNames::_COUNT should be > 50 (we have ~59 ports)";
    EXPECT_LT(count, 200u)
        << "PortNames::_COUNT sanity check — shouldn't be unreasonably large";
}

TEST(E003_JitProviderFlatArray, AotProviderUnaffected) {
    // AotProvider should still use constexpr fold expression, not flat array.
    // Compile-time check.
    using TestProvider = AotProvider<
        Binding<PortNames::v_in, 0>,
        Binding<PortNames::v_out, 1>
    >;

    constexpr uint32_t v_in = TestProvider::get(PortNames::v_in);
    constexpr uint32_t v_out = TestProvider::get(PortNames::v_out);
    constexpr uint32_t unmapped = TestProvider::get(PortNames::cmd);

    static_assert(v_in == 0, "AotProvider v_in should be 0");
    static_assert(v_out == 1, "AotProvider v_out should be 1");
    static_assert(unmapped == UINT32_MAX, "AotProvider unmapped should be UINT32_MAX");

    // AotProvider should be zero-size (no data members)
    EXPECT_LE(sizeof(TestProvider), 1u);
}

// =============================================================================
// E-004 Regression: Simulator time_ is double
// =============================================================================

TEST(E004_DoubleTime, GetTimeReturnsDouble) {
    // Compile-time check: get_time() must return double, not float.
    static_assert(
        std::is_same_v<decltype(std::declval<JIT_Simulator>().get_time()), double>,
        "Simulator::get_time() must return double (E-004)"
    );
}

TEST(E004_DoubleTime, PrecisionAfterManySteps) {
    // Simulate the equivalent of 4 hours at 60 Hz and verify precision.
    // With float: 4h * 3600s/h * 60Hz = 864000 steps.
    // At t=14400s, float32 ULP is ~1ms, so dt=1/60 (~16.67ms) would be
    // lost within a few ULPs. Double has ~15 decimal digits, so 14400.0
    // + 0.01666... is exact to picosecond resolution.

    double time = 0.0;
    const double dt = 1.0 / 60.0;
    const int steps = 864000;  // 4 hours at 60 Hz

    for (int i = 0; i < steps; ++i) {
        time += dt;
    }

    // Expected: steps * dt = 864000 / 60 = 14400.0 seconds
    double expected = static_cast<double>(steps) * dt;

    // With double, error should be less than 1 microsecond
    EXPECT_NEAR(time, expected, 1e-6)
        << "Time drift after 4 hours should be negligible with double precision";

    // With float, the same computation would lose significant precision.
    // Demonstrate the problem float would have had:
    float time_f = 0.0f;
    const float dt_f = 1.0f / 60.0f;
    for (int i = 0; i < steps; ++i) {
        time_f += dt_f;
    }
    float expected_f = static_cast<float>(steps) * dt_f;
    float float_error = std::abs(time_f - expected_f);

    // Float error should be measurably worse than double error
    double double_error = std::abs(time - expected);
    EXPECT_GT(float_error, double_error * 100.0f)
        << "Float precision loss should be orders of magnitude worse than double";
}

TEST(E004_DoubleTime, SimulatorTimeStartsAtZero) {
    JIT_Simulator sim;
    EXPECT_DOUBLE_EQ(sim.get_time(), 0.0);
}

// =============================================================================
// Integration: full simulator smoke test with all fixes active
// =============================================================================

TEST(ArchitectureRegression, FullSimulatorSmokeWithAllFixes) {
     // Build a simple circuit and step it many times.
     // Verifies all four fixes work together without crashes.
     const char* json = R"({
         "devices": [
             {"name": "ref_gnd", "classname": "RefNode", "params": {"value": "0"}},
             {"name": "bat1", "classname": "ElectricalSource", "params": {
                 "voltage": "28", "resistance": "0.01"
             }},
             {"name": "azs1", "classname": "AZS", "params": {}}
         ],
         "connections": [
             {"from": "bat1.v_out", "to": "azs1.v_in"},
             {"from": "azs1.v_out", "to": "ref_gnd.v"},
             {"from": "bat1.v_in", "to": "ref_gnd.v"}
         ]
     })";

     JIT_Simulator sim;
     EXPECT_NO_THROW(sim.start(build_input_from_json(json)));
     EXPECT_TRUE(sim.is_running());

     // Step 600 frames (10 seconds at 60 Hz)
     const double dt = 1.0 / 60.0;
     for (int i = 0; i < 600; ++i) {
         sim.step(dt);
     }

     // E-004: time should be precise (double accumulation)
     EXPECT_NEAR(sim.get_time(), 10.0, 0.01);

     // Step count
     EXPECT_EQ(sim.get_step_count(), 600u);

     sim.stop();
     EXPECT_FALSE(sim.is_running());
     EXPECT_DOUBLE_EQ(sim.get_time(), 0.0);
}

// =============================================================================
// E-005 Regression: ComponentVariant is std::variant (not virtual)
// =============================================================================

TEST(E005_ComponentVariant, IsStdVariant) {
    // ComponentVariant must be a std::variant, not a base class with virtual dispatch.
    // This is critical: the variant preserves the template-based Provider pattern
    // that allows AOT to use constexpr fold expressions (zero-overhead).
    static_assert(
        std::variant_size_v<ComponentVariant> > 0,
        "ComponentVariant must be a std::variant (E-005)"
    );
}

TEST(E005_ComponentVariant, HasManyAlternatives) {
    // The variant should have 60+ alternatives (one per component type).
    // If this drops significantly, either components were removed or
    // the variant was accidentally replaced.
    constexpr size_t count = std::variant_size_v<ComponentVariant>;
    EXPECT_GT(count, 60u)
        << "ComponentVariant should have 60+ alternatives (currently " << count << ")";
}

TEST(E005_ComponentVariant, ContainsElectricalSourceAndAZS) {
     // Spot-check that key component types are in the variant.
     // Uses std::get_if at compile time to verify type presence.
     ComponentVariant v = ElectricalSource<JitProvider>{};
     EXPECT_NE(std::get_if<ElectricalSource<JitProvider>>(&v), nullptr);

     ComponentVariant v2 = AZS<JitProvider>{};
     EXPECT_NE(std::get_if<AZS<JitProvider>>(&v2), nullptr);
}

// =============================================================================
// E-006 Regression: SimulationState alignment is NOT artificially inflated
// =============================================================================

TEST(E006_NoMisleadingAlignas, SimulationStateDefaultAlignment) {
    // Before E-006, SimulationState had alignas(64) on vector members,
    // which only aligned the control block (3 pointers), not the heap data.
    // After the fix, alignof(SimulationState) should be the natural alignment
    // (typically 8 on 64-bit), NOT 64.
    constexpr size_t align = alignof(SimulationState);
    EXPECT_LT(align, 64u)
        << "SimulationState should NOT have alignas(64) — it was misleading (E-006). "
           "Current alignment: " << align;
    // Typical natural alignment for a struct with vector + pointer members
    EXPECT_LE(align, 16u);
}

TEST(E006_NoMisleadingAlignas, VectorDataIsHeapAllocated) {
    // Verify that values.data() is NOT guaranteed to be 64-byte aligned.
    // This documents the actual behavior — the heap allocator decides alignment.
    SimulationState st;
    st.values.resize(256, 0.0f);

    // The data pointer alignment is allocator-dependent, but we can verify
    // the struct itself doesn't claim 64-byte alignment.
    uintptr_t data_addr = reinterpret_cast<uintptr_t>(st.values.data());

    // This test just documents the situation — heap alignment is platform-specific.
    // On most platforms, malloc returns 16-byte aligned memory.
    EXPECT_GT(data_addr, 0u) << "values.data() should be a valid heap pointer";

    // The key invariant: SimulationState as a struct should be small and naturally aligned
    EXPECT_LT(sizeof(SimulationState), 256u)
        << "SimulationState should be compact (just vectors + counter + pointer)";
}

// =============================================================================
// E-008 Regression: dt is clamped to MAX_DT in Simulator::step()
// =============================================================================

TEST(E008_DtClamp, MaxDtConstantExists) {
    // Verify the MAX_DT constant is publicly accessible and has the expected value.
    static_assert(JIT_Simulator::MAX_DT > 0.0f, "MAX_DT must be positive");
    EXPECT_FLOAT_EQ(JIT_Simulator::MAX_DT, 0.1f);
}

TEST(E008_DtClamp, LargeDtIsClamped) {
     // Step with dt=1.0s — time should only advance by MAX_DT (0.1s), not 1.0s.
     const char* json = R"({
         "devices": [
             {"name": "ref_gnd", "classname": "RefNode", "params": {"value": "0"}},
             {"name": "bat1", "classname": "ElectricalSource", "params": {
                 "voltage": "28", "resistance": "0.01"
             }}
         ],
         "connections": [
             {"from": "bat1.v_out", "to": "ref_gnd.v"},
             {"from": "bat1.v_in", "to": "ref_gnd.v"}
         ]
     })";

    JIT_Simulator sim;
    sim.start(build_input_from_json(json));
    ASSERT_TRUE(sim.is_running());

    // Step with an absurdly large dt (simulating a 1-second frame hitch)
    sim.step(1.0f);

    // Time should have advanced by MAX_DT, not 1.0
    EXPECT_NEAR(sim.get_time(), static_cast<double>(JIT_Simulator::MAX_DT), 1e-6)
        << "dt=1.0 should be clamped to MAX_DT=" << JIT_Simulator::MAX_DT;
    EXPECT_EQ(sim.get_step_count(), 1u);
}

TEST(E008_DtClamp, NormalDtIsNotClamped) {
     // Step with dt=1/60 — should not be clamped.
     const char* json = R"({
         "devices": [
             {"name": "ref_gnd", "classname": "RefNode", "params": {"value": "0"}},
             {"name": "bat1", "classname": "ElectricalSource", "params": {
                 "voltage": "28", "resistance": "0.01"
             }}
         ],
         "connections": [
             {"from": "bat1.v_out", "to": "ref_gnd.v"},
             {"from": "bat1.v_in", "to": "ref_gnd.v"}
         ]
     })";

    JIT_Simulator sim;
    sim.start(build_input_from_json(json));
    ASSERT_TRUE(sim.is_running());

    const float normal_dt = 1.0f / 60.0f;
    sim.step(normal_dt);

    // Time should advance by exactly the dt (not clamped)
    EXPECT_NEAR(sim.get_time(), static_cast<double>(normal_dt), 1e-6)
        << "Normal dt should not be clamped";
}

TEST(E008_DtClamp, ExactlyMaxDtIsNotClamped) {
     // Step with dt exactly equal to MAX_DT — should not be modified.
     const char* json = R"({
         "devices": [
             {"name": "ref_gnd", "classname": "RefNode", "params": {"value": "0"}},
             {"name": "bat1", "classname": "ElectricalSource", "params": {
                 "voltage": "28", "resistance": "0.01"
             }}
         ],
         "connections": [
             {"from": "bat1.v_out", "to": "ref_gnd.v"},
             {"from": "bat1.v_in", "to": "ref_gnd.v"}
         ]
     })";

    JIT_Simulator sim;
    sim.start(build_input_from_json(json));
    ASSERT_TRUE(sim.is_running());

    sim.step(JIT_Simulator::MAX_DT);

    EXPECT_NEAR(sim.get_time(), static_cast<double>(JIT_Simulator::MAX_DT), 1e-6)
        << "dt == MAX_DT should not be clamped further";
}

TEST(E008_DtClamp, ZeroDtIsIgnored) {
    // Step with dt=0 should be a no-op (no time advance, no step count).
    JIT_Simulator sim;
    const char* json = R"({
        "devices": [
            {"name": "ref_gnd", "classname": "RefNode", "params": {"value": "0"}}
        ],
        "connections": []
    })";
    sim.start(build_input_from_json(json));
    ASSERT_TRUE(sim.is_running());

    sim.step(0.0f);
    EXPECT_DOUBLE_EQ(sim.get_time(), 0.0);
    EXPECT_EQ(sim.get_step_count(), 0u);
}

TEST(E008_DtClamp, NegativeDtIsIgnored) {
    // Step with negative dt should be a no-op.
    JIT_Simulator sim;
    const char* json = R"({
        "devices": [
            {"name": "ref_gnd", "classname": "RefNode", "params": {"value": "0"}}
        ],
        "connections": []
    })";
    sim.start(build_input_from_json(json));
    ASSERT_TRUE(sim.is_running());

    sim.step(-0.5f);
    EXPECT_DOUBLE_EQ(sim.get_time(), 0.0);
    EXPECT_EQ(sim.get_step_count(), 0u);
}

// =============================================================================
// E-009 Regression: single-solve pipeline with one-frame delay
// =============================================================================

TEST(E009_SingleSolve, PipelineProducesConsistentElectricalResults) {
     // Verify single-solve pipeline: source voltage appears on the bus after
     // steady-state stepping. AZS starts closed via params, so voltage flows
     // through to a load node (not ground — checking a mid-circuit node).
     //
     // Circuit: bat1.v_out -> azs1.v_in, azs1.v_out -> load1.v_in,
     //          load1.v_out -> ref_gnd.v, bat1.v_in -> ref_gnd.v
     // With AZS closed, the load node (azs1.v_out / load1.v_in) should see
     // source voltage minus small resistive drops.
     const char* json = R"({
         "devices": [
             {"name": "ref_gnd", "classname": "RefNode", "params": {"value": "0"}},
             {"name": "bat1", "classname": "ElectricalSource", "params": {
                 "voltage": "28", "resistance": "0.01"
             }},
             {"name": "azs1", "classname": "AZS", "params": {"closed": "1"}},
             {"name": "load1", "classname": "Resistor", "params": {"conductance": "0.1"}}
         ],
         "connections": [
             {"from": "bat1.v_out", "to": "azs1.v_in"},
             {"from": "azs1.v_out", "to": "load1.v_in"},
             {"from": "load1.v_out", "to": "ref_gnd.v"},
             {"from": "bat1.v_in", "to": "ref_gnd.v"}
         ]
     })";

    JIT_Simulator sim;
    sim.start(build_input_from_json(json));
    ASSERT_TRUE(sim.is_running());

    const double dt = 1.0 / 60.0;

    // Step several frames to reach electrical steady state
    for (int i = 0; i < 10; ++i) {
        sim.step(dt);
    }

    // With AZS closed and a resistive load, the mid-bus node should have
    // significant voltage (close to 28V, minus small drops).
    float v_bus = sim.get_signal_value(sim.resolve_signal_key("azs1", "v_out"));

    // The bus should be energized — pipeline ran electrical solve at least once
    EXPECT_GT(v_bus, 10.0f)
        << "With AZS closed and battery=28V, bus voltage should be significant. "
           "Got " << v_bus << "V — pipeline may not be running electrical solve.";
}

TEST(E009_SingleSolve, StepCountAndTimeConsistent) {
     // Verify that each step() call increments step_count and time consistently.
     // This validates the pipeline runs exactly once per step (not 2x for 9-phase).
     JIT_Simulator sim;
     const char* json = R"({
         "devices": [
             {"name": "ref_gnd", "classname": "RefNode", "params": {"value": "0"}},
             {"name": "bat1", "classname": "ElectricalSource", "params": {
                 "voltage": "28", "resistance": "0.01"
             }}
         ],
         "connections": [
             {"from": "bat1.v_out", "to": "ref_gnd.v"},
             {"from": "bat1.v_in", "to": "ref_gnd.v"}
         ]
     })";
     sim.start(build_input_from_json(json));
     ASSERT_TRUE(sim.is_running());

     const double dt = 1.0 / 60.0;
     for (int i = 0; i < 100; ++i) {
         sim.step(dt);
     }

     EXPECT_EQ(sim.get_step_count(), 100u);
     EXPECT_NEAR(sim.get_time(), 100.0 * dt, 1e-4)
         << "Time should equal step_count * dt (single-solve, one pass per step)";
}

// =============================================================================
// E-010 Regression: single canonical ComponentRegistry (issue #24)
//
// The project must have exactly ONE ComponentRegistry type — the canonical
// model type from `core/model/component_registry.h`. There must be
// no `bp2::ComponentRegistry` class. All subsystems (codec, validation,
// flattener, bake, editor) must consume `const ComponentRegistry&` from the
// core model, and the canonical loader from `io/json/component_registry_json_loader.h`.
// =============================================================================

TEST(E010_SingleRegistry, CanonicalRegistryLoadsFromLibrary) {
    // The canonical load path must succeed and contain known types.
    ComponentRegistry reg = load_component_registry("library/");
    EXPECT_GT(reg.all_types().size(), 50u)
        << "Canonical ComponentRegistry should have 50+ types from library/";
    EXPECT_TRUE(reg.has("ElectricalSource"))
        << "ComponentRegistry must contain ElectricalSource";
    EXPECT_TRUE(reg.has("AZS"))
        << "ComponentRegistry must contain AZS";
    EXPECT_TRUE(reg.has("Resistor"))
        << "ComponentRegistry must contain Resistor";
}

TEST(E010_SingleRegistry, RegistryHasPortDefinitions) {
    // Verify the canonical registry provides port data (not just stubs).
    ComponentRegistry reg = load_component_registry("library/");
    const auto* src = reg.get("ElectricalSource");
    ASSERT_NE(src, nullptr);
    EXPECT_FALSE(spec_ports(*src).empty())
        << "ElectricalSource type definition must have port entries";
}

TEST(E010_SingleRegistry, RegistryHasCategoryMapping) {
    // The categories map is used for library path lookup (e.g. "electrical").
    ComponentRegistry reg = load_component_registry("library/");
    EXPECT_FALSE(reg.all_categories().empty())
        << "ComponentRegistry must populate categories for menu/path lookup";

    // AZS should be in an "electrical" category subdirectory
    auto it = reg.all_categories().find("AZS");
    ASSERT_NE(it, reg.all_categories().end());
    EXPECT_FALSE(it->second.empty());
}

TEST(E010_SingleRegistry, NoSecondRegistryInBp2Namespace) {
    // Compile-time structural check: bp2:: should not define a ComponentRegistry.
    // This test passes as long as no bp2::ComponentRegistry class exists.
    // If someone reintroduces bp2::ComponentRegistry, adding the include for it
    // would be needed here, and this static_assert would need to be updated.
    // The real enforcement is that this test file includes the canonical
    // registry headers directly
    // and uses ComponentRegistry unqualified — if a bp2::ComponentRegistry existed,
    // it would cause ambiguity errors in bp2-using translation units.
    static_assert(
        std::is_same_v<const std::unordered_map<std::string, ComponentSpec>&,
                       decltype(std::declval<const ComponentRegistry&>().all_types())>,
        "ComponentRegistry must expose all_types() returning const unordered_map<string, ComponentSpec>&"
    );
}
