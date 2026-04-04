#include <gtest/gtest.h>
#include "core/solvers/jit/simulator.h"
#include "core/solvers/aot/codegen.h"
#include "core/solvers/jit/jit_solver.h"
#include "core/solvers/jit/subsolvers/electrical_subsolver.h"
#include <cmath>

namespace {

float abs_error(float a, float b) {
    return std::fabs(a - b);
}

float rel_error(float a, float b) {
    float denom = std::fmax(std::fabs(a), std::fabs(b));
    if (denom < 1e-9f) return 0.0f;
    return std::fabs(a - b) / denom;
}

} // anonymous namespace

// ============================================================================
// JIT↔AOT Electrical Parity Fixtures
//
// These fixtures establish reference electrical solve behavior through the
// full JIT_Simulator pipeline. They test topologies that exercise all major
// semantic conventions documented in knowledge/22_electrical_semantics.md.
//
// Once AOT codegen emits solve_electrical() calls at the correct phase
// position, these same JSON topologies should produce numerically identical
// results when run through the AOT path. The AOT comparison is a planned
// Phase 1 follow-up; these fixtures serve as the reference baseline now.
//
// Fixture naming convention:
//   Parity_<Topology>_<Condition>
// ============================================================================

// ----------------------------------------------------------------------------
// Fixture 1: Simple Thevenin Divider
// Topology: ElectricalSource (V=28V, R=1Ω) -> Conductance (g=1S) -> RefNode (0V)
// Expected: V_junction = 14V (28V * 1/(1+1) = 14V)
// Semantics tested: TheveninSource Norton conversion, ConductanceBranch stamp,
//                   fixed-node boundary, KCL at junction node
// ----------------------------------------------------------------------------
TEST(ElectricalParityFixtures, SimpleTheveninDivider) {
    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "ElectricalSource", "params": {"voltage": "28.0", "resistance": "1.0"}},
            {"name": "load", "classname": "ElectricalConductance", "params": {"conductance": "1.0"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            "src.v_out -> load.v_in",
            "load.v_out -> gnd.v",
            "src.v_in -> gnd.v"
        ]
    })";

    JIT_Simulator sim;
    ASSERT_NO_THROW(sim.start_from_json(json));
    sim.step(1.0f / 60.0f);

    float v_gnd = sim.get_port_value("gnd", "v");
    float v_src_out = sim.get_port_value("src", "v_out");

    EXPECT_NEAR(v_gnd, 0.0f, 1e-4f) << "Ground node must hold at 0V";
    EXPECT_NEAR(v_src_out, 14.0f, 1e-3f) << "Junction voltage: Vth * g/(g+1/Rint) = 28 * 1/(1+1) = 14V";
    EXPECT_LT(rel_error(v_src_out, 14.0f), 1e-5f) << "Relative error < 1e-5";
}

// ----------------------------------------------------------------------------
// Fixture 2: Series Chain (two equal resistors)
// Topology: Source (V=28V, R=0) -> R1 (g=0.5S) -> R2 (g=0.5S) -> Ground
// Expected: V_middle = 14V, V_source = 28V, I_R1 = I_R2 = 7A
// Semantics tested: Series current continuity, voltage drop across consecutive
//                   ConductanceBranch stamps, node_to_unknown dense indexing
// ----------------------------------------------------------------------------
TEST(ElectricalParityFixtures, SeriesChainTwoResistors) {
    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "ElectricalSource", "params": {"voltage": "28.0", "resistance": "0.0"}},
            {"name": "r1", "classname": "ElectricalConductance", "params": {"conductance": "0.5"}},
            {"name": "r2", "classname": "ElectricalConductance", "params": {"conductance": "0.5"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            "src.v_out -> r1.v_in",
            "r1.v_out -> r2.v_in",
            "r2.v_out -> gnd.v",
            "src.v_in -> gnd.v"
        ]
    })";

    JIT_Simulator sim;
    ASSERT_NO_THROW(sim.start_from_json(json));
    sim.step(1.0f / 60.0f);

    float v_gnd = sim.get_port_value("gnd", "v");
    float v_r1_out = sim.get_port_value("r1", "v_out");
    float v_src_out = sim.get_port_value("src", "v_out");

    EXPECT_NEAR(v_gnd, 0.0f, 1e-4f);
    EXPECT_NEAR(v_r1_out, 14.0f, 1e-3f) << "Voltage drop: 28V across 2 equal resistors = 14V at middle";
    EXPECT_NEAR(v_src_out, 28.0f, 1e-3f) << "Ideal source must be at rated voltage";
}

// ----------------------------------------------------------------------------
// Fixture 3: Parallel Branch Split
// Topology: Source (V=24V, R=2Ω) -> node
//           node -> R_parallel1 (g=0.5S) -> ground
//           node -> R_parallel2 (g=0.5S) -> ground
// Expected: I_total = 24/2 = 12A, I_each_branch = 6A, V_node = 12V
// Semantics tested: Parallel conductance stamps, KCL at branch junction,
//                   multiple ConductanceBranch on same nodes
// ----------------------------------------------------------------------------
TEST(ElectricalParityFixtures, ParallelBranchSplit) {
    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "ElectricalSource", "params": {"voltage": "24.0", "resistance": "2.0"}},
            {"name": "r1", "classname": "ElectricalConductance", "params": {"conductance": "0.5"}},
            {"name": "r2", "classname": "ElectricalConductance", "params": {"conductance": "0.5"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            "src.v_out -> r1.v_in",
            "src.v_out -> r2.v_in",
            "r1.v_out -> gnd.v",
            "r2.v_out -> gnd.v",
            "src.v_in -> gnd.v"
        ]
    })";

    JIT_Simulator sim;
    ASSERT_NO_THROW(sim.start_from_json(json));
    sim.step(1.0f / 60.0f);

    float v_gnd = sim.get_port_value("gnd", "v");
    float v_src_out = sim.get_port_value("src", "v_out");

    EXPECT_NEAR(v_gnd, 0.0f, 1e-4f);
    // R_int=2Ω in series with parallel combination of two 2Ω resistors (eq R=1Ω)
    // Total R = 2 + 1 = 3Ω, I_total = 24/3 = 8A
    // V_node = I_total * 1Ω = 8V
    EXPECT_NEAR(v_src_out, 8.0f, 1e-3f) << "V_node: R_eq_parallel=1Ω, V=24*1/(2+1)=8V";
}

// ----------------------------------------------------------------------------
// Fixture 4: Multi-Island (two independent circuits)
// Topology:
//   Island A: Source(10V, R=1Ω) -> Conductance(g=1S) -> Ground
//   Island B: Source(24V, R=0) -> Conductance(g=2S) -> Ground
// Expected: Island A V = 5V, Island B V = 16V (independent solves)
// Semantics tested: Multiple island extraction, independent linear systems,
//                   island-by-island solve isolation
// ----------------------------------------------------------------------------
TEST(ElectricalParityFixtures, MultiIsland) {
    const std::string json = R"({
        "devices": [
            {"name": "src_a", "classname": "ElectricalSource", "params": {"voltage": "10.0", "resistance": "1.0"}},
            {"name": "load_a", "classname": "ElectricalConductance", "params": {"conductance": "1.0"}},
            {"name": "gnd_a", "classname": "RefNode", "params": {"value": "0.0"}},
            {"name": "src_b", "classname": "ElectricalSource", "params": {"voltage": "24.0", "resistance": "0.0"}},
            {"name": "load_b", "classname": "ElectricalConductance", "params": {"conductance": "2.0"}},
            {"name": "gnd_b", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            "src_a.v_out -> load_a.v_in",
            "load_a.v_out -> gnd_a.v",
            "src_a.v_in -> gnd_a.v",
            "src_b.v_out -> load_b.v_in",
            "load_b.v_out -> gnd_b.v",
            "src_b.v_in -> gnd_b.v"
        ]
    })";

    JIT_Simulator sim;
    ASSERT_NO_THROW(sim.start_from_json(json));
    sim.step(1.0f / 60.0f);

    float v_src_a = sim.get_port_value("src_a", "v_out");
    float v_src_b = sim.get_port_value("src_b", "v_out");

    // Island A: R_int=1Ω, g=1S (R=1Ω), Vth=10V
    // I = 10/(1+1) = 5A, V_node = I * 1 = 5V
    EXPECT_NEAR(v_src_a, 5.0f, 1e-3f) << "Island A: V = 10 * 1/(1+1) = 5V";
    // Island B: R_int=0 (ideal), g=2S (R=0.5Ω), Vth=24V
    // I = 24/0.5 = 48A, V_node = 24V (ideal source)
    EXPECT_NEAR(v_src_b, 24.0f, 1e-3f) << "Island B: ideal source at rated voltage";
}

// ----------------------------------------------------------------------------
// Fixture 5: Near-Short High Conductance
// Topology: Source (V=12V, R=0.001Ω) -> Conductance (g=1000S = R=0.001Ω) -> Ground
// Expected: V_node ≈ 6V (matched source resistance and load conductance)
// Semantics tested: Numerical stability with very high conductance (1e3S),
//                   large conductance ratio handling, float precision limits
// ----------------------------------------------------------------------------
TEST(ElectricalParityFixtures, NearShortHighConductance) {
    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "ElectricalSource", "params": {"voltage": "12.0", "resistance": "0.001"}},
            {"name": "load", "classname": "ElectricalConductance", "params": {"conductance": "1000.0"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            "src.v_out -> load.v_in",
            "load.v_out -> gnd.v",
            "src.v_in -> gnd.v"
        ]
    })";

    JIT_Simulator sim;
    ASSERT_NO_THROW(sim.start_from_json(json));
    sim.step(1.0f / 60.0f);

    float v_gnd = sim.get_port_value("gnd", "v");
    float v_src_out = sim.get_port_value("src", "v_out");

    EXPECT_NEAR(v_gnd, 0.0f, 1e-4f);
    // R_int = 0.001Ω, g = 1000S (R = 0.001Ω), perfectly matched
    // R_total = 0.001 + 0.001 = 0.002Ω
    // I = 12 / 0.002 = 6000A
    // V_node = I * 0.001 = 6V
    EXPECT_NEAR(v_src_out, 6.0f, 1e-2f) << "Matched source/load: V = 12/2 = 6V (1% tolerance for high-current float)";
}

// ============================================================================
// AOT Parity Summary
// ============================================================================
// These 5 fixtures cover the canonical electrical topologies documented in
// knowledge/22_electrical_semantics.md Phase 0 exit gate.
//
// When AOT codegen is updated to emit solve_electrical() calls (Phase 1 step 4),
// the same JSON inputs above should be run through the AOT path and produce
// numerically identical results. The target error is:
//   - max abs voltage error < 1e-3V
//   - max rel voltage error < 1e-5 relative
//   - max branch current error < 1e-3A
//   - KCL residual per island < 1e-10 absolute
// ============================================================================

// ============================================================================
// AOT Electrical Parity Tests
// ============================================================================
// These tests verify that extract_electrical_plan() (used by codegen) produces
// the same electrical plan and solve results as build_systems_dev() (JIT path).
// Both paths use the same shared solve_electrical() function, so plan parity
// implies result parity.
//
// Helper: run AOT path (extract_electrical_plan + solve_electrical)
static void run_aot_electrical(
    const std::vector<DeviceInstance>& devices,
    const PortToSignal& port_to_signal,
    ElectricalBuildPlan& out_plan,
    SimulationState& out_state,
    ElectricalRuntimeState& out_rt
) {
    // Extract plan using codegen's extract_electrical_plan
    ElectricalExtractOptions options;
    options.strict_port_resolution = true;
    options.warn_on_missing_ports = false;
    ElectricalPlanCodegen codegen_plan = extract_electrical_plan(devices, port_to_signal, options);

    // Convert ElectricalPlanCodegen to ElectricalBuildPlan (same as AotElectricalPlan constructor)
    out_plan.islands.clear();
    for (auto& island_cg : codegen_plan.islands) {
        ElectricalIslandPlan isl;
        isl.signal_indices = island_cg.signal_indices;
        for (auto& e : island_cg.elements) {
            isl.elements.push_back({
                static_cast<ElectricalElementKind>(static_cast<uint8_t>(e.kind)),
                e.node_a, e.node_b, e.value_a, e.value_b, e.component_index
            });
        }
        out_plan.islands.push_back(std::move(isl));
    }

    // Allocate signals — match JIT path's signal_count (max_signal + 1 + sentinel)
    uint32_t signal_count = 0;
    for (const auto& [port, sig] : port_to_signal) {
        signal_count = std::max(signal_count, sig + 1);
    }
    signal_count += 1;  // sentinel at end, matching build_systems_dev
    for (uint32_t i = 0; i < signal_count; ++i) {
        out_state.allocate_signal(0.0f, {Domain::Electrical, false});
    }

    // Pre-allocate scratch buffers (reserve to avoid reallocation)
    uint32_t max_nodes = 0, max_elems = 0, max_comp = 0;
    for (const auto& island : out_plan.islands) {
        max_nodes = std::max(max_nodes, (uint32_t)island.signal_indices.size());
        max_elems = std::max(max_elems, (uint32_t)island.elements.size());
        for (const auto& e : island.elements)
            max_comp = std::max(max_comp, e.component_index);
    }
    out_rt.branch_currents.reserve(max_comp + 1);
    out_rt.island_nodes.reserve(max_nodes);
    out_rt.fixed_nodes.reserve(max_elems);
    out_rt.fixed_voltages.reserve(max_nodes);
    out_rt.is_fixed.reserve(max_nodes);
    out_rt.node_to_unknown.reserve(max_nodes);
    out_rt.island_voltages.reserve(max_nodes);
    out_rt.scratch_matrix.reserve(static_cast<size_t>(max_nodes) * max_nodes);
    out_rt.scratch_rhs.reserve(max_nodes);

    // Run electrical solve
    solve_electrical(out_plan, out_state, out_rt, 1.0f / 60.0f);
}

// Helper: set RefNode initial values in SimulationState
static void set_refnode_values(
    SimulationState& st,
    const std::vector<DeviceInstance>& devices,
    const PortToSignal& port_to_signal
) {
    for (const auto& dev : devices) {
        if (dev.classname == "RefNode") {
            float value = 0.0f;
            auto it = dev.params.find("value");
            if (it != dev.params.end()) {
                value = std::stof(it->second);
            }
            std::string port_key = dev.name + ".v";
            auto it_sig = port_to_signal.find(port_key);
            if (it_sig != port_to_signal.end()) {
                st.values[it_sig->second] = value;
            }
        }
    }
}

// Helper: get voltage at a port from SimulationState
static float get_voltage(const SimulationState& st, const PortToSignal& port_to_signal,
                         const std::string& device_name, const std::string& port_name) {
    std::string key = device_name + "." + port_name;
    auto it = port_to_signal.find(key);
    if (it == port_to_signal.end()) return 0.0f;
    return st.values[it->second];
}

// ----------------------------------------------------------------------------
// AOT Parity Fixture 1: Simple Thevenin Divider
// ----------------------------------------------------------------------------
TEST(ElectricalAotParity, SimpleTheveninDivider) {
    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "ElectricalSource", "params": {"voltage": "28.0", "resistance": "1.0"}},
            {"name": "load", "classname": "ElectricalConductance", "params": {"conductance": "1.0"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            "src.v_out -> load.v_in",
            "load.v_out -> gnd.v",
            "src.v_in -> gnd.v"
        ]
    })";

    auto ctx = parse_json(json);
    std::vector<std::pair<std::string, std::string>> conn_pairs;
    for (const auto& c : ctx.connections) {
        conn_pairs.push_back({c.from, c.to});
    }

    // JIT path
    auto jit_result = build_systems_dev(ctx.devices, conn_pairs);
    SimulationState jit_state;
    for (uint32_t i = 0; i < jit_result.signal_count; ++i)
        jit_state.allocate_signal(0.0f, {Domain::Electrical, false});
    set_refnode_values(jit_state, ctx.devices, jit_result.port_to_signal);
    ElectricalRuntimeState jit_rt;
    solve_electrical(jit_result.electrical_plan, jit_state, jit_rt, 1.0f / 60.0f);

    // AOT path
    ElectricalBuildPlan aot_plan;
    SimulationState aot_state;
    ElectricalRuntimeState aot_rt;
    run_aot_electrical(ctx.devices, jit_result.port_to_signal,
                       aot_plan, aot_state, aot_rt);

    // Compare signal values
    ASSERT_EQ(jit_result.electrical_plan.islands.size(), aot_plan.islands.size());
    for (size_t i = 0; i < jit_result.signal_count && i < 100u; ++i) {
        if (jit_state.signal_types[i].is_fixed) continue; // Skip fixed signals
        EXPECT_NEAR(jit_state.values[i], aot_state.values[i], 1e-6f)
            << "Signal " << i << " voltage mismatch";
    }

    // Compare specific known voltages
    float jit_v_src_out = get_voltage(jit_state, jit_result.port_to_signal, "src", "v_out");
    float aot_v_src_out = get_voltage(aot_state, jit_result.port_to_signal, "src", "v_out");
    EXPECT_NEAR(jit_v_src_out, 14.0f, 1e-3f);
    EXPECT_NEAR(jit_v_src_out, aot_v_src_out, 1e-6f);
}

// ----------------------------------------------------------------------------
// AOT Parity Fixture 2: Series Chain Two Resistors
// ----------------------------------------------------------------------------
TEST(ElectricalAotParity, SeriesChainTwoResistors) {
    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "ElectricalSource", "params": {"voltage": "28.0", "resistance": "0.0"}},
            {"name": "r1", "classname": "ElectricalConductance", "params": {"conductance": "0.5"}},
            {"name": "r2", "classname": "ElectricalConductance", "params": {"conductance": "0.5"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            "src.v_out -> r1.v_in",
            "r1.v_out -> r2.v_in",
            "r2.v_out -> gnd.v",
            "src.v_in -> gnd.v"
        ]
    })";

    auto ctx = parse_json(json);
    std::vector<std::pair<std::string, std::string>> conn_pairs;
    for (const auto& c : ctx.connections) {
        conn_pairs.push_back({c.from, c.to});
    }

    auto jit_result = build_systems_dev(ctx.devices, conn_pairs);
    SimulationState jit_state;
    for (uint32_t i = 0; i < jit_result.signal_count; ++i)
        jit_state.allocate_signal(0.0f, {Domain::Electrical, false});
    set_refnode_values(jit_state, ctx.devices, jit_result.port_to_signal);
    ElectricalRuntimeState jit_rt;
    solve_electrical(jit_result.electrical_plan, jit_state, jit_rt, 1.0f / 60.0f);

    ElectricalBuildPlan aot_plan;
    SimulationState aot_state;
    ElectricalRuntimeState aot_rt;
    run_aot_electrical(ctx.devices, jit_result.port_to_signal,
                       aot_plan, aot_state, aot_rt);

    for (size_t i = 0; i < jit_result.signal_count && i < 100u; ++i) {
        if (jit_state.signal_types[i].is_fixed) continue;
        EXPECT_NEAR(jit_state.values[i], aot_state.values[i], 1e-6f)
            << "Signal " << i << " voltage mismatch";
    }

    float jit_v_r1 = get_voltage(jit_state, jit_result.port_to_signal, "r1", "v_out");
    float aot_v_r1 = get_voltage(aot_state, jit_result.port_to_signal, "r1", "v_out");
    EXPECT_NEAR(jit_v_r1, 14.0f, 1e-3f);
    EXPECT_NEAR(jit_v_r1, aot_v_r1, 1e-6f);
}

// ----------------------------------------------------------------------------
// AOT Parity Fixture 3: Parallel Branch Split
// ----------------------------------------------------------------------------
TEST(ElectricalAotParity, ParallelBranchSplit) {
    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "ElectricalSource", "params": {"voltage": "24.0", "resistance": "2.0"}},
            {"name": "r1", "classname": "ElectricalConductance", "params": {"conductance": "0.5"}},
            {"name": "r2", "classname": "ElectricalConductance", "params": {"conductance": "0.5"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            "src.v_out -> r1.v_in",
            "r1.v_out -> gnd.v",
            "src.v_out -> r2.v_in",
            "r2.v_out -> gnd.v",
            "src.v_in -> gnd.v"
        ]
    })";

    auto ctx = parse_json(json);
    std::vector<std::pair<std::string, std::string>> conn_pairs;
    for (const auto& c : ctx.connections) {
        conn_pairs.push_back({c.from, c.to});
    }

    auto jit_result = build_systems_dev(ctx.devices, conn_pairs);
    SimulationState jit_state;
    for (uint32_t i = 0; i < jit_result.signal_count; ++i)
        jit_state.allocate_signal(0.0f, {Domain::Electrical, false});
    set_refnode_values(jit_state, ctx.devices, jit_result.port_to_signal);
    ElectricalRuntimeState jit_rt;
    solve_electrical(jit_result.electrical_plan, jit_state, jit_rt, 1.0f / 60.0f);

    ElectricalBuildPlan aot_plan;
    SimulationState aot_state;
    ElectricalRuntimeState aot_rt;
    run_aot_electrical(ctx.devices, jit_result.port_to_signal,
                       aot_plan, aot_state, aot_rt);

    for (size_t i = 0; i < jit_result.signal_count && i < 100u; ++i) {
        if (jit_state.signal_types[i].is_fixed) continue;
        EXPECT_NEAR(jit_state.values[i], aot_state.values[i], 1e-6f)
            << "Signal " << i << " voltage mismatch";
    }
}

// ----------------------------------------------------------------------------
// AOT Parity Fixture 4: Multi-Island (two disconnected circuits)
// ----------------------------------------------------------------------------
TEST(ElectricalAotParity, MultiIsland) {
    const std::string json = R"({
        "devices": [
            {"name": "src1", "classname": "ElectricalSource", "params": {"voltage": "12.0", "resistance": "1.0"}},
            {"name": "load1", "classname": "ElectricalConductance", "params": {"conductance": "1.0"}},
            {"name": "gnd1", "classname": "RefNode", "params": {"value": "0.0"}},
            {"name": "src2", "classname": "ElectricalSource", "params": {"voltage": "24.0", "resistance": "2.0"}},
            {"name": "load2", "classname": "ElectricalConductance", "params": {"conductance": "2.0"}},
            {"name": "gnd2", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            "src1.v_out -> load1.v_in",
            "load1.v_out -> gnd1.v",
            "src1.v_in -> gnd1.v",
            "src2.v_out -> load2.v_in",
            "load2.v_out -> gnd2.v",
            "src2.v_in -> gnd2.v"
        ]
    })";

    auto ctx = parse_json(json);
    std::vector<std::pair<std::string, std::string>> conn_pairs;
    for (const auto& c : ctx.connections) {
        conn_pairs.push_back({c.from, c.to});
    }

    auto jit_result = build_systems_dev(ctx.devices, conn_pairs);
    SimulationState jit_state;
    for (uint32_t i = 0; i < jit_result.signal_count; ++i)
        jit_state.allocate_signal(0.0f, {Domain::Electrical, false});
    set_refnode_values(jit_state, ctx.devices, jit_result.port_to_signal);
    ElectricalRuntimeState jit_rt;
    solve_electrical(jit_result.electrical_plan, jit_state, jit_rt, 1.0f / 60.0f);

    ElectricalBuildPlan aot_plan;
    SimulationState aot_state;
    ElectricalRuntimeState aot_rt;
    run_aot_electrical(ctx.devices, jit_result.port_to_signal,
                       aot_plan, aot_state, aot_rt);

    ASSERT_EQ(jit_result.electrical_plan.islands.size(), aot_plan.islands.size());

    for (size_t i = 0; i < jit_result.signal_count && i < 100u; ++i) {
        if (jit_state.signal_types[i].is_fixed) continue;
        EXPECT_NEAR(jit_state.values[i], aot_state.values[i], 1e-6f)
            << "Signal " << i << " voltage mismatch";
    }
}

// ----------------------------------------------------------------------------
// AOT Parity Fixture 5: Near-Short High Conductance
// ----------------------------------------------------------------------------
TEST(ElectricalAotParity, NearShortHighConductance) {
    const std::string json = R"({
        "devices": [
            {"name": "src", "classname": "ElectricalSource", "params": {"voltage": "12.0", "resistance": "0.001"}},
            {"name": "load", "classname": "ElectricalConductance", "params": {"conductance": "1000.0"}},
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}}
        ],
        "connections": [
            "src.v_out -> load.v_in",
            "load.v_out -> gnd.v",
            "src.v_in -> gnd.v"
        ]
    })";

    auto ctx = parse_json(json);
    std::vector<std::pair<std::string, std::string>> conn_pairs;
    for (const auto& c : ctx.connections) {
        conn_pairs.push_back({c.from, c.to});
    }

    auto jit_result = build_systems_dev(ctx.devices, conn_pairs);
    SimulationState jit_state;
    for (uint32_t i = 0; i < jit_result.signal_count; ++i)
        jit_state.allocate_signal(0.0f, {Domain::Electrical, false});
    set_refnode_values(jit_state, ctx.devices, jit_result.port_to_signal);
    ElectricalRuntimeState jit_rt;
    solve_electrical(jit_result.electrical_plan, jit_state, jit_rt, 1.0f / 60.0f);

    ElectricalBuildPlan aot_plan;
    SimulationState aot_state;
    ElectricalRuntimeState aot_rt;
    run_aot_electrical(ctx.devices, jit_result.port_to_signal,
                       aot_plan, aot_state, aot_rt);

    for (size_t i = 0; i < jit_result.signal_count && i < 100u; ++i) {
        if (jit_state.signal_types[i].is_fixed) continue;
        EXPECT_NEAR(jit_state.values[i], aot_state.values[i], 1e-6f)
            << "Signal " << i << " voltage mismatch";
    }
}
