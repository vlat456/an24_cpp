#include "generated_vsu_test.h"
#include <cstring>  // memcpy

namespace an24 {

Systems::Systems()
{
    // Pre-allocate convergence buffer (zero-allocation in hot path)
    static float buf[SIGNAL_COUNT];
    convergence_buffer = buf;

    gnd.node_idx = 0;
    gnd.value = 0;
    bat_main_1.v_in_idx = 0;
    bat_main_1.v_out_idx = 1;
    bat_main_1.charge = 1000;
    bat_main_1.v_nominal = 28;
    bat_main_1.capacity = 1000;
    bat_main_1.internal_r = 0.01;
    vsu_1.v_bus_idx = 1;
    vsu_1.t4_out_idx = 2;
    vsu_1.v_start_idx = 1;
    vsu_1.k_mod_idx = 5;
    vsu_1.v_gen_mon_idx = 3;
    vsu_1.rpm_out_idx = 4;
    vsu_1.target_rpm = 16000;
    rug_vsu.k_mod_idx = 5;
    rug_vsu.v_gen_idx = 1;
    rug_vsu.v_target = 28.5;
    light_1.v_out_idx = 0;
    light_1.brightness_idx = 6;
    light_1.v_in_idx = 1;
    light_1.max_brightness = 100;
    light_1.color = std::string("white");
    t4_sensor.input_idx = 2;
    t4_sensor.output_idx = 7;
    t4_sensor.factor = 0.1;
}

void Systems::pre_load() {
    bat_main_1.inv_internal_r = 1.0f / bat_main_1.internal_r;
}

void Systems::solve_step(void* state, uint32_t step) {
    switch (step % 60) {
        case 0: step_0(state); break;
        case 1: step_1(state); break;
        case 2: step_2(state); break;
        case 3: step_3(state); break;
        case 4: step_4(state); break;
        case 5: step_5(state); break;
        case 6: step_6(state); break;
        case 7: step_7(state); break;
        case 8: step_8(state); break;
        case 9: step_9(state); break;
        case 10: step_10(state); break;
        case 11: step_11(state); break;
        case 12: step_12(state); break;
        case 13: step_13(state); break;
        case 14: step_14(state); break;
        case 15: step_15(state); break;
        case 16: step_16(state); break;
        case 17: step_17(state); break;
        case 18: step_18(state); break;
        case 19: step_19(state); break;
        case 20: step_20(state); break;
        case 21: step_21(state); break;
        case 22: step_22(state); break;
        case 23: step_23(state); break;
        case 24: step_24(state); break;
        case 25: step_25(state); break;
        case 26: step_26(state); break;
        case 27: step_27(state); break;
        case 28: step_28(state); break;
        case 29: step_29(state); break;
        case 30: step_30(state); break;
        case 31: step_31(state); break;
        case 32: step_32(state); break;
        case 33: step_33(state); break;
        case 34: step_34(state); break;
        case 35: step_35(state); break;
        case 36: step_36(state); break;
        case 37: step_37(state); break;
        case 38: step_38(state); break;
        case 39: step_39(state); break;
        case 40: step_40(state); break;
        case 41: step_41(state); break;
        case 42: step_42(state); break;
        case 43: step_43(state); break;
        case 44: step_44(state); break;
        case 45: step_45(state); break;
        case 46: step_46(state); break;
        case 47: step_47(state); break;
        case 48: step_48(state); break;
        case 49: step_49(state); break;
        case 50: step_50(state); break;
        case 51: step_51(state); break;
        case 52: step_52(state); break;
        case 53: step_53(state); break;
        case 54: step_54(state); break;
        case 55: step_55(state); break;
        case 56: step_56(state); break;
        case 57: step_57(state); break;
        case 58: step_58(state); break;
        case 59: step_59(state); break;
    }
}

AOT_INLINE void Systems::step_0(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_1(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_2(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_3(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_4(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_5(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_6(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_7(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_8(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_9(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_10(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_11(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_12(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_13(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_14(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_15(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_16(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_17(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_18(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_19(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_20(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_21(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_22(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_23(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_24(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_25(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_26(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_27(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_28(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_29(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_30(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_31(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_32(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_33(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_34(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_35(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_36(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_37(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_38(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_39(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_40(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_41(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_42(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_43(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_44(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_45(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_46(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_47(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_48(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_49(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_50(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_51(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_52(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_53(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_54(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_55(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_56(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_57(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_58(void* state) {
    auto* st = static_cast<SimulationState*>(state);
    gnd.solve_electrical(*st);
    bat_main_1.solve_electrical(*st);
    vsu_1.solve_electrical(*st);
    rug_vsu.solve_electrical(*st);
    light_1.solve_electrical(*st);
    t4_sensor.solve_electrical(*st);
}

AOT_INLINE void Systems::step_59(void* state) {
    auto* st = static_cast<SimulationState*>(state);
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
