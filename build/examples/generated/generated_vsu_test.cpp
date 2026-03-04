#include "generated_vsu_test.h"

namespace an24 {

Systems::Systems()
{
    gnd.node_idx = 0;
    gnd.value = 0;
    bat_main_1.v_in_idx = 0;
    bat_main_1.v_out_idx = 1;
    bat_main_1.capacity = 1000;
    bat_main_1.v_nominal = 28;
    bat_main_1.charge = 1000;
    vsu_1.k_mod_idx = 5;
    vsu_1.rpm_out_idx = 4;
    vsu_1.t4_out_idx = 3;
    vsu_1.v_bus_idx = 1;
    vsu_1.v_gen_mon_idx = 2;
    vsu_1.v_start_idx = 1;
    vsu_1.target_rpm = 16000;
    rug_vsu.k_mod_idx = 5;
    rug_vsu.v_gen_idx = 1;
    rug_vsu.v_target = 28.5;
    light_1.brightness_idx = 6;
    light_1.v_in_idx = 1;
    light_1.v_out_idx = 0;
    light_1.max_brightness = 100;
    light_1.color = std::string("white");
    t4_sensor.input_idx = 3;
    t4_sensor.output_idx = 7;
    t4_sensor.factor = 0.1;
}

void Systems::pre_load() {
    bat_main_1.inv_internal_r = 1.0f / bat_main_1.internal_r;
}

void Systems::solve_step(void* state, uint32_t step) {
    auto* st = static_cast<SimulationState*>(state);
    (void)step;

    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

void Systems::post_step(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.post_step(*st, dt);
    bat_main_1.post_step(*st, dt);
    vsu_1.post_step(*st, dt);
    rug_vsu.post_step(*st, dt);
    light_1.post_step(*st, dt);
    t4_sensor.post_step(*st, dt);
}

} // namespace an24
