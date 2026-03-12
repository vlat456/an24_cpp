/// Debug test for AND gate always outputting 1
/// Reproduces the circuit from blueprint.blueprint:
///   Battery -> Bus -> Positive_V_to_Bool -> AND.A
///   HoldButton.state -> AND.B
///   (HoldButton not pressed -> state = 0 -> AND should output 0)

#include <gtest/gtest.h>
#include "json_parser/json_parser.h"
#include "jit_solver/jit_solver.h"
#include "jit_solver/SOR_constants.h"
#include "jit_solver/components/all.h"
#include <spdlog/spdlog.h>
#include <cstdio>


class ANDGateDebugTest : public ::testing::Test {
protected:
    void SetUp() override {
        spdlog::set_level(spdlog::level::debug);
    }
};

/// Helper: run the full simulation step (same order as editor simulation.cpp)
static void run_step(BuildResult& result, SimulationState& state, float dt, float omega) {
    state.clear_through();

    // 1. Electrical
    for (auto* variant : result.domain_components.electrical) {
        std::visit([&](auto& comp) {
            if constexpr (requires { comp.solve_electrical(state, dt); }) {
                comp.solve_electrical(state, dt);
            }
        }, *variant);
    }

    // 2. SOR
    state.precompute_inv_conductance();
    solve_sor_iteration(state.across.data(), state.through.data(),
        state.inv_conductance.data(), state.across.size(), SOR::OMEGA);

    // 3. Post-step
    for (auto& [name, variant] : result.devices) {
        std::visit([&](auto& comp) {
            if constexpr (requires { comp.post_step(state, dt); }) {
                comp.post_step(state, dt);
            }
        }, variant);
    }

    // 4. Logical (after SOR + post_step)
    for (auto* variant : result.domain_components.logical) {
        std::visit([&](auto& comp) {
            if constexpr (requires { comp.solve_logical(state, dt); }) {
                comp.solve_logical(state, dt);
            }
        }, *variant);
    }
}

TEST_F(ANDGateDebugTest, AND_With_Battery_VToBool_HoldButton) {
    // Minimal circuit reproducing the blueprint:
    //   gnd(0V) -> bat.v_in
    //   bat.v_out -> bus.v
    //   bus.v -> v2b.Vin
    //   v2b.o -> and_1.A
    //   hb.state -> and_1.B
    //   (hb.control is unconnected - floats at 0)
    //   (hb.v_in -> bus.v, hb.v_out unconnected)

    const char* json = R"({
        "devices": [
            {"name": "gnd", "classname": "RefNode", "params": {"value": "0.0"}},
            {"name": "bat", "classname": "Battery", "params": {
                "v_nominal": "28.0", "internal_r": "0.01", "inv_internal_r": "100.0",
                "capacity": "1000.0", "inv_capacity": "0.001", "charge": "1000.0"
            }},
            {"name": "bus", "classname": "Bus"},
            {"name": "v2b", "classname": "Positive_V_to_Bool"},
            {"name": "hb", "classname": "HoldButton", "params": {"idle": "0.0"}},
            {"name": "and_1", "classname": "AND"}
        ],
        "connections": [
            {"from": "gnd.v", "to": "bat.v_in"},
            {"from": "bat.v_out", "to": "bus.v"},
            {"from": "bus.v", "to": "v2b.Vin"},
            {"from": "bus.v", "to": "hb.v_in"},
            {"from": "v2b.o", "to": "and_1.A"},
            {"from": "hb.state", "to": "and_1.B"}
        ]
    })";

    // Parse
    ParserContext ctx = parse_json(std::string(json));
    std::vector<std::pair<std::string, std::string>> connections;
    for (const auto& c : ctx.connections) {
        connections.push_back({c.from, c.to});
    }

    // Build
    BuildResult result = build_systems_dev(ctx.devices, connections);

    // Allocate state
    SimulationState state;
    for (uint32_t i = 0; i < result.signal_count; ++i) {
        bool is_fixed = std::binary_search(
            result.fixed_signals.begin(), result.fixed_signals.end(), i);
        state.allocate_signal(0.0f, {Domain::Electrical, is_fixed});
    }

    // Initialize RefNodes
    for (const auto& dev : ctx.devices) {
        if (dev.classname == "RefNode") {
            float value = std::stof(dev.params.at("value"));
            auto it = result.port_to_signal.find(dev.name + ".v");
            if (it != result.port_to_signal.end()) {
                state.across[it->second] = value;
            }
        }
    }

    // Print signal mapping
    printf("\n=== SIGNAL MAP ===\n");
    for (auto& [port, sig] : result.port_to_signal) {
        printf("  %-30s -> signal[%u]\n", port.c_str(), sig);
    }

    // Print domain component counts
    printf("\n=== DOMAIN COMPONENTS ===\n");
    printf("  electrical: %zu\n", result.domain_components.electrical.size());
    printf("  logical:    %zu\n", result.domain_components.logical.size());

    // Get signal indices for the signals we care about
    auto get_sig = [&](const std::string& port) -> uint32_t {
        auto it = result.port_to_signal.find(port);
        EXPECT_NE(it, result.port_to_signal.end()) << "Missing: " << port;
        return it->second;
    };

    uint32_t sig_bus_v    = get_sig("bus.v");
    uint32_t sig_v2b_Vin  = get_sig("v2b.Vin");
    uint32_t sig_v2b_o    = get_sig("v2b.o");
    uint32_t sig_hb_state = get_sig("hb.state");
    uint32_t sig_and_A    = get_sig("and_1.A");
    uint32_t sig_and_B    = get_sig("and_1.B");
    uint32_t sig_and_o    = get_sig("and_1.o");

    printf("\n=== KEY SIGNALS ===\n");
    printf("  bus.v     = signal[%u]\n", sig_bus_v);
    printf("  v2b.Vin   = signal[%u] (should == bus.v)\n", sig_v2b_Vin);
    printf("  v2b.o     = signal[%u]\n", sig_v2b_o);
    printf("  hb.state  = signal[%u]\n", sig_hb_state);
    printf("  and_1.A   = signal[%u] (should == v2b.o)\n", sig_and_A);
    printf("  and_1.B   = signal[%u] (should == hb.state)\n", sig_and_B);
    printf("  and_1.o   = signal[%u]\n", sig_and_o);

    // Verify wired signals share the same index
    EXPECT_EQ(sig_v2b_Vin, sig_bus_v) << "v2b.Vin should be wired to bus.v";
    EXPECT_EQ(sig_and_A, sig_v2b_o) << "and_1.A should be wired to v2b.o";
    EXPECT_EQ(sig_and_B, sig_hb_state) << "and_1.B should be wired to hb.state";

    float dt = 1.0f / 60.0f;

    // Run 10 steps with per-phase tracing
    printf("\n=== SIMULATION (per-phase) ===\n");
    for (int step = 0; step < 5; ++step) {
        state.clear_through();
        printf("  step %d AFTER clear:   v2b.o=%.4f  hb.state=%.4f  and.o=%.4f\n",
               step, state.across[sig_v2b_o], state.across[sig_hb_state], state.across[sig_and_o]);

        // 1. Electrical
        for (auto* variant : result.domain_components.electrical) {
            std::visit([&](auto& comp) {
                if constexpr (requires { comp.solve_electrical(state, dt); }) {
                    comp.solve_electrical(state, dt);
                }
            }, *variant);
        }
        printf("  step %d AFTER elec:    bus=%.4f  v2b.o=%.4f  hb.state=%.4f\n",
               step, state.across[sig_bus_v], state.across[sig_v2b_o], state.across[sig_hb_state]);

        // 2. SOR
        state.precompute_inv_conductance();
        printf("  step %d invG[v2b.o]=%e invG[hb.state]=%e invG[and.o]=%e\n",
               step, state.inv_conductance[sig_v2b_o], state.inv_conductance[sig_hb_state], state.inv_conductance[sig_and_o]);
        printf("  step %d through[v2b.o]=%e through[hb.state]=%e through[and.o]=%e\n",
               step, state.through[sig_v2b_o], state.through[sig_hb_state], state.through[sig_and_o]);
        solve_sor_iteration(state.across.data(), state.through.data(),
            state.inv_conductance.data(), state.across.size(), SOR::OMEGA);
        printf("  step %d AFTER SOR:     bus=%.4f  v2b.o=%.4f  hb.state=%.4f\n",
               step, state.across[sig_bus_v], state.across[sig_v2b_o], state.across[sig_hb_state]);

        // 3. Post-step
        for (auto& [name, variant] : result.devices) {
            std::visit([&](auto& comp) {
                if constexpr (requires { comp.post_step(state, dt); }) {
                    comp.post_step(state, dt);
                }
            }, variant);
        }
        printf("  step %d AFTER post:    v2b.o=%.4f  hb.state=%.4f\n",
               step, state.across[sig_v2b_o], state.across[sig_hb_state]);

        // 4. Logical
        for (size_t li = 0; li < result.domain_components.logical.size(); ++li) {
            auto* variant = result.domain_components.logical[li];
            std::visit([&](auto& comp) {
                using T = std::decay_t<decltype(comp)>;
                if constexpr (requires { comp.solve_logical(state, dt); }) {
                    printf("  step %d LOGICAL[%zu] %s: BEFORE v2b.o=%.4f and.A=%.4f and.B=%.4f\n",
                           step, li, typeid(T).name(),
                           state.across[sig_v2b_o], state.across[sig_and_A], state.across[sig_and_B]);
                    comp.solve_logical(state, dt);
                    printf("  step %d LOGICAL[%zu] %s: AFTER  v2b.o=%.4f and.A=%.4f and.B=%.4f and.o=%.4f\n",
                           step, li, typeid(T).name(),
                           state.across[sig_v2b_o], state.across[sig_and_A], state.across[sig_and_B], state.across[sig_and_o]);
                }
            }, *variant);
        }
        printf("  step %d FINAL: bus=%.2f v2b.o=%.2f hb.state=%.2f AND.A=%.2f AND.B=%.2f AND.o=%.2f\n\n",
               step, state.across[sig_bus_v], state.across[sig_v2b_o],
               state.across[sig_hb_state], state.across[sig_and_A],
               state.across[sig_and_B], state.across[sig_and_o]);
    }

    // After 10 steps:
    // - Battery should output ~28V on bus
    // - Positive_V_to_Bool should convert to 1.0 (28V > 0)
    // - HoldButton not pressed -> state = 0.0
    // - AND: A=1.0, B=0.0 -> output should be 0.0
    printf("\n=== ASSERTIONS ===\n");
    float bus_v = state.across[sig_bus_v];
    float and_a = state.across[sig_and_A];
    float and_b = state.across[sig_and_B];
    float and_o = state.across[sig_and_o];

    printf("  bus.v = %.4f (expect ~28V)\n", bus_v);
    printf("  AND.A = %.4f (expect 1.0 from V_to_Bool)\n", and_a);
    printf("  AND.B = %.4f (expect 0.0 from HoldButton.state)\n", and_b);
    printf("  AND.o = %.4f (expect 0.0 since B is false)\n", and_o);

    EXPECT_GT(bus_v, 20.0f) << "Battery should charge bus";
    EXPECT_NEAR(and_a, 1.0f, 0.01f) << "V_to_Bool should output 1.0 (bus has voltage)";
    EXPECT_NEAR(and_b, 0.0f, 0.01f) << "HoldButton not pressed, state should be 0.0";
    EXPECT_NEAR(and_o, 0.0f, 0.01f) << "AND should output 0.0 (B is false)";
}
