#include <gtest/gtest.h>
#include "jit_solver/components/all.h"
#include "jit_solver/components/all.cpp"
#include "jit_solver/components/port_registry.h"

using namespace an24;

// Хелпер: создать компонент с привязанными портами
static VoltageSubtract<JitProvider> make_voltage_subtract() {
    VoltageSubtract<JitProvider> comp;
    comp.provider.set(PortNames::Va, 0);
    comp.provider.set(PortNames::Vb, 1);
    comp.provider.set(PortNames::Vo, 2);
    return comp;
}

// Хелпер: создать SimulationState нужного размера
static SimulationState make_state(size_t n = 4) {
    SimulationState st;
    st.across.resize(n, 0.0f);
    st.through.resize(n, 0.0f);
    st.conductance.resize(n, 0.0f);
    return st;
}

TEST(VoltageSubtractTest, BasicSubtraction_Positive) {
    auto comp = make_voltage_subtract();
    auto st = make_state();

    // Va = 30V, Vb = 20V → Vo = 10V
    st.across[0] = 30.0f;  // Va
    st.across[1] = 20.0f;  // Vb

    comp.solve_electrical(st, 1.0f / 60.0f);
    comp.post_step(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[2], 10.0f, 0.001f);  // Vo = 30 - 20
}

TEST(VoltageSubtractTest, BasicSubtraction_Negative) {
    auto comp = make_voltage_subtract();
    auto st = make_state();

    // Va = 20V, Vb = 30V → Vo = -10V (отрицательное!)
    st.across[0] = 20.0f;  // Va
    st.across[1] = 30.0f;  // Vb

    comp.solve_electrical(st, 1.0f / 60.0f);
    comp.post_step(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[2], -10.0f, 0.001f);  // Vo = 20 - 30
}

TEST(VoltageSubtractTest, ZeroInput) {
    auto comp = make_voltage_subtract();
    auto st = make_state();

    // Va = 0V, Vb = 0V → Vo = 0V
    st.across[0] = 0.0f;  // Va
    st.across[1] = 0.0f;  // Vb

    comp.solve_electrical(st, 1.0f / 60.0f);
    comp.post_step(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[2], 0.0f, 0.001f);  // Vo = 0 - 0
}

TEST(VoltageSubtractTest, EqualInputs) {
    auto comp = make_voltage_subtract();
    auto st = make_state();

    // Va = 28V, Vb = 28V → Vo = 0V
    st.across[0] = 28.0f;  // Va
    st.across[1] = 28.0f;  // Vb

    comp.solve_electrical(st, 1.0f / 60.0f);
    comp.post_step(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[2], 0.0f, 0.001f);  // Vo = 28 - 28
}

TEST(VoltageSubtractTest, LargePositive) {
    auto comp = make_voltage_subtract();
    auto st = make_state();

    // Va = 100V, Vb = 5V → Vo = 95V
    st.across[0] = 100.0f;  // Va
    st.across[1] = 5.0f;    // Vb

    comp.solve_electrical(st, 1.0f / 60.0f);
    comp.post_step(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[2], 95.0f, 0.001f);  // Vo = 100 - 5
}

TEST(VoltageSubtractTest, LargeNegative) {
    auto comp = make_voltage_subtract();
    auto st = make_state();

    // Va = 5V, Vb = 100V → Vo = -95V
    st.across[0] = 5.0f;    // Va
    st.across[1] = 100.0f;  // Vb

    comp.solve_electrical(st, 1.0f / 60.0f);
    comp.post_step(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[2], -95.0f, 0.001f);  // Vo = 5 - 100
}

TEST(VoltageSubtractTest, VBus_LowerThan_VGen) {
    auto comp = make_voltage_subtract();
    auto st = make_state();

    // Симуляция подключения к генератору:
    // Va = v_gen = 28.5V, Vb = v_bus = 28V
    st.across[0] = 28.5f;  // Va (генератор)
    st.across[1] = 28.0f;  // Vb (шина)

    comp.solve_electrical(st, 1.0f / 60.0f);
    comp.post_step(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[2], 0.5f, 0.001f);  // Vo = 28.5 - 28 = 0.5V (разница для подключения)
}

TEST(VoltageSubtractTest, VBus_HigherThan_VGen_ReverseCurrent) {
    auto comp = make_voltage_subtract();
    auto st = make_state();

    // Обратный ток:
    // Va = v_gen = 28V, Vb = v_bus = 29V
    st.across[0] = 28.0f;  // Va (генератор)
    st.across[1] = 29.0f;  // Vb (шина)

    comp.solve_electrical(st, 1.0f / 60.0f);
    comp.post_step(st, 1.0f / 60.0f);

    EXPECT_NEAR(st.across[2], -1.0f, 0.001f);  // Vo = 28 - 29 = -1V (обратный ток!)
}

TEST(VoltageSubtractTest, MultipleSteps_Stability) {
    auto comp = make_voltage_subtract();
    auto st = make_state();

    st.across[0] = 30.0f;  // Va
    st.across[1] = 20.0f;  // Vb

    // Несколько шагов симуляции
    for (int i = 0; i < 10; i++) {
        comp.solve_electrical(st, 1.0f / 60.0f);
        comp.post_step(st, 1.0f / 60.0f);
    }

    // Результат должен быть стабильным
    EXPECT_NEAR(st.across[2], 10.0f, 0.001f);
}
