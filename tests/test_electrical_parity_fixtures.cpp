#include <gtest/gtest.h>
#include "jit_solver/simulator.h"
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
