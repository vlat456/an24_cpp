/// JIT integration test: Battery -> lamp_pass_through blueprint voltage flow.

#include <gtest/gtest.h>
#include "jit_solver/simulator.h"

TEST(JITIntegration, LampPassThrough_Blueprint_VoltageFlow) {
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

    Simulator<JIT_Solver> sim;
    sim.start_from_json(std::string(json));

    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 100; ++i) {
        sim.step(dt);
    }

    const float gnd_v = sim.get_port_value("gnd", "v");
    const float bat_vout = sim.get_port_value("battery", "v_out");
    const float lamp_bp_vin = sim.get_wire_voltage("lamp_bp:vin.ext");
    const float lamp_bp_vout = sim.get_wire_voltage("lamp_bp:vout.ext");

    EXPECT_NEAR(gnd_v, 0.0f, 0.1f);
    EXPECT_GT(bat_vout, 25.0f);
    EXPECT_LT(bat_vout, 30.0f);
    EXPECT_NEAR(lamp_bp_vin, bat_vout, 1.0f);
    EXPECT_GT(lamp_bp_vout, 20.0f);
    EXPECT_LT(lamp_bp_vout, 30.0f);
}
