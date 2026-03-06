#include "generated_vsu_test.h"
#include <cstring>  // memcpy

namespace an24 {

Systems::Systems()
{
    // Pre-allocate convergence buffer (zero-allocation in hot path)
    static float buf[SIGNAL_COUNT];
    convergence_buffer = buf;

    gnd.v_idx = 0;
    gnd.value = 0;
    bat_main_1.v_in_idx = 0;
    bat_main_1.v_out_idx = 1;
    bat_main_1.charge = 1000;
    bat_main_1.v_nominal = 28;
    bat_main_1.capacity = 1000;
    bat_main_1.internal_r = 0.01;
    vsu_1.k_mod_idx = 4;
    vsu_1.rpm_out_idx = 2;
    vsu_1.t4_out_idx = 3;
    vsu_1.v_start_idx = 1;
    vsu_1.v_bus_idx = 1;
    vsu_1.target_rpm = 16000;
    rug_vsu.k_mod_idx = 4;
    rug_vsu.v_gen_idx = 1;
    rug_vsu.v_target = 28.5;
    light_1.v_out_idx = 0;
    light_1.brightness_idx = 5;
    light_1.v_in_idx = 1;
    light_1.max_brightness = 100;
    light_1.color = std::string("white");
    t4_sensor.input_idx = 3;
    t4_sensor.output_idx = 6;
    t4_sensor.factor = 0.1;
}

void Systems::pre_load() {
    bat_main_1.inv_internal_r = 1.0f / bat_main_1.internal_r;
}

void Systems::solve_step(void* state, uint32_t step, float dt) {
    switch (step % 60) {
        case 0: step_0(state, dt); break;
        case 1: step_1(state, dt); break;
        case 2: step_2(state, dt); break;
        case 3: step_3(state, dt); break;
        case 4: step_4(state, dt); break;
        case 5: step_5(state, dt); break;
        case 6: step_6(state, dt); break;
        case 7: step_7(state, dt); break;
        case 8: step_8(state, dt); break;
        case 9: step_9(state, dt); break;
        case 10: step_10(state, dt); break;
        case 11: step_11(state, dt); break;
        case 12: step_12(state, dt); break;
        case 13: step_13(state, dt); break;
        case 14: step_14(state, dt); break;
        case 15: step_15(state, dt); break;
        case 16: step_16(state, dt); break;
        case 17: step_17(state, dt); break;
        case 18: step_18(state, dt); break;
        case 19: step_19(state, dt); break;
        case 20: step_20(state, dt); break;
        case 21: step_21(state, dt); break;
        case 22: step_22(state, dt); break;
        case 23: step_23(state, dt); break;
        case 24: step_24(state, dt); break;
        case 25: step_25(state, dt); break;
        case 26: step_26(state, dt); break;
        case 27: step_27(state, dt); break;
        case 28: step_28(state, dt); break;
        case 29: step_29(state, dt); break;
        case 30: step_30(state, dt); break;
        case 31: step_31(state, dt); break;
        case 32: step_32(state, dt); break;
        case 33: step_33(state, dt); break;
        case 34: step_34(state, dt); break;
        case 35: step_35(state, dt); break;
        case 36: step_36(state, dt); break;
        case 37: step_37(state, dt); break;
        case 38: step_38(state, dt); break;
        case 39: step_39(state, dt); break;
        case 40: step_40(state, dt); break;
        case 41: step_41(state, dt); break;
        case 42: step_42(state, dt); break;
        case 43: step_43(state, dt); break;
        case 44: step_44(state, dt); break;
        case 45: step_45(state, dt); break;
        case 46: step_46(state, dt); break;
        case 47: step_47(state, dt); break;
        case 48: step_48(state, dt); break;
        case 49: step_49(state, dt); break;
        case 50: step_50(state, dt); break;
        case 51: step_51(state, dt); break;
        case 52: step_52(state, dt); break;
        case 53: step_53(state, dt); break;
        case 54: step_54(state, dt); break;
        case 55: step_55(state, dt); break;
        case 56: step_56(state, dt); break;
        case 57: step_57(state, dt); break;
        case 58: step_58(state, dt); break;
        case 59: step_59(state, dt); break;
    }
}

AOT_INLINE void Systems::step_0(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_1(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_2(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_3(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_4(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_5(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_6(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_7(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_8(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_9(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_10(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_11(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_12(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_13(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_14(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_15(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_16(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_17(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_18(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_19(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_20(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_21(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_22(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_23(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_24(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_25(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_26(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_27(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_28(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_29(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_30(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_31(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_32(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_33(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_34(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_35(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_36(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_37(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_38(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_39(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_40(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_41(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_42(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_43(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_44(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_45(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_46(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_47(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_48(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_49(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_50(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_51(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_52(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_53(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_54(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_55(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_56(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_57(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_58(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
}

AOT_INLINE void Systems::step_59(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st, dt);
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
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

AOT_INLINE void Systems::balance_electrical(void* state, float inv_omega) {
    auto* st = static_cast<SimulationState*>(state);
    // __restrict: no aliasing, enables SIMD
    float* __restrict acc = st->across.data();
    const float* __restrict thr = st->through.data();
    const float* __restrict inv_g = st->inv_conductance.data();
    const auto& types = st->signal_types;
    const uint32_t count = static_cast<uint32_t>(st->across.size());

    for (uint32_t i = 0; i < count; ++i) {
        if (!types[i].is_fixed && inv_g[i] > 0.0f) {
            acc[i] += thr[i] * inv_g[i] * inv_omega;
        }
    }
}

AOT_INLINE bool Systems::check_convergence(void* state, float tolerance) const {
    auto* st = static_cast<SimulationState*>(state);
    const float* __restrict across = st->across.data();
    const float* __restrict buf = convergence_buffer;
    const uint32_t count = st->dynamic_signals_count;

    // Sparse check: every 4th signal - cache friendly
    for (uint32_t i = 0; i < count; i += 4) {
        float delta = std::abs(across[i] - buf[i]);
        if (AOT_UNLIKELY(delta > tolerance)) return false;
    }
    return true;
}

} // namespace an24
