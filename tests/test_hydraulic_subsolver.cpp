#include <gtest/gtest.h>
#include "core/solvers/jit/subsolvers/hydraulic_subsolver.h"
#include "core/solvers/jit/state.h"
#include <algorithm>
#include <cmath>

namespace {

HydraulicIslandPlan make_island(
    std::vector<uint32_t> signal_indices,
    std::vector<HydraulicElement> elements
) {
    HydraulicIslandPlan island;
    island.signal_indices = std::move(signal_indices);
    island.elements = std::move(elements);
    return island;
}

SimulationState make_sim_state(size_t num_signals) {
    SimulationState st;
    st.values.resize(num_signals, 0.0f);
    return st;
}

} // anonymous namespace

// ============================================================================
// Hydraulic Subsolver Tests
// Mirrors electrical_subsolver_tests with pressure/flow physics.
// ============================================================================

TEST(HydraulicSubsolver, SimplePressureDivider) {
    // PressureSource P_th=7.65 kPa, R_int=0.1 (g=10), load g=10 to atm.
    // Expected: P at node1 = 7.65 * (10/(10+10)) = 3.825 kPa
    HydraulicBuildPlan plan;
    plan.islands.push_back(make_island(
        {0, 1},
        {
            // Atmospheric ground node, fixed P=0
            HydraulicElement{HydraulicElementKind::FixedPressureNode, 0, 0, 0.0f, 0.0f, 0u},
            // PressureSource: P_th=7.65, R_int=0.1 from node 1 to node 0
            HydraulicElement{HydraulicElementKind::PressureSource, 1, 0, 7.65f, 0.1f, 1u},
            // FlowBranch: g=10 from node 1 to node 0 (load)
            HydraulicElement{HydraulicElementKind::FlowBranch, 1, 0, 10.0f, 0.0f, 2u}
        }
    ));

    SimulationState st = make_sim_state(4);
    HydraulicRuntimeState rt;
    rt.enable_diagnostics = true;

    solve_hydraulic(plan, st, rt, 0.0);

    EXPECT_NEAR(st.values[0], 0.0f, 1e-3f);
    EXPECT_NEAR(st.values[1], 3.825f, 0.01f);

    // Source branch flow (Norton): g*(P1-P0) - Qn = 10*(3.825-0) - 76.5 = -38.25
    EXPECT_NEAR(rt.branch_flows[1], -38.25f, 0.1f);

    ASSERT_EQ(rt.island_diagnostics.size(), 1u);
    EXPECT_TRUE(rt.island_diagnostics[0].solve_ok);
    EXPECT_EQ(rt.island_diagnostics[0].unknown_count, 1u);
    EXPECT_LT(rt.island_diagnostics[0].max_abs_kcl_residual, 1e-4f);
}

TEST(HydraulicSubsolver, SeriesOrificeChain) {
    // P_src=7.65 kPa, R_int=0.1 (g=10) from node 2 to node 0 (atm)
    // Valve: g=10 from node 2 to node 1
    // Drain orifice: g=5 from node 1 to node 0
    //
    // KCL node 2: (g_src+g_valve)*P2 - g_valve*P1 = Q_src
    // KCL node 1: -g_valve*P2 + (g_valve+g_drain)*P1 = 0
    // [20 -10][P2] = [76.5]
    // [-10 15][P1]   [ 0  ]
    // det=200, P2=5.7375, P1=3.825
    HydraulicBuildPlan plan;
    plan.islands.push_back(make_island(
        {0, 1, 2},
        {
            HydraulicElement{HydraulicElementKind::FixedPressureNode, 0, 0, 0.0f, 0.0f, 0u},
            HydraulicElement{HydraulicElementKind::PressureSource, 2, 0, 7.65f, 0.1f, 1u},
            HydraulicElement{HydraulicElementKind::FlowBranch, 2, 1, 10.0f, 0.0f, 2u},
            HydraulicElement{HydraulicElementKind::FlowBranch, 1, 0, 5.0f, 0.0f, 3u}
        }
    ));

    SimulationState st = make_sim_state(4);
    HydraulicRuntimeState rt;

    solve_hydraulic(plan, st, rt, 0.0);

    EXPECT_NEAR(st.values[0], 0.0f, 1e-3f);
    EXPECT_NEAR(st.values[1], 3.825f, 0.01f);
    EXPECT_NEAR(st.values[2], 5.7375f, 0.01f);

    // Flow conservation: all branches should carry ~19.125 L/s
    EXPECT_NEAR(rt.branch_flows[2], 19.125f, 0.1f);  // valve flow
    EXPECT_NEAR(rt.branch_flows[3], 19.125f, 0.1f);  // drain flow
}

TEST(HydraulicSubsolver, DuplicateFixedPressureDeduplicatedSilently) {
    HydraulicBuildPlan plan;
    plan.islands.push_back(make_island(
        {0, 1},
        {
            HydraulicElement{HydraulicElementKind::FixedPressureNode, 0, 0, 0.0f, 0.0f, 0u},
            HydraulicElement{HydraulicElementKind::FixedPressureNode, 0, 0, 0.0f, 0.0f, 1u},
            HydraulicElement{HydraulicElementKind::FlowBranch, 0, 1, 1.0f, 0.0f, 2u}
        }
    ));

    SimulationState st = make_sim_state(4);
    HydraulicRuntimeState rt;

    EXPECT_NO_THROW(solve_hydraulic(plan, st, rt, 0.0));
    EXPECT_NEAR(st.values[0], 0.0f, 1e-3f);
}

TEST(HydraulicSubsolver, ZeroConductanceSingularFallback) {
    HydraulicBuildPlan plan;
    plan.islands.push_back(make_island(
        {0, 1},
        {
            HydraulicElement{HydraulicElementKind::FixedPressureNode, 0, 0, 0.0f, 0.0f, 0u},
            HydraulicElement{HydraulicElementKind::FlowBranch, 1, 0, 0.0f, 0.0f, 1u}
        }
    ));

    SimulationState st = make_sim_state(4);
    st.values[1] = 5.0f;
    HydraulicRuntimeState rt;

    EXPECT_NO_THROW(solve_hydraulic(plan, st, rt, 0.0));
    EXPECT_NEAR(st.values[1], 5.0f, 1e-3f);
    EXPECT_EQ(rt.counters.singular_fallbacks, 1u);
}

TEST(HydraulicSubsolver, BranchFlowStoragePopulated) {
    // PressureSource P_th=10, R_int=0.5 (g=2, Qn=20), load1 g=2, load2 g=1
    // Total g=5, P1=20/5=4 kPa
    HydraulicBuildPlan plan;
    plan.islands.push_back(make_island(
        {0, 1},
        {
            HydraulicElement{HydraulicElementKind::FixedPressureNode, 0, 0, 0.0f, 0.0f, 5u},
            HydraulicElement{HydraulicElementKind::PressureSource, 1, 0, 10.0f, 0.5f, 2u},
            HydraulicElement{HydraulicElementKind::FlowBranch, 1, 0, 2.0f, 0.0f, 3u},
            HydraulicElement{HydraulicElementKind::FlowBranch, 1, 0, 1.0f, 0.0f, 7u}
        }
    ));

    SimulationState st = make_sim_state(4);
    HydraulicRuntimeState rt;

    solve_hydraulic(plan, st, rt, 0.0);

    EXPECT_EQ(rt.branch_flows.size(), 8u);
    EXPECT_NEAR(rt.branch_flows[5], 0.0f, 1e-9f);
    EXPECT_NEAR(st.values[1], 4.0f, 1e-3f);
    EXPECT_NEAR(rt.branch_flows[3], 8.0f, 1e-3f);   // g=2 * P=4 = 8
    EXPECT_NEAR(rt.branch_flows[7], 4.0f, 1e-3f);   // g=1 * P=4 = 4
    // Source: g=2 * (4-0) - 20 = -12
    EXPECT_NEAR(rt.branch_flows[2], -12.0f, 1e-3f);
}

TEST(HydraulicSubsolver, EmptyPlanClearsBranchFlows) {
    HydraulicBuildPlan plan;
    SimulationState st = make_sim_state(4);
    HydraulicRuntimeState rt;
    rt.branch_flows = {1.0f, 2.0f, 3.0f};

    EXPECT_NO_THROW(solve_hydraulic(plan, st, rt, 0.0));
    EXPECT_TRUE(rt.branch_flows.empty());
}

TEST(HydraulicSubsolver, AllNodesFixedNoSolveNeeded) {
    HydraulicBuildPlan plan;
    plan.islands.push_back(make_island(
        {0, 1},
        {
            HydraulicElement{HydraulicElementKind::FixedPressureNode, 0, 0, 0.0f, 0.0f, 0u},
            HydraulicElement{HydraulicElementKind::FixedPressureNode, 1, 1, 5.0f, 0.0f, 1u}
        }
    ));

    SimulationState st = make_sim_state(4);
    HydraulicRuntimeState rt;

    EXPECT_NO_THROW(solve_hydraulic(plan, st, rt, 0.0));
    EXPECT_NEAR(st.values[0], 0.0f, 1e-3f);
    EXPECT_NEAR(st.values[1], 5.0f, 1e-3f);
}

TEST(HydraulicSubsolver, PressureWritebackToSimulationState) {
    // Non-contiguous signal indices
    HydraulicBuildPlan plan;
    plan.islands.push_back(make_island(
        {10, 20, 30},
        {
            HydraulicElement{HydraulicElementKind::FixedPressureNode, 10, 10, 0.0f, 0.0f, 0u},
            HydraulicElement{HydraulicElementKind::PressureSource, 30, 10, 24.0f, 2.0f, 1u},
            HydraulicElement{HydraulicElementKind::FlowBranch, 30, 20, 1.0f, 0.0f, 2u},
            HydraulicElement{HydraulicElementKind::FlowBranch, 20, 10, 1.0f, 0.0f, 3u}
        }
    ));

    SimulationState st;
    st.values.resize(50, 999.0f);

    HydraulicRuntimeState rt;
    solve_hydraulic(plan, st, rt, 0.0);

    EXPECT_NEAR(st.values[10], 0.0f, 1e-3f);
    EXPECT_NEAR(st.values[30], 12.0f, 1e-3f);
    EXPECT_NEAR(st.values[20], 6.0f, 1e-3f);
    EXPECT_EQ(st.values[0], 999.0f);
    EXPECT_EQ(st.values[5], 999.0f);
}

TEST(HydraulicSubsolver, SingularIslandPreservesPreviousState) {
    // Floating island — no FixedPressureNode → singular
    HydraulicBuildPlan plan;
    plan.islands.push_back(make_island(
        {0, 1},
        {
            HydraulicElement{HydraulicElementKind::PressureSource, 0, 1, 28.0f, 0.01f, 0u},
            HydraulicElement{HydraulicElementKind::FlowBranch, 0, 1, 1.0f, 0.0f, 1u}
        }
    ));

    SimulationState st = make_sim_state(2);
    st.values[0] = 12.5f;
    st.values[1] = -3.0f;
    HydraulicRuntimeState rt;

    EXPECT_NO_THROW(solve_hydraulic(plan, st, rt, 1.0 / 60.0));
    EXPECT_NEAR(st.values[0], 12.5f, 1e-6f);
    EXPECT_NEAR(st.values[1], -3.0f, 1e-6f);

    ASSERT_GE(rt.branch_flows.size(), 2u);
    EXPECT_FLOAT_EQ(rt.branch_flows[0], 0.0f);
    EXPECT_FLOAT_EQ(rt.branch_flows[1], 0.0f);
}

TEST(HydraulicSubsolver, SolveCountersTrackSpecializedPaths) {
    HydraulicBuildPlan plan;

    // N==0 (all fixed)
    plan.islands.push_back(make_island({0}, {
        HydraulicElement{HydraulicElementKind::FixedPressureNode, 0, UINT32_MAX, 0.0f, 0.0f, 0u}
    }));

    // N==1
    plan.islands.push_back(make_island({1, 2}, {
        HydraulicElement{HydraulicElementKind::FixedPressureNode, 1, UINT32_MAX, 0.0f, 0.0f, 1u},
        HydraulicElement{HydraulicElementKind::PressureSource, 2, 1, 28.0f, 1.0f, 2u},
        HydraulicElement{HydraulicElementKind::FlowBranch, 2, 1, 1.0f, 0.0f, 3u}
    }));

    // N==2
    plan.islands.push_back(make_island({3, 4, 5}, {
        HydraulicElement{HydraulicElementKind::FixedPressureNode, 3, UINT32_MAX, 0.0f, 0.0f, 4u},
        HydraulicElement{HydraulicElementKind::PressureSource, 5, 3, 28.0f, 1.0f, 5u},
        HydraulicElement{HydraulicElementKind::FlowBranch, 5, 4, 0.5f, 0.0f, 6u},
        HydraulicElement{HydraulicElementKind::FlowBranch, 4, 3, 0.5f, 0.0f, 7u}
    }));

    // Singular floating N==2
    plan.islands.push_back(make_island({6, 7}, {
        HydraulicElement{HydraulicElementKind::PressureSource, 6, 7, 28.0f, 0.01f, 8u},
        HydraulicElement{HydraulicElementKind::FlowBranch, 6, 7, 1.0f, 0.0f, 9u}
    }));

    SimulationState st = make_sim_state(10);
    st.values[6] = 1.0f;
    st.values[7] = -1.0f;
    HydraulicRuntimeState rt;

    solve_hydraulic(plan, st, rt, 0.0);

    EXPECT_EQ(rt.counters.islands_total, 4u);
    EXPECT_EQ(rt.counters.solves_n0, 1u);
    EXPECT_EQ(rt.counters.solves_n1, 1u);
    EXPECT_EQ(rt.counters.solves_n2, 2u);
    EXPECT_EQ(rt.counters.solves_dense, 0u);
    EXPECT_EQ(rt.counters.singular_fallbacks, 1u);
}

TEST(HydraulicSubsolver, SolveCountersTrackDensePathForN3) {
    HydraulicBuildPlan plan;
    plan.islands.push_back(make_island(
        {0, 1, 2, 3},
        {
            HydraulicElement{HydraulicElementKind::FixedPressureNode, 0, UINT32_MAX, 0.0f, 0.0f, 0u},
            HydraulicElement{HydraulicElementKind::PressureSource, 3, 0, 28.0f, 1.0f, 1u},
            HydraulicElement{HydraulicElementKind::FlowBranch, 3, 2, 1.0f, 0.0f, 2u},
            HydraulicElement{HydraulicElementKind::FlowBranch, 2, 1, 1.0f, 0.0f, 3u},
            HydraulicElement{HydraulicElementKind::FlowBranch, 1, 0, 1.0f, 0.0f, 4u}
        }
    ));

    SimulationState st = make_sim_state(8);
    HydraulicRuntimeState rt;
    rt.enable_diagnostics = true;
    solve_hydraulic(plan, st, rt, 0.0);

    EXPECT_EQ(rt.counters.islands_total, 1u);
    EXPECT_EQ(rt.counters.solves_dense, 1u);
    EXPECT_EQ(rt.counters.singular_fallbacks, 0u);
    ASSERT_EQ(rt.island_diagnostics.size(), 1u);
    EXPECT_TRUE(rt.island_diagnostics[0].solve_ok);
    EXPECT_EQ(rt.island_diagnostics[0].unknown_count, 3u);
}

TEST(HydraulicSubsolver, ReservedScratchBuffersStableAcrossSteps) {
    HydraulicBuildPlan plan;
    plan.islands.push_back(make_island(
        {0, 1, 2},
        {
            HydraulicElement{HydraulicElementKind::FixedPressureNode, 0, UINT32_MAX, 0.0f, 0.0f, 0u},
            HydraulicElement{HydraulicElementKind::PressureSource, 2, 0, 28.0f, 1.0f, 1u},
            HydraulicElement{HydraulicElementKind::FlowBranch, 2, 1, 0.5f, 0.0f, 2u},
            HydraulicElement{HydraulicElementKind::FlowBranch, 1, 0, 0.5f, 0.0f, 3u}
        }
    ));

    SimulationState st = make_sim_state(6);
    HydraulicRuntimeState rt;
    rt.enable_diagnostics = true;
    rt.reserve(/*max_nodes=*/3, /*max_elements=*/4, /*max_element_id=*/3);

    solve_hydraulic(plan, st, rt, 0.0);

    size_t cap_branch_flows = rt.branch_flows.capacity();
    size_t cap_island_nodes = rt.island_nodes.capacity();
    size_t cap_island_pressures = rt.island_pressures.capacity();
    size_t cap_matrix = rt.scratch_matrix.capacity();
    size_t cap_rhs = rt.scratch_rhs.capacity();

    for (int i = 0; i < 100; ++i) {
        solve_hydraulic(plan, st, rt, 1.0 / 60.0);
    }

    EXPECT_EQ(rt.branch_flows.capacity(), cap_branch_flows);
    EXPECT_EQ(rt.island_nodes.capacity(), cap_island_nodes);
    EXPECT_EQ(rt.island_pressures.capacity(), cap_island_pressures);
    EXPECT_EQ(rt.scratch_matrix.capacity(), cap_matrix);
    EXPECT_EQ(rt.scratch_rhs.capacity(), cap_rhs);
}

TEST(HydraulicSubsolver, GetBranchFlowReturnsZeroForInvalidHandle) {
    HydraulicRuntimeState rt;
    rt.branch_flows = {1.0f, 2.0f, 3.0f};

    // Default handle — invalid
    HydraulicPrimitiveHandle invalid;
    EXPECT_FLOAT_EQ(get_branch_flow(rt, invalid), 0.0f);

    // Valid handle, out of range
    HydraulicPrimitiveHandle oob{0, 0, 100};
    EXPECT_FLOAT_EQ(get_branch_flow(rt, oob), 0.0f);

    // Valid handle, in range
    HydraulicPrimitiveHandle valid{0, 0, 1};
    EXPECT_FLOAT_EQ(get_branch_flow(rt, valid), 2.0f);
}

TEST(HydraulicSubsolver, ConvenienceOverloadInitializesFromPlanDefaults) {
    HydraulicBuildPlan plan;
    plan.islands.push_back(make_island(
        {0, 1},
        {
            HydraulicElement{HydraulicElementKind::FixedPressureNode, 0, 0, 0.0f, 0.0f, 0u},
            HydraulicElement{HydraulicElementKind::PressureSource, 1, 0, 7.65f, 0.1f, 1u},
            HydraulicElement{HydraulicElementKind::FlowBranch, 1, 0, 10.0f, 0.0f, 2u}
        }
    ));

    SimulationState st = make_sim_state(4);
    HydraulicRuntimeState rt;

    // Use convenience overload — should auto-init element_value_a
    solve_hydraulic(plan, st, rt, 0.0);

    EXPECT_NEAR(st.values[1], 3.825f, 0.01f);
    EXPECT_EQ(rt.element_value_a.size(), 3u);
    EXPECT_NEAR(rt.element_value_a[1], 7.65f, 1e-6f);
    EXPECT_NEAR(rt.element_value_a[2], 10.0f, 1e-6f);
}
