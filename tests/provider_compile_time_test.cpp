#include "jit_solver/components/provider_components.h"
#include <iostream>
#include <cassert>

using namespace an24;

// Test that AotProvider::get() is constexpr
void test_constexpr() {
    using MyProvider = AotProvider<
        Binding<PortNames::v_in, 5>,
        Binding<PortNames::v_out, 10>
    >;

    // Compile-time constant test
    constexpr uint32_t v_in_idx = MyProvider::get(PortNames::v_in);
    constexpr uint32_t v_out_idx = MyProvider::get(PortNames::v_out);

    static_assert(v_in_idx == 5, "v_in_idx should be 5 at compile time");
    static_assert(v_out_idx == 10, "v_out_idx should be 10 at compile time");

    std::cout << "✅ AotProvider::get() is constexpr!\n";
    std::cout << "   v_in_idx = " << v_in_idx << " (compile-time constant)\n";
    std::cout << "   v_out_idx = " << v_out_idx << " (compile-time constant)\n";
}

int main() {
    std::cout << "╔════════════════════════════════════════════════════════╗\n";
    std::cout << "║  Compile-Time Optimization Test                      ║\n";
    std::cout << "╚════════════════════════════════════════════════════════╝\n\n";

    test_constexpr();

    std::cout << "\n╔════════════════════════════════════════════════════════╗\n";
    std::cout << "║  ✅ AotProvider generates compile-time constants!    ║\n";
    std::cout << "║  This means: st->across[0] instead of indirect!      ║\n";
    std::cout << "╚════════════════════════════════════════════════════════╝\n";

    return 0;
}
