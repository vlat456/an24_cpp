#include "generated_an24_composite_test.h"

namespace an24 {

Systems::Systems()
{
    dc_bus_1.bus_idx = 1;
    dc_bus_2.bus_idx = 2;
    gnd.node_idx = 0;
    gnd.value = 0;
    bat_main_1.v_in_idx = 0;
    bat_main_1.v_out_idx = 1;
    bat_main_1.capacity = 1000;
    bat_main_1.v_nominal = 28;
    bat_main_1.charge = 1000;
    bat_main_2.v_in_idx = 0;
    bat_main_2.v_out_idx = 2;
    bat_main_2.capacity = 80;
    bat_main_2.v_nominal = 24;
    bat_main_2.charge = 80;
    relay_bus_tie.v_in_idx = 1;
    relay_bus_tie.v_out_idx = 2;
    gs24_1.v_in_idx = 0;
    gs24_1.v_out_idx = 1;
    gs24_1.v_nominal = 28.5;
    gs24_1.target_rpm = 16000;
    light_1.brightness_idx = 3;
    light_1.v_in_idx = 2;
    light_1.v_out_idx = 0;
    light_1.max_brightness = 100;
    light_1.color = std::string("white");
    gyro_1.input_idx = 2;
    agk_47_1.input_idx = 1;
}

void Systems::pre_load() {
    bat_main_1.inv_internal_r = 1.0f / bat_main_1.internal_r;
    bat_main_2.inv_internal_r = 1.0f / bat_main_2.internal_r;
}

void Systems::solve_step(void* state, uint32_t step) {
    auto* st = static_cast<SimulationState*>(state);
    (void)step;

    dc_bus_1.solve_electrical(*st);
    dc_bus_2.solve_electrical(*st);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    bat_main_2.solve_electrical(*st);
    relay_bus_tie.solve_electrical(*st);
    gs24_1.solve_electrical(*st);
    light_1.solve_electrical(*st);
    gyro_1.solve_electrical(*st);
    agk_47_1.solve_electrical(*st);
}

void Systems::post_step(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    dc_bus_1.post_step(*st, dt);
    dc_bus_2.post_step(*st, dt);
    gnd.post_step(*st, dt);
    bat_main_1.post_step(*st, dt);
    bat_main_2.post_step(*st, dt);
    relay_bus_tie.post_step(*st, dt);
    gs24_1.post_step(*st, dt);
    light_1.post_step(*st, dt);
    gyro_1.post_step(*st, dt);
    agk_47_1.post_step(*st, dt);
}

} // namespace an24
