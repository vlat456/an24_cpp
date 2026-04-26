/// Integration tests for the pneumatic domain solver.
///
/// Tests the full pipeline: blueprint → build → solve_nodal → pressure/flow results.
/// Verifies that pneumatic components (PneumaticCompressor, PneumaticRef, PneumaticValve)
/// produce correct nodal analysis results through the unified solve_nodal() function.

#include <gtest/gtest.h>
#include "core/solvers/jit/simulator.h"
#include "core/solvers/jit/jit_solver.h"
#include "core/solvers/jit/subsolvers/nodal_types.h"
#include "core/solvers/jit/build_common.h"
#include "io/json/parse_json_api.h"
#include "core/solvers/common/signal_key.h"

using namespace jit_solver_impl;

namespace {

/// Helper: parse JSON and build a simulator, run N steps, return simulator.
JIT_Simulator make_sim_and_run(const std::string& json, int steps = 1, double dt = 1.0 / 60.0) {
    auto input = build_input_from_json(json);
    JIT_Simulator sim;
    sim.start(input);
    for (int i = 0; i < steps; ++i) {
        sim.step(dt);
    }
    return sim;
}

/// Helper: get signal value by device and port name.
float get_val(const JIT_Simulator& sim, const std::string& device, const std::string& port) {
    auto key = sim.resolve_signal_key(device, port);
    return sim.get_signal_value(key);
}

} // anonymous namespace

// =============================================================================
// Basic Pneumatic Circuit: Compressor → Valve → PRef
// =============================================================================

TEST(PneumaticIntegration, CompressorThroughOpenValve_PressureDrop) {
    // Circuit: Compressor(p_out=700kPa) → Valve(open) → PRef(0kPa)
    // Compressor internal_r = 0.05 kPa·s/L, Valve g_open = 5.0 L/(s·kPa)
    // With compressor at rated RPM, P_source = 700 kPa
    //
    // Expected: p_out ≈ 700 * R_valve / (R_valve + R_comp)
    // R_valve = 1/g_open = 0.2 kPa·s/L
    // R_comp = 0.05 kPa·s/L
    // p_out = 700 * 0.2 / (0.2 + 0.05) = 700 * 0.8 = 560 kPa

    const char* json = R"(
    {
        "devices": [
            {
                "name": "comp",
                "classname": "PneumaticCompressor",
                "params": {"max_pressure": "700.0", "internal_r": "0.05", "rated_rpm": "24000.0"}
            },
            {
                "name": "valve",
                "classname": "PneumaticValve",
                "params": {"normally_closed": "false", "g_open": "5.0", "g_closed": "0.0001"}
            },
            {
                "name": "ref",
                "classname": "PneumaticRef",
                "params": {"pressure": "0.0"}
            }
        ],
        "connections": [
            {"from": "comp.p_out", "to": "valve.flow_in"},
            {"from": "valve.flow_out", "to": "ref.p"},
            {"from": "comp.p_ref", "to": "ref.p"}
        ]
    }
    )";

    auto input = build_input_from_json(json);
    input.initial_values["comp.rpm_in"] = 24000.0f;

    JIT_Simulator sim;
    sim.start(input);
    // Run a few frames for the CopySignal patch op to propagate
    sim.step(1.0 / 60.0);
    sim.step(1.0 / 60.0);

    // Check that p_out has a reasonable pressure (not 0, not 700)
    float p_out = get_val(sim, "comp", "p_out");
    EXPECT_GT(p_out, 100.0f) << "Compressor output pressure should be significant";
    EXPECT_LT(p_out, 700.0f) << "Compressor output should be less than max (due to load)";

    // Flow through the valve should be positive
    float p_ref = get_val(sim, "ref", "p");
    EXPECT_NEAR(p_ref, 0.0f, 1.0f) << "PneumaticRef should be at 0 kPa";
}

TEST(PneumaticIntegration, ClosedValve_NoFlow) {
    // Same circuit but valve closed — compressor sees near-open-circuit
    const char* json = R"(
    {
        "devices": [
            {
                "name": "comp",
                "classname": "PneumaticCompressor",
                "params": {"max_pressure": "700.0", "internal_r": "0.05", "rated_rpm": "24000.0"}
            },
            {
                "name": "valve",
                "classname": "PneumaticValve",
                "params": {"normally_closed": "true", "g_open": "5.0", "g_closed": "0.0001"}
            },
            {
                "name": "ref",
                "classname": "PneumaticRef",
                "params": {"pressure": "0.0"}
            }
        ],
        "connections": [
            {"from": "comp.p_out", "to": "valve.flow_in"},
            {"from": "valve.flow_out", "to": "ref.p"},
            {"from": "comp.p_ref", "to": "ref.p"}
        ]
    }
    )";

    auto input = build_input_from_json(json);
    input.initial_values["comp.rpm_in"] = 24000.0f;
    // ctrl = 0 means valve stays closed (normally_closed=true)

    JIT_Simulator sim;
    sim.start(input);
    sim.step(1.0 / 60.0);
    sim.step(1.0 / 60.0);

    // With valve closed, compressor p_out should be close to source pressure
    // (very little flow through g_closed=0.0001)
    float p_out = get_val(sim, "comp", "p_out");
    EXPECT_GT(p_out, 600.0f) << "With closed valve, pressure should be near max";
}

TEST(PneumaticIntegration, PneumaticRefZeroPressure) {
    // Simplest circuit: just a PneumaticRef
    const char* json = R"(
    {
        "devices": [
            {
                "name": "ref",
                "classname": "PneumaticRef",
                "params": {"pressure": "101.325"}
            }
        ],
        "connections": []
    }
    )";

    auto input = build_input_from_json(json);
    JIT_Simulator sim;
    sim.start(input);
    sim.step(1.0 / 60.0);

    float p = get_val(sim, "ref", "p");
    EXPECT_NEAR(p, 101.325f, 0.1f) << "PneumaticRef should output its configured pressure";
}

// =============================================================================
// Regression: valve timing and normally-open behavior
// =============================================================================

TEST(PneumaticIntegration, NormallyOpenValve_StaysOpenWhenCtrlZero) {
    // Normally-open valve (normally_closed=false) should stay open when ctrl=0.
    // Regression test: old code read ctrl in execute() and set next_state=false
    // when ctrl=0, which incorrectly closed a normally-open valve after 2 frames.
    const char* json = R"(
    {
        "devices": [
            {
                "name": "comp",
                "classname": "PneumaticCompressor",
                "params": {"max_pressure": "700.0", "internal_r": "0.05", "rated_rpm": "24000.0"}
            },
            {
                "name": "valve",
                "classname": "PneumaticValve",
                "params": {"normally_closed": "false", "g_open": "5.0", "g_closed": "0.0001"}
            },
            {
                "name": "ref",
                "classname": "PneumaticRef",
                "params": {"pressure": "0.0"}
            }
        ],
        "connections": [
            {"from": "comp.p_out", "to": "valve.flow_in"},
            {"from": "valve.flow_out", "to": "ref.p"},
            {"from": "comp.p_ref", "to": "ref.p"}
        ]
    }
    )";

    auto input = build_input_from_json(json);
    input.initial_values["comp.rpm_in"] = 24000.0f;
    // ctrl defaults to 0 — normally-open valve should stay open

    JIT_Simulator sim;
    sim.start(input);

    // Run many frames to ensure the valve stays open (not closing after delay).
    for (int i = 0; i < 10; ++i) {
        sim.step(1.0 / 60.0);
    }

    // With open valve, pressure should be divided by resistances (~560 kPa).
    // With closed valve it would be ~700 kPa (near open-circuit).
    float p_out = get_val(sim, "comp", "p_out");
    EXPECT_LT(p_out, 600.0f)
        << "Normally-open valve should stay open with ctrl=0, p_out=" << p_out;
    EXPECT_GT(p_out, 400.0f)
        << "Pressure should show significant drop through open valve, p_out=" << p_out;
}

TEST(PneumaticIntegration, ValveCtrlChange_OneFrameDelay) {
    // Regression test: verify that ctrl changes take effect in exactly 1 frame
    // (matching SolenoidValve semantics), not 2-3 frames.
    //
    // Pipeline: patch_ops → solve → execute → scheduler → commit
    // ctrl is read in commit() (after scheduler), signal written in commit().
    // Next frame's patch_op reads the signal → 1-frame delay.
    const char* json = R"(
    {
        "devices": [
            {
                "name": "comp",
                "classname": "PneumaticCompressor",
                "params": {"max_pressure": "700.0", "internal_r": "0.05", "rated_rpm": "24000.0"}
            },
            {
                "name": "valve",
                "classname": "PneumaticValve",
                "params": {"normally_closed": "true", "g_open": "5.0", "g_closed": "0.0001"}
            },
            {
                "name": "ref",
                "classname": "PneumaticRef",
                "params": {"pressure": "0.0"}
            }
        ],
        "connections": [
            {"from": "comp.p_out", "to": "valve.flow_in"},
            {"from": "valve.flow_out", "to": "ref.p"},
            {"from": "comp.p_ref", "to": "ref.p"}
        ]
    }
    )";

    auto input = build_input_from_json(json);
    input.initial_values["comp.rpm_in"] = 24000.0f;

    JIT_Simulator sim;
    sim.start(input);

    // Warm-up: let CopySignal patch op propagate compressor pressure.
    // Frame 0: compressor execute writes p_source=700, CopySignal hasn't run yet.
    // Frame 1: CopySignal copies p_source to element_value_a, solve uses it.
    sim.step(1.0 / 60.0);
    sim.step(1.0 / 60.0);

    // Verify valve is closed (normally_closed=true, ctrl=0).
    float p_closed = get_val(sim, "comp", "p_out");
    EXPECT_GT(p_closed, 600.0f) << "Valve should be closed initially, p=" << p_closed;

    // Open the valve by setting ctrl signal.
    auto ctrl_key = sim.resolve_signal_key("valve", "ctrl");
    sim.apply_typed_overrides({{ctrl_key, 1.0f}});

    // Frame 2: patch_op still reads old signal → valve still closed.
    sim.step(1.0 / 60.0);
    float p_frame2 = get_val(sim, "comp", "p_out");
    EXPECT_GT(p_frame2, 600.0f)
        << "One-frame delay: valve should still be closed, p=" << p_frame2;

    // Frame 3: patch_op reads signal written in frame 2's commit → valve open.
    sim.step(1.0 / 60.0);
    float p_frame3 = get_val(sim, "comp", "p_out");
    EXPECT_LT(p_frame3, 600.0f)
        << "Valve should now be open (1-frame delay elapsed), p=" << p_frame3;
}

TEST(PneumaticIntegration, NormallyOpenValve_ClosesWhenCtrlActive) {
    // Normally-open valve should CLOSE when ctrl > 0.5.
    // (For NO: state = !ctrl_active, so ctrl=1 → state=false → g_closed)
    const char* json = R"(
    {
        "devices": [
            {
                "name": "comp",
                "classname": "PneumaticCompressor",
                "params": {"max_pressure": "700.0", "internal_r": "0.05", "rated_rpm": "24000.0"}
            },
            {
                "name": "valve",
                "classname": "PneumaticValve",
                "params": {"normally_closed": "false", "g_open": "5.0", "g_closed": "0.0001"}
            },
            {
                "name": "ref",
                "classname": "PneumaticRef",
                "params": {"pressure": "0.0"}
            }
        ],
        "connections": [
            {"from": "comp.p_out", "to": "valve.flow_in"},
            {"from": "valve.flow_out", "to": "ref.p"},
            {"from": "comp.p_ref", "to": "ref.p"}
        ]
    }
    )";

    auto input = build_input_from_json(json);
    input.initial_values["comp.rpm_in"] = 24000.0f;
    input.initial_values["valve.ctrl"] = 1.0f;  // Energize → closes NO valve

    JIT_Simulator sim;
    sim.start(input);

    // Run frames: with ctrl=1, NO valve should close.
    for (int i = 0; i < 5; ++i) {
        sim.step(1.0 / 60.0);
    }

    float p_out = get_val(sim, "comp", "p_out");
    EXPECT_GT(p_out, 600.0f)
        << "Normally-open valve should close when ctrl=1, p_out=" << p_out;
}
