#include "generated_vsu_test.h"
#include "jit_solver/components/all.cpp"
#include <cstring>  // memcpy

#ifdef __GNUC__
#pragma GCC optimize("fast-math,unroll-loops")
#endif

namespace an24 {

// Explicit template instantiations for AOT
template class RefNode<AotProvider<Binding<PortNames::v, 0>>>;
template class Battery<AotProvider<Binding<PortNames::v_in, 0>, Binding<PortNames::v_out, 1>>>;
template class RU19A<AotProvider<Binding<PortNames::k_mod, 4>, Binding<PortNames::rpm_out, 2>, Binding<PortNames::t4_out, 3>, Binding<PortNames::v_start, 1>, Binding<PortNames::v_bus, 1>>>;
template class RUG82<AotProvider<Binding<PortNames::k_mod, 4>, Binding<PortNames::v_gen, 1>>>;
template class IndicatorLight<AotProvider<Binding<PortNames::v_out, 0>, Binding<PortNames::brightness, 5>, Binding<PortNames::v_in, 1>>>;
template class LerpNode<AotProvider<Binding<PortNames::input, 3>, Binding<PortNames::output, 6>>>;

Systems::Systems()
{
    // Pre-allocate convergence buffer (zero-allocation in hot path)
    alignas(64) static float buf[SIGNAL_COUNT];
    convergence_buffer = buf;

    gnd.value = 0;
    bat_main_1.charge = 1000;
    bat_main_1.v_nominal = 28;
    bat_main_1.capacity = 1000;
    bat_main_1.internal_r = 0.01;
    vsu_1.target_rpm = 16000;
    rug_vsu.v_target = 28.5;
    light_1.max_brightness = 100;
    light_1.color = std::string("white");
    t4_sensor.factor = 0.1;
}

void Systems::pre_load() {
    bat_main_1.inv_internal_r = 1.0f / bat_main_1.internal_r;
}

void Systems::solve_step(void* state, uint32_t step, float dt) {
    // Computed goto dispatch table (static const for one-time init)
    static const void* dispatch_table[60] = {
        &&step_0,
        &&step_1,
        &&step_2,
        &&step_3,
        &&step_4,
        &&step_5,
        &&step_6,
        &&step_7,
        &&step_8,
        &&step_9,
        &&step_10,
        &&step_11,
        &&step_12,
        &&step_13,
        &&step_14,
        &&step_15,
        &&step_16,
        &&step_17,
        &&step_18,
        &&step_19,
        &&step_20,
        &&step_21,
        &&step_22,
        &&step_23,
        &&step_24,
        &&step_25,
        &&step_26,
        &&step_27,
        &&step_28,
        &&step_29,
        &&step_30,
        &&step_31,
        &&step_32,
        &&step_33,
        &&step_34,
        &&step_35,
        &&step_36,
        &&step_37,
        &&step_38,
        &&step_39,
        &&step_40,
        &&step_41,
        &&step_42,
        &&step_43,
        &&step_44,
        &&step_45,
        &&step_46,
        &&step_47,
        &&step_48,
        &&step_49,
        &&step_50,
        &&step_51,
        &&step_52,
        &&step_53,
        &&step_54,
        &&step_55,
        &&step_56,
        &&step_57,
        &&step_58,
        &&step_59
    };

    // Direct jump - no bounds check needed (step % 60 is always 0-59)
    goto *dispatch_table[step % 60];

    step_0:
        step_0(state, dt);
        return;

    step_1:
        step_1(state, dt);
        return;

    step_2:
        step_2(state, dt);
        return;

    step_3:
        step_3(state, dt);
        return;

    step_4:
        step_4(state, dt);
        return;

    step_5:
        step_5(state, dt);
        return;

    step_6:
        step_6(state, dt);
        return;

    step_7:
        step_7(state, dt);
        return;

    step_8:
        step_8(state, dt);
        return;

    step_9:
        step_9(state, dt);
        return;

    step_10:
        step_10(state, dt);
        return;

    step_11:
        step_11(state, dt);
        return;

    step_12:
        step_12(state, dt);
        return;

    step_13:
        step_13(state, dt);
        return;

    step_14:
        step_14(state, dt);
        return;

    step_15:
        step_15(state, dt);
        return;

    step_16:
        step_16(state, dt);
        return;

    step_17:
        step_17(state, dt);
        return;

    step_18:
        step_18(state, dt);
        return;

    step_19:
        step_19(state, dt);
        return;

    step_20:
        step_20(state, dt);
        return;

    step_21:
        step_21(state, dt);
        return;

    step_22:
        step_22(state, dt);
        return;

    step_23:
        step_23(state, dt);
        return;

    step_24:
        step_24(state, dt);
        return;

    step_25:
        step_25(state, dt);
        return;

    step_26:
        step_26(state, dt);
        return;

    step_27:
        step_27(state, dt);
        return;

    step_28:
        step_28(state, dt);
        return;

    step_29:
        step_29(state, dt);
        return;

    step_30:
        step_30(state, dt);
        return;

    step_31:
        step_31(state, dt);
        return;

    step_32:
        step_32(state, dt);
        return;

    step_33:
        step_33(state, dt);
        return;

    step_34:
        step_34(state, dt);
        return;

    step_35:
        step_35(state, dt);
        return;

    step_36:
        step_36(state, dt);
        return;

    step_37:
        step_37(state, dt);
        return;

    step_38:
        step_38(state, dt);
        return;

    step_39:
        step_39(state, dt);
        return;

    step_40:
        step_40(state, dt);
        return;

    step_41:
        step_41(state, dt);
        return;

    step_42:
        step_42(state, dt);
        return;

    step_43:
        step_43(state, dt);
        return;

    step_44:
        step_44(state, dt);
        return;

    step_45:
        step_45(state, dt);
        return;

    step_46:
        step_46(state, dt);
        return;

    step_47:
        step_47(state, dt);
        return;

    step_48:
        step_48(state, dt);
        return;

    step_49:
        step_49(state, dt);
        return;

    step_50:
        step_50(state, dt);
        return;

    step_51:
        step_51(state, dt);
        return;

    step_52:
        step_52(state, dt);
        return;

    step_53:
        step_53(state, dt);
        return;

    step_54:
        step_54(state, dt);
        return;

    step_55:
        step_55(state, dt);
        return;

    step_56:
        step_56(state, dt);
        return;

    step_57:
        step_57(state, dt);
        return;

    step_58:
        step_58(state, dt);
        return;

    step_59:
        step_59(state, dt);
        return;

}

AOT_INLINE void Systems::step_0(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_1(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_2(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_3(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_4(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_5(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_6(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_7(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_8(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_9(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_10(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_11(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_12(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_13(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_14(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_15(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_16(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_17(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_18(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_19(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_20(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_21(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_22(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_23(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_24(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_25(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_26(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_27(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_28(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_29(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_30(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_31(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_32(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_33(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_34(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_35(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_36(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_37(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_38(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_39(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_40(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_41(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_42(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_43(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_44(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_45(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_46(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_47(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_48(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_49(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_50(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_51(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_52(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_53(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_54(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_55(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_56(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_57(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_58(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

AOT_INLINE void Systems::step_59(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    st->clear_through();
    // gnd (no-op)
    bat_main_1.solve_electrical(*st, dt);
    vsu_1.solve_electrical(*st, dt);
    rug_vsu.solve_electrical(*st, dt);
    light_1.solve_electrical(*st, dt);
    t4_sensor.solve_electrical(*st, dt);
    st->precompute_inv_conductance();
    balance_electrical(st, 1.3f);
}

void Systems::post_step(void* state, float dt) {
    auto* st = static_cast<SimulationState*>(state);
    vsu_1.post_step(*st, dt);
    t4_sensor.post_step(*st, dt);
}

AOT_INLINE void Systems::balance_electrical(void* state, float omega) {
    auto* st = static_cast<SimulationState*>(state);
    float* __restrict acc = st->across.data();
    const float* __restrict thr = st->through.data();
    const float* __restrict inv_g = st->inv_conductance.data();

    // Branchless: inv_g[i]=0 for fixed signals, so acc+=0 (no update)
    for (uint32_t i = 0; i < SIGNAL_COUNT; ++i) {
        acc[i] += thr[i] * inv_g[i] * omega;
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
