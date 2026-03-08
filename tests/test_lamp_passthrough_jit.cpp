/// JIT integration test: Battery -> lamp_pass_through blueprint voltage flow
/// Tests that collapsed blueprints correctly pass voltage through when expanded

#include <gtest/gtest.h>
#include "json_parser/json_parser.h"
#include "jit_solver/jit_solver.h"
#include "jit_solver/SOR_constants.h"
#include <spdlog/spdlog.h>

using namespace an24;

TEST(JITIntegration, LampPassThrough_Blueprint_VoltageFlow) {
    // Circuit: Ref(0V) -> Battery.In, Battery.Out -> LampPassThrough.In, LampPassThrough.Out -> measure
    // This tests that the lamp_pass_through collapsed blueprint correctly passes voltage

    const char* json = R"(
    {
      "devices": [
        {
          "name": "gnd",
          "classname": "RefNode",
          "params": {"value": "0.0"}
        },
        {
          "name": "battery",
          "classname": "Battery",
          "params": {
            "v_nominal": "28.0",
            "internal_r": "0.01",
            "inv_internal_r": "100.0",
            "capacity": "1000.0",
            "inv_capacity": "0.001",
            "charge": "1000.0"
          }
        },
        {
          "name": "lamp_bp",
          "classname": "lamp_pass_through"
        }
      ],
      "connections": [
        {
          "from": "gnd.v",
          "to": "battery.v_in"
        },
        {
          "from": "battery.v_out",
          "to": "lamp_bp.vin"
        }
      ]
    }
    )";

    // Parse and build
    ParserContext ctx = parse_json(std::string(json));

    std::vector<std::pair<std::string, std::string>> connections;
    for (const auto& c : ctx.connections) {
        connections.push_back({c.from, c.to});
    }

    BuildResult result = build_systems_dev(ctx.devices, connections);

    // Allocate state
    SimulationState state;
    for (uint32_t i = 0; i < result.signal_count; ++i) {
        bool is_fixed = std::binary_search(
            result.fixed_signals.begin(),
            result.fixed_signals.end(), i);
        (void)state.allocate_signal(0.0f, {Domain::Electrical, is_fixed});
    }

    // Initialize RefNodes
    for (const auto& dev : ctx.devices) {
        if (dev.classname == "RefNode") {
            float value = 0.0f;
            auto it_val = dev.params.find("value");
            if (it_val != dev.params.end()) {
                value = std::stof(it_val->second);
            }
            auto it_sig = result.port_to_signal.find(dev.name + ".v");
            if (it_sig != result.port_to_signal.end()) {
                state.across[it_sig->second] = value;
            }
        }
    }

    // Run simulation
    float dt = 1.0f / 60.0f;
    for (int step = 0; step < 100; ++step) {
        state.clear_through();

        for (auto* variant : result.domain_components.electrical) {
            std::visit([&](auto& comp) {
                if constexpr (requires { comp.solve_electrical(state, dt); }) {
                    comp.solve_electrical(state, dt);
                }
            }, *variant);
        }

        state.precompute_inv_conductance();

        // SOR update
        for (size_t i = 0; i < state.across.size(); ++i) {
            if (!state.signal_types[i].is_fixed && state.inv_conductance[i] > 0.0f) {
                state.across[i] += state.through[i] * state.inv_conductance[i] * SOR::OMEGA;
            }
        }

        // post_step
        for (auto& [name, variant] : result.devices) {
            std::visit([&](auto& comp) {
                if constexpr (requires { comp.post_step(state, dt); }) {
                    comp.post_step(state, dt);
                }
            }, variant);
        }
    }

    // Check voltages
    auto gnd_it = result.port_to_signal.find("gnd.v");
    ASSERT_NE(gnd_it, result.port_to_signal.end()) << "gnd.v should exist";
    float gnd_v = state.across[gnd_it->second];

    auto bat_vin_it = result.port_to_signal.find("battery.v_in");
    ASSERT_NE(bat_vin_it, result.port_to_signal.end()) << "battery.v_in should exist";
    float bat_vin = state.across[bat_vin_it->second];

    auto bat_vout_it = result.port_to_signal.find("battery.v_out");
    ASSERT_NE(bat_vout_it, result.port_to_signal.end()) << "battery.v_out should exist";
    float bat_vout = state.across[bat_vout_it->second];

    // lamp_pass_through blueprint expands with prefix "lamp_bp:"
    // BlueprintInput becomes "lamp_bp:vin" with port "port"
    // BlueprintOutput becomes "lamp_bp:vout" with port "port"
    auto lamp_bp_vin_it = result.port_to_signal.find("lamp_bp:vin.port");
    ASSERT_NE(lamp_bp_vin_it, result.port_to_signal.end()) << "lamp_bp:vin.port should exist (BlueprintInput exposed port)";
    float lamp_bp_vin = state.across[lamp_bp_vin_it->second];

    auto lamp_bp_vout_it = result.port_to_signal.find("lamp_bp:vout.port");
    ASSERT_NE(lamp_bp_vout_it, result.port_to_signal.end()) << "lamp_bp:vout.port should exist (BlueprintOutput exposed port)";
    float lamp_bp_vout = state.across[lamp_bp_vout_it->second];

    // Log all voltages
    spdlog::warn("=== JIT Integration Test: lamp_pass_through Blueprint ===");
    spdlog::warn("gnd.v            = {:.2f}V", gnd_v);
    spdlog::warn("battery.v_in     = {:.2f}V", bat_vin);
    spdlog::warn("battery.v_out    = {:.2f}V", bat_vout);
    spdlog::warn("lamp_bp:vin.port = {:.2f}V", lamp_bp_vin);
    spdlog::warn("lamp_bp:vout.port= {:.2f}V", lamp_bp_vout);
    spdlog::warn("=======================================================");

    // Verify expected behavior
    EXPECT_NEAR(gnd_v, 0.0f, 0.1f) << "Ground should be at 0V";

    // Battery should output ~28V
    EXPECT_GT(bat_vout, 25.0f) << "Battery output should be >25V";
    EXPECT_LT(bat_vout, 30.0f) << "Battery output should be <30V";

    // lamp_pass_through input should match battery output
    EXPECT_NEAR(lamp_bp_vin, bat_vout, 1.0f) << "Blueprint input should match battery output";

    // lamp_pass_through output should have voltage (IndicatorLight passes most voltage)
    EXPECT_GT(lamp_bp_vout, 20.0f) << "Blueprint output should be >20V (IndicatorLight passing voltage)";
    EXPECT_LT(lamp_bp_vout, 30.0f) << "Blueprint output should be <30V";
}
