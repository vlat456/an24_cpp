#include "jit_solver/components/provider_components.h"
#include "jit_solver/components/port_registry.h"
#include "jit_solver/state.h"
#include <iostream>
#include <cassert>

using namespace an24;

void test_aot_provider() {
    std::cout << "=== Testing AotProvider (constexpr) ===\n";

    // AOT component with constexpr port indices
    using MyBattery = Battery_Provider<
        AotProvider<
            Binding<PortNames::v_in, 0>,
            Binding<PortNames::v_out, 1>
        >
    >;

    MyBattery bat;
    bat.v_nominal = 24.0f;
    bat.internal_r = 0.1f;
    bat.pre_load();

    SimulationState st;
    st.across.resize(2);
    st.through.resize(2);

    st.across[0] = 0.0f;  // v_in (gnd)
    st.across[1] = 20.0f; // v_out (bus)

    bat.solve_electrical(st, 0.001f);

    std::cout << "Battery AOT test:\n";
    std::cout << "  v_gnd = " << st.across[0] << "V\n";
    std::cout << "  v_bus = " << st.across[1] << "V\n";
    std::cout << "  i = " << st.through[1] << "A\n";
    std::cout << "  Expected: I ≈ (24 + 0 - 20) / 0.1 = 40A\n";

    assert(st.through[1] > 39.0f && st.through[1] < 41.0f);
    std::cout << "✅ AotProvider test passed!\n\n";
}

void test_jit_provider() {
    std::cout << "=== Testing JitProvider (runtime) ===\n";

    // JIT component with runtime port indices
    using MyBattery = Battery_Provider<JitProvider>;

    MyBattery bat;
    bat.v_nominal = 24.0f;
    bat.internal_r = 0.1f;
    bat.pre_load();

    // Simulate JSON parsing - set port indices at runtime
    bat.provider.set(PortNames::v_in, 0);
    bat.provider.set(PortNames::v_out, 1);

    SimulationState st;
    st.across.resize(2);
    st.through.resize(2);

    st.across[0] = 0.0f;  // v_in (gnd)
    st.across[1] = 20.0f; // v_out (bus)

    bat.solve_electrical(st, 0.001f);

    std::cout << "Battery JIT test:\n";
    std::cout << "  v_gnd = " << st.across[0] << "V\n";
    std::cout << "  v_bus = " << st.across[1] << "V\n";
    std::cout << "  i = " << st.through[1] << "A\n";
    std::cout << "  Expected: I ≈ (24 + 0 - 20) / 0.1 = 40A\n";

    assert(st.through[1] > 39.0f && st.through[1] < 41.0f);
    std::cout << "✅ JitProvider test passed!\n\n";
}

void test_resistor_aot() {
    std::cout << "=== Testing Resistor AotProvider ===\n";

    using MyResistor = Resistor_Provider<
        AotProvider<
            Binding<PortNames::v_in, 0>,
            Binding<PortNames::v_out, 1>
        >
    >;

    MyResistor r;
    r.conductance = 0.5f; // 2 Ohm

    SimulationState st;
    st.across.resize(2);
    st.through.resize(2);

    st.across[0] = 10.0f; // v_in
    st.across[1] = 5.0f;  // v_out

    r.solve_electrical(st, 0.001f);

    std::cout << "Resistor AOT test:\n";
    std::cout << "  v_in = " << st.across[0] << "V\n";
    std::cout << "  v_out = " << st.across[1] << "V\n";
    std::cout << "  i = " << st.through[1] << "A\n";
    std::cout << "  Expected: I = (10 - 5) * 0.5 = 2.5A\n";

    assert(st.through[1] > 2.4f && st.through[1] < 2.6f);
    std::cout << "✅ Resistor AotProvider test passed!\n\n";
}

void test_comparator_aot() {
    std::cout << "=== Testing Comparator AotProvider ===\n";

    using MyComparator = Comparator_Provider<
        AotProvider<
            Binding<PortNames::Va, 0>,
            Binding<PortNames::Vb, 1>,
            Binding<PortNames::o, 2>
        >
    >;

    MyComparator comp;
    comp.Von = 0.1f;
    comp.Voff = -0.1f;

    SimulationState st;
    st.across.resize(3);

    // Test case 1: Va > Vb
    st.across[0] = 5.0f; // Va
    st.across[1] = 0.0f; // Vb
    st.across[2] = 0.0f; // output

    comp.solve_logical(st, 0.001f);

    std::cout << "Comparator AOT test:\n";
    std::cout << "  Test 1: Va=5V, Vb=0V\n";
    std::cout << "  Output = " << st.across[2] << "V (expected 1.0V)\n";
    assert(st.across[2] == 1.0f);

    // Test case 2: Va < Vb — output must turn OFF (diff = -5.0 < Voff = -0.1)
    st.across[0] = 0.0f; // Va
    st.across[1] = 5.0f; // Vb
    comp.solve_logical(st, 0.001f);

    std::cout << "  Test 2: Va=0V, Vb=5V (after turn-on)\n";
    std::cout << "  Output = " << st.across[2] << "V (expected 0.0V — diff below Voff)\n";
    assert(st.across[2] == 0.0f);

    // Test case 3: Hysteresis band — Va=4.95, Vb=0 (diff=4.95, in band [Voff=-0.1, Von=0.1])
    // First re-arm: set output ON
    st.across[0] = 5.0f; st.across[1] = 0.0f;
    comp.solve_logical(st, 0.001f);
    assert(st.across[2] == 1.0f);  // ON

    // Now drop into hysteresis keep band: diff=0.05, which is > Voff(-0.1) but < Von(0.1)
    st.across[0] = 0.05f; st.across[1] = 0.0f;
    comp.solve_logical(st, 0.001f);

    std::cout << "  Test 3: Va=0.05V, Vb=0V (hysteresis keep band)\n";
    std::cout << "  Output = " << st.across[2] << "V (expected 1.0V — in keep band)\n";
    assert(st.across[2] == 1.0f);

    std::cout << "✅ Comparator AotProvider test passed!\n\n";
}

int main() {
    std::cout << "╔════════════════════════════════════════════════════════╗\n";
    std::cout << "║  Provider Pattern Test - Zero Overhead Abstraction     ║\n";
    std::cout << "╚════════════════════════════════════════════════════════╝\n\n";

    test_aot_provider();
    test_jit_provider();
    test_resistor_aot();
    test_comparator_aot();

    std::cout << "╔════════════════════════════════════════════════════════╗\n";
    std::cout << "║  🎉 All tests passed! Provider pattern works!         ║\n";
    std::cout << "╚════════════════════════════════════════════════════════╝\n";

    return 0;
}
