#include <gtest/gtest.h>
#include "jit_solver/subsolvers/electrical_subsolver.h"
#include "jit_solver/state.h"
#include <algorithm>
#include <cmath>

namespace {

// Helper to create an island plan with given nodes and elements
ElectricalIslandPlan make_island(
    std::vector<uint32_t> signal_indices,
    std::vector<ElectricalElement> elements
) {
    ElectricalIslandPlan island;
    island.signal_indices = std::move(signal_indices);
    island.elements = std::move(elements);
    return island;
}

// Helper to create a SimulationState with given signal count
SimulationState make_sim_state(size_t num_signals) {
    SimulationState st;
    st.values.resize(num_signals, 0.0f);
    st.signal_types.resize(num_signals, {Domain::Electrical, false});
    return st;
}

} // anonymous namespace

// ============================================================================
// Electrical Subsolver Tests - Batch 4
// Tests the actual electrical island solving algorithm
// ============================================================================

TEST(ElectricalSubsolver, SimpleTheveninDivider) {
    // Test: Vth=28V with Rint=1 ohm (g=1), Rload=1 ohm (g=1) to ground
    // Expected: V at node1 (the junction) = 28 * (1/(1+1)) = 14V
    //
    // Nodes: 0 = ground (fixed 0V), 1 = junction (unknown)
    // Elements: TheveninSource(28V, 1R) from 1->0, ConductanceBranch(1S) from 1->0

    ElectricalBuildPlan plan;
    plan.islands.push_back(make_island(
        {0, 1},  // signal indices
        {
            // Ground node 0, fixed 0V
            ElectricalElement{
                ElectricalElementKind::FixedVoltageNode,
                0, 0,   // node_a=0, node_b=0 (unused)
                0.0f, 0.0f,  // value_a=0V
                0u  // component_index=0
            },
            // TheveninSource: Vth=28, Rseries=1 from node 1 to node 0.
            ElectricalElement{
                ElectricalElementKind::TheveninSource,
                1, 0,
                28.0f, 1.0f,  // value_a=Vth=28, value_b=Rseries=1
                1u  // component_index=1
            },
            // ConductanceBranch: g=1S from node 1 to node 0 (load to ground)
            ElectricalElement{
                ElectricalElementKind::ConductanceBranch,
                1, 0,   // node_a=1, node_b=0
                1.0f, 0.0f,  // value_a=g=1
                2u  // component_index=2
            }
        }
    ));

    SimulationState st = make_sim_state(4);  // Extra space
    ElectricalRuntimeState rt;

    solve_electrical(plan, st, rt, 0.0f);

    // Node 0 should be 0V (ground)
    EXPECT_NEAR(st.values[0], 0.0f, 1e-3f);
    // Node 1 should be around 14V
    EXPECT_NEAR(st.values[1], 14.0f, 1e-3f);

    // Check branch currents
    EXPECT_EQ(rt.branch_currents.size(), 3u);
    // Thevenin branch current is stored as net Norton branch current.
    // With node_a=1, node_b=0: I = g*(14-0) - 28 = -14A.
    EXPECT_NEAR(rt.branch_currents[1], -14.0f, 1e-3f);

    ASSERT_EQ(rt.island_diagnostics.size(), 1u);
    EXPECT_TRUE(rt.island_diagnostics[0].solve_ok);
    EXPECT_EQ(rt.island_diagnostics[0].island_index, 0u);
    EXPECT_EQ(rt.island_diagnostics[0].unknown_count, 1u);
    EXPECT_LT(rt.island_diagnostics[0].max_abs_kcl_residual, 1e-4f);
}

TEST(ElectricalSubsolver, SeriesChainTwoResistors) {
    // Test: Battery 28V -> R1 (2 ohm) -> R2 (2 ohm) -> Ground
    // Expected: V at node1 (after R1) = 28 * (2/(2+2)) = 14V
    // Expected: V at node2 (after R2, i.e., ground junction) = 0V
    //
    // Nodes: 0 = ground (fixed 0V), 1 = after R1, 2 = battery output
    // Wait, let me reconsider: battery+ is node2, battery- is node0(ground)
    // R1 connects node2 to node1, R2 connects node1 to node0
    //
    // Actually simpler: node0=ground, node1=middle, node2=source
    // Thevenin at node2-node0: Vth=28, Rseries=0 (ideal source)
    // R1: node2-node1 with g=0.5 (R=2)
    // R2: node1-node0 with g=0.5 (R=2)

    ElectricalBuildPlan plan;
    plan.islands.push_back(make_island(
        {0, 1, 2},  // signal indices: ground, middle, source
        {
            // Ground node 0, fixed 0V
            ElectricalElement{
                ElectricalElementKind::FixedVoltageNode,
                0, 0,
                0.0f, 0.0f,
                0u
            },
            // TheveninSource: Vth=28, Rseries=0 from node 2 to node 0
            ElectricalElement{
                ElectricalElementKind::TheveninSource,
                2, 0,   // node_a=2 (source), node_b=0 (ground)
                28.0f, 0.0f,  // Vth=28, Rseries=0 (ideal)
                1u
            },
            // R1: g=0.5S (R=2) from node2 to node1
            ElectricalElement{
                ElectricalElementKind::ConductanceBranch,
                2, 1,
                0.5f, 0.0f,  // g=0.5
                2u
            },
            // R2: g=0.5S (R=2) from node1 to node0
            ElectricalElement{
                ElectricalElementKind::ConductanceBranch,
                1, 0,
                0.5f, 0.0f,  // g=0.5
                3u
            }
        }
    ));

    SimulationState st = make_sim_state(4);
    ElectricalRuntimeState rt;

    solve_electrical(plan, st, rt, 0.0f);

    // Ground = 0V
    EXPECT_NEAR(st.values[0], 0.0f, 1e-3f);
    // Middle node should be 14V
    EXPECT_NEAR(st.values[1], 14.0f, 1e-3f);
    // Source node should be 28V
    EXPECT_NEAR(st.values[2], 28.0f, 1e-3f);

    // Currents: through R1 should equal through R2 (series)
    // I = (28-14)/2 = 7A through each resistor
    EXPECT_NEAR(rt.branch_currents[2], 7.0f, 1e-3f);  // R1 current
    EXPECT_NEAR(rt.branch_currents[3], 7.0f, 1e-3f);  // R2 current
}

TEST(ElectricalSubsolver, ConflictingFixedConstraintsThrows) {
    // Test: Two FixedVoltageNode on same node with different values
    // Should throw runtime_error

    ElectricalBuildPlan plan;
    plan.islands.push_back(make_island(
        {0, 1},
        {
            // Node 0 fixed at 0V
            ElectricalElement{
                ElectricalElementKind::FixedVoltageNode,
                0, 0,
                0.0f, 0.0f,
                0u
            },
            // Node 0 also fixed at 5V - CONFLICT!
            ElectricalElement{
                ElectricalElementKind::FixedVoltageNode,
                0, 0,
                5.0f, 0.0f,
                1u
            },
            // Some element connecting to make it a valid island
            ElectricalElement{
                ElectricalElementKind::ConductanceBranch,
                0, 1,
                1.0f, 0.0f,
                2u
            }
        }
    ));

    SimulationState st = make_sim_state(4);
    ElectricalRuntimeState rt;

    EXPECT_THROW(solve_electrical(plan, st, rt, 0.0f), std::runtime_error);
}

TEST(ElectricalSubsolver, NegativeConductanceThrows) {
    // Test: ConductanceBranch with negative g should throw

    ElectricalBuildPlan plan;
    plan.islands.push_back(make_island(
        {0, 1},
        {
            // Ground node 0
            ElectricalElement{
                ElectricalElementKind::FixedVoltageNode,
                0, 0,
                0.0f, 0.0f,
                0u
            },
            // Negative conductance!
            ElectricalElement{
                ElectricalElementKind::ConductanceBranch,
                1, 0,
                -1.0f, 0.0f,  // g = -1 (invalid!)
                1u
            }
        }
    ));

    SimulationState st = make_sim_state(4);
    ElectricalRuntimeState rt;

    EXPECT_THROW(solve_electrical(plan, st, rt, 0.0f), std::runtime_error);
}

TEST(ElectricalSubsolver, BranchCurrentStoragePopulated) {
    // Test: Verify branch_currents vector is properly sized and populated
    // Circuit: node 0 ground, node 1 unknown
    // Thevenin from node 1 to node 0: Vth=10, Rseries=1
    // Conductance g=2 from node 1 to node 0
    // Conductance g=1 from node 1 to node 0
    //
    // Total conductance at node 1: g_internal(1) + g_load1(2) + g_load2(1) = 4S
    // KCL: 4*V1 = In = 10 => V1 = 2.5V
    //
    // Branch currents:
    // - Conductance g=2: I = 2*(2.5-0) = 5A (from 1 to 0)
    // - Conductance g=1: I = 1*(2.5-0) = 2.5A (from 1 to 0)
    // - Thevenin internal: I = 1*(0-2.5) = -2.5A (from 0 to 1, since Va=0 < Vb=2.5)

    ElectricalBuildPlan plan;
    plan.islands.push_back(make_island(
        {0, 1},
        {
            ElectricalElement{
                ElectricalElementKind::FixedVoltageNode,
                0, 0,
                0.0f, 0.0f,
                5u  // high component_index
            },
            // Thevenin from node 1 to node 0: Vth=10, Rseries=1
            ElectricalElement{
                ElectricalElementKind::TheveninSource,
                1, 0,
                10.0f, 1.0f,  // Vth=10V, Rseries=1R
                2u
            },
            // Conductance g=2 from node 1 to node 0
            ElectricalElement{
                ElectricalElementKind::ConductanceBranch,
                1, 0,
                2.0f, 0.0f,
                3u
            },
            // Conductance g=1 from node 1 to node 0
            ElectricalElement{
                ElectricalElementKind::ConductanceBranch,
                1, 0,
                1.0f, 0.0f,
                7u
            }
        }
    ));

    SimulationState st = make_sim_state(4);
    ElectricalRuntimeState rt;

    solve_electrical(plan, st, rt, 0.0f);

    // Should have resized to max+1 = 8
    EXPECT_EQ(rt.branch_currents.size(), 8u);

    // FixedVoltageNode at index 5 should have 0 current
    EXPECT_NEAR(rt.branch_currents[5], 0.0f, 1e-9f);

    // Node 1 should be at 2.5V (In / g_total = 10 / 4)
    EXPECT_NEAR(st.values[1], 2.5f, 1e-3f);

    // ConductanceBranch currents
    // Current through g=2: 2 * (2.5 - 0) = 5A
    EXPECT_NEAR(rt.branch_currents[3], 5.0f, 1e-3f);
    // Current through g=1: 1 * (2.5 - 0) = 2.5A
    EXPECT_NEAR(rt.branch_currents[7], 2.5f, 1e-3f);

    // TheveninSource branch current (net Norton form): g*(Va-Vb) - In.
    // Va=2.5, Vb=0, g=1, In=10 => 2.5 - 10 = -7.5A.
    EXPECT_NEAR(rt.branch_currents[2], -7.5f, 1e-3f);
}

TEST(ElectricalSubsolver, EmptyPlanClearsBranchCurrents) {
    ElectricalBuildPlan plan;
    SimulationState st = make_sim_state(4);
    ElectricalRuntimeState rt;
    rt.branch_currents = {1.0f, 2.0f, 3.0f};

    EXPECT_NO_THROW(solve_electrical(plan, st, rt, 0.0f));
    EXPECT_TRUE(rt.branch_currents.empty());
}

TEST(ElectricalSubsolver, AllNodesFixedNoSolveNeeded) {
    // Test: All nodes are fixed voltage - N=0 case should not attempt solve

    ElectricalBuildPlan plan;
    plan.islands.push_back(make_island(
        {0, 1},
        {
            ElectricalElement{
                ElectricalElementKind::FixedVoltageNode,
                0, 0,
                0.0f, 0.0f,
                0u
            },
            ElectricalElement{
                ElectricalElementKind::FixedVoltageNode,
                1, 1,
                5.0f, 0.0f,
                1u
            }
        }
    ));

    SimulationState st = make_sim_state(4);
    ElectricalRuntimeState rt;

    // Should not throw, even though no conductance elements
    EXPECT_NO_THROW(solve_electrical(plan, st, rt, 0.0f));

    // Voltages should be written
    EXPECT_NEAR(st.values[0], 0.0f, 1e-3f);
    EXPECT_NEAR(st.values[1], 5.0f, 1e-3f);
}

TEST(ElectricalSubsolver, VoltageWritebackToSimulationState) {
    // Test: Solved voltages are correctly written to st.values

    ElectricalBuildPlan plan;
    plan.islands.push_back(make_island(
        {10, 20, 30},  // Non-contiguous signal indices
        {
            ElectricalElement{
                ElectricalElementKind::FixedVoltageNode,
                10, 10,
                0.0f, 0.0f,
                0u
            },
            ElectricalElement{
                ElectricalElementKind::TheveninSource,
                30, 10,
                24.0f, 2.0f,  // Vth=24, R=2
                1u
            },
            ElectricalElement{
                ElectricalElementKind::ConductanceBranch,
                30, 20,
                1.0f, 0.0f,  // R=1 ohm from node30 to node20
                2u
            },
            ElectricalElement{
                ElectricalElementKind::ConductanceBranch,
                20, 10,
                1.0f, 0.0f,  // R=1 ohm from node20 to node10
                3u
            }
        }
    ));

    // Create state with extra space
    SimulationState st;
    st.values.resize(50, 999.0f);  // Initialize with sentinel
    st.signal_types.resize(50, {Domain::Electrical, false});

    ElectricalRuntimeState rt;

    solve_electrical(plan, st, rt, 0.0f);

    // Only signal indices 10, 20, 30 should be modified
    EXPECT_NEAR(st.values[10], 0.0f, 1e-3f);   // Ground
    // V at node30 = Vth - I*Rseries = 24 - 6*2 = 12V
    EXPECT_NEAR(st.values[30], 12.0f, 1e-3f);
    // V at node20 = Vth - I*(Rseries+R1) = 24 - 6*3 = 6V
    EXPECT_NEAR(st.values[20], 6.0f, 1e-3f);

    // Other indices should be untouched (sentinel 999)
    EXPECT_EQ(st.values[0], 999.0f);
    EXPECT_EQ(st.values[5], 999.0f);
}

TEST(ElectricalSubsolver, SignalIndexOutOfRangeThrows) {
    // Test: If signal_indices contains index >= st.values.size(), throw

    ElectricalBuildPlan plan;
    plan.islands.push_back(make_island(
        {100},  // Only node 100
        {
            ElectricalElement{
                ElectricalElementKind::FixedVoltageNode,
                100, 100,
                0.0f, 0.0f,
                0u
            }
        }
    ));

    SimulationState st = make_sim_state(10);  // Only 10 signals (0-9)
    ElectricalRuntimeState rt;

    EXPECT_THROW(solve_electrical(plan, st, rt, 0.0f), std::runtime_error);
}

TEST(ElectricalSubsolver, SingularIslandDoesNotThrowAndKeepsPreviousState) {
    ElectricalBuildPlan plan;
    ElectricalIslandPlan island;

    // Two-node floating island (no FixedVoltageNode): singular by construction.
    island.signal_indices = {0, 1};
    island.elements = {
        ElectricalElement{
            ElectricalElementKind::TheveninSource,
            0, 1,
            28.0f, 0.01f,
            0u
        },
        ElectricalElement{
            ElectricalElementKind::ConductanceBranch,
            0, 1,
            1.0f, 0.0f,
            1u
        }
    };
    plan.islands.push_back(island);

    SimulationState st = make_sim_state(2);
    st.values[0] = 12.5f;
    st.values[1] = -3.0f;

    ElectricalRuntimeState rt;

    EXPECT_NO_THROW(solve_electrical(plan, st, rt, 1.0f / 60.0f));

    // On singular fallback, previous voltages are preserved.
    EXPECT_NEAR(st.values[0], 12.5f, 1e-6f);
    EXPECT_NEAR(st.values[1], -3.0f, 1e-6f);

    // Branch currents are zeroed for failed solve.
    ASSERT_GE(rt.branch_currents.size(), 2u);
    EXPECT_FLOAT_EQ(rt.branch_currents[0], 0.0f);
    EXPECT_FLOAT_EQ(rt.branch_currents[1], 0.0f);
}

TEST(ElectricalSubsolver, SpecializedN1SolveMatchesExpectedDivider) {
    // Single unknown node (N=1): source + load to fixed ground.
    // This path should use the specialized N==1 solve branch.
    ElectricalBuildPlan plan;
    plan.islands.push_back(make_island(
        {0, 1},
        {
            ElectricalElement{ElectricalElementKind::FixedVoltageNode, 0, 0, 0.0f, 0.0f, 0u},
            ElectricalElement{ElectricalElementKind::TheveninSource, 1, 0, 28.0f, 1.0f, 1u},
            ElectricalElement{ElectricalElementKind::ConductanceBranch, 1, 0, 1.0f, 0.0f, 2u}
        }
    ));

    SimulationState st = make_sim_state(4);
    ElectricalRuntimeState rt;

    solve_electrical(plan, st, rt, 0.0f);

    EXPECT_NEAR(st.values[1], 14.0f, 1e-3f);
    ASSERT_EQ(rt.island_diagnostics.size(), 1u);
    EXPECT_TRUE(rt.island_diagnostics[0].solve_ok);
    EXPECT_EQ(rt.island_diagnostics[0].unknown_count, 1u);
}

TEST(ElectricalSubsolver, SpecializedN2SolveMatchesSeriesChain) {
    // Two unknown nodes (N=2): ideal source node + middle node against fixed ground.
    // Topology:
    //   node2 --R1(2ohm)-- node1 --R2(2ohm)-- node0(gnd)
    //   source Vth=28V, Rseries=1 between node2 and node0
    // Expected: node2=22.4V, node1=11.2V.
    ElectricalBuildPlan plan;
    plan.islands.push_back(make_island(
        {0, 1, 2},
        {
            ElectricalElement{ElectricalElementKind::FixedVoltageNode, 0, 0, 0.0f, 0.0f, 0u},
            ElectricalElement{ElectricalElementKind::TheveninSource, 2, 0, 28.0f, 1.0f, 1u},
            ElectricalElement{ElectricalElementKind::ConductanceBranch, 2, 1, 0.5f, 0.0f, 2u},
            ElectricalElement{ElectricalElementKind::ConductanceBranch, 1, 0, 0.5f, 0.0f, 3u}
        }
    ));

    SimulationState st = make_sim_state(5);
    ElectricalRuntimeState rt;

    solve_electrical(plan, st, rt, 0.0f);

    EXPECT_NEAR(st.values[2], 22.4f, 1e-3f);
    EXPECT_NEAR(st.values[1], 11.2f, 1e-3f);
    ASSERT_EQ(rt.island_diagnostics.size(), 1u);
    EXPECT_TRUE(rt.island_diagnostics[0].solve_ok);
    EXPECT_EQ(rt.island_diagnostics[0].unknown_count, 2u);
    EXPECT_LT(rt.island_diagnostics[0].max_abs_kcl_residual, 1e-4f);
}

TEST(ElectricalSubsolver, SolveCountersTrackSpecializedPaths) {
    ElectricalBuildPlan plan;

    // Island 0: N==0 (all fixed)
    plan.islands.push_back(make_island(
        {0},
        {
            ElectricalElement{ElectricalElementKind::FixedVoltageNode, 0, 0, 0.0f, 0.0f, 0u}
        }
    ));

    // Island 1: N==1
    plan.islands.push_back(make_island(
        {1, 2},
        {
            ElectricalElement{ElectricalElementKind::FixedVoltageNode, 1, UINT32_MAX, 0.0f, 0.0f, 1u},
            ElectricalElement{ElectricalElementKind::TheveninSource, 2, 1, 28.0f, 1.0f, 2u},
            ElectricalElement{ElectricalElementKind::ConductanceBranch, 2, 1, 1.0f, 0.0f, 3u}
        }
    ));

    // Island 2: N==2
    plan.islands.push_back(make_island(
        {3, 4, 5},
        {
            ElectricalElement{ElectricalElementKind::FixedVoltageNode, 3, UINT32_MAX, 0.0f, 0.0f, 4u},
            ElectricalElement{ElectricalElementKind::TheveninSource, 5, 3, 28.0f, 1.0f, 5u},
            ElectricalElement{ElectricalElementKind::ConductanceBranch, 5, 4, 0.5f, 0.0f, 6u},
            ElectricalElement{ElectricalElementKind::ConductanceBranch, 4, 3, 0.5f, 0.0f, 7u}
        }
    ));

    // Island 3: singular floating N==2 -> fallback
    plan.islands.push_back(make_island(
        {6, 7},
        {
            ElectricalElement{ElectricalElementKind::TheveninSource, 6, 7, 28.0f, 0.01f, 8u},
            ElectricalElement{ElectricalElementKind::ConductanceBranch, 6, 7, 1.0f, 0.0f, 9u}
        }
    ));

    SimulationState st = make_sim_state(10);
    st.values[6] = 1.0f;
    st.values[7] = -1.0f;

    ElectricalRuntimeState rt;
    solve_electrical(plan, st, rt, 0.0f);

    EXPECT_EQ(rt.counters.islands_total, 4u);
    EXPECT_EQ(rt.counters.solves_n0, 1u);
    EXPECT_EQ(rt.counters.solves_n1, 1u);
    EXPECT_EQ(rt.counters.solves_n2, 2u);
    EXPECT_EQ(rt.counters.solves_dense, 0u);
    EXPECT_EQ(rt.counters.singular_fallbacks, 1u);
}

TEST(ElectricalSubsolver, SolveCountersTrackDensePathForN3) {
    ElectricalBuildPlan plan;
    // N==3 island (nodes 1,2,3 unknown; node0 fixed).
    plan.islands.push_back(make_island(
        {0, 1, 2, 3},
        {
            ElectricalElement{ElectricalElementKind::FixedVoltageNode, 0, UINT32_MAX, 0.0f, 0.0f, 0u},
            ElectricalElement{ElectricalElementKind::TheveninSource, 3, 0, 28.0f, 1.0f, 1u},
            ElectricalElement{ElectricalElementKind::ConductanceBranch, 3, 2, 1.0f, 0.0f, 2u},
            ElectricalElement{ElectricalElementKind::ConductanceBranch, 2, 1, 1.0f, 0.0f, 3u},
            ElectricalElement{ElectricalElementKind::ConductanceBranch, 1, 0, 1.0f, 0.0f, 4u}
        }
    ));

    SimulationState st = make_sim_state(8);
    ElectricalRuntimeState rt;
    solve_electrical(plan, st, rt, 0.0f);

    EXPECT_EQ(rt.counters.islands_total, 1u);
    EXPECT_EQ(rt.counters.solves_n0, 0u);
    EXPECT_EQ(rt.counters.solves_n1, 0u);
    EXPECT_EQ(rt.counters.solves_n2, 0u);
    EXPECT_EQ(rt.counters.solves_dense, 1u);
    EXPECT_EQ(rt.counters.singular_fallbacks, 0u);
    ASSERT_EQ(rt.island_diagnostics.size(), 1u);
    EXPECT_TRUE(rt.island_diagnostics[0].solve_ok);
    EXPECT_EQ(rt.island_diagnostics[0].unknown_count, 3u);
}
