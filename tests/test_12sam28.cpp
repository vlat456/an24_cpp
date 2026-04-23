#include <gtest/gtest.h>

#include "core/solvers/jit/simulator.h"

#include <string>

namespace {

const char* kBatteryWithLoadJson(float conductance_s) {
    static std::string json;
    json = "{"
           "\"devices\":["
           "{\"name\":\"sb\",\"classname\":\"12SAM28\"},"
           "{\"name\":\"load\",\"classname\":\"ElectricalConductance\",\"params\":{\"conductance\":\"" +
           std::to_string(conductance_s) +
           "\"}},"
           "{\"name\":\"gnd\",\"classname\":\"RefNode\",\"params\":{\"value\":\"0.0\"}}"
           "],"
           "\"connections\":["
           "{\"from\":\"sb.v_out\",\"to\":\"load.v_in\"},"
           "{\"from\":\"load.v_out\",\"to\":\"gnd.v\"},"
           "{\"from\":\"sb.v_in\",\"to\":\"gnd.v\"}"
           "]"
           "}";
    return json.c_str();
}

}  // namespace

TEST(SAM28Composite, InitialOutputsAreSane) {
    JIT_Simulator sim;
    sim.start(build_input_from_json(kBatteryWithLoadJson(0.1f)));

    // Two warmup steps: frame 1 populates LUT→cmd, frame 2 CVS uses cmd for solve
    sim.step(1.0 / 60.0);
    sim.step(1.0 / 60.0);

    const float charge = sim.get_signal_value(sim.resolve_signal_key("sb", "charge_out"));
    const float soc = sim.get_signal_value(sim.resolve_signal_key("sb", "soc_out"));
    const float v_out = sim.get_signal_value(sim.resolve_signal_key("sb", "v_out"));

    EXPECT_NEAR(charge, 28.0f, 1e-3f);
    EXPECT_NEAR(soc, 1.0f, 1e-3f);
    EXPECT_GT(v_out, 24.5f);
    EXPECT_LT(v_out, 25.3f);
}

TEST(SAM28Composite, DischargeDecreasesChargeAndSoc) {
    JIT_Simulator sim;
    sim.start(build_input_from_json(kBatteryWithLoadJson(2.0f)));

    // Two warmup steps for CVS to receive LUT→cmd
    sim.step(1.0 / 60.0);
    sim.step(1.0 / 60.0);
    const float charge_0 = sim.get_signal_value(sim.resolve_signal_key("sb", "charge_out"));
    const float soc_0 = sim.get_signal_value(sim.resolve_signal_key("sb", "soc_out"));

    for (int i = 0; i < 600; ++i) {
        sim.step(1.0 / 60.0);
    }

    const float charge_1 = sim.get_signal_value(sim.resolve_signal_key("sb", "charge_out"));
    const float soc_1 = sim.get_signal_value(sim.resolve_signal_key("sb", "soc_out"));

    EXPECT_LT(charge_1, charge_0 - 0.05f);
    EXPECT_LT(soc_1, soc_0 - 0.001f);
}

TEST(SAM28Composite, SocToOcvFeedbackCausesVoltageDrop) {
    JIT_Simulator sim;
    sim.start(build_input_from_json(kBatteryWithLoadJson(10.0f)));

    // Two warmup steps for CVS to receive LUT→cmd
    sim.step(1.0 / 60.0);
    sim.step(1.0 / 60.0);
    const float v_0 = sim.get_signal_value(sim.resolve_signal_key("sb", "v_out"));

    for (int i = 0; i < 1800; ++i) {
        sim.step(1.0 / 60.0);
    }

    const float v_1 = sim.get_signal_value(sim.resolve_signal_key("sb", "v_out"));
    const float soc_1 = sim.get_signal_value(sim.resolve_signal_key("sb", "soc_out"));

    EXPECT_LT(soc_1, 1.0f);
    EXPECT_LT(v_1, v_0 - 0.05f);
}
