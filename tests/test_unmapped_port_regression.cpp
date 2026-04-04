/// Regression tests for [BUG-21]: JitProvider returning 0 for unmapped ports.
///
/// The old JitProvider::get() returned 0 (a valid signal index) when a port
/// wasn't mapped. This caused unmapped port accesses to silently read/write
/// signal index 0, corrupting whatever signal happened to live there.
///
/// The fix uses UINT32_MAX as a sentinel value and adds has() for safe checks.

#include <gtest/gtest.h>
#include "jit_solver/components/provider.h"
#include "jit_solver/components/port_registry.h"

// =============================================================================
// Sentinel Value Tests
// =============================================================================

TEST(UnmappedPortRegression, UnmappedConstant_IsUINT32MAX) {
    EXPECT_EQ(JitProvider::UNMAPPED, UINT32_MAX);
}

TEST(UnmappedPortRegression, MappedPort_ReturnsCorrectIndex) {
    JitProvider p;
    p.set(PortNames::v_out, 42);

    EXPECT_EQ(p.get(PortNames::v_out), 42u);
}

TEST(UnmappedPortRegression, Has_ReturnsTrueForMapped) {
    JitProvider p;
    p.set(PortNames::v_out, 7);

    EXPECT_TRUE(p.has(PortNames::v_out));
}

TEST(UnmappedPortRegression, Has_ReturnsFalseForUnmapped) {
    JitProvider p;
    // Don't map v_in
    p.set(PortNames::v_out, 7);

    EXPECT_FALSE(p.has(PortNames::v_in));
}

TEST(UnmappedPortRegression, MultiplePortsMapped) {
    JitProvider p;
    p.set(PortNames::v_in, 0);
    p.set(PortNames::v_out, 1);
    p.set(PortNames::control, 2);

    EXPECT_EQ(p.get(PortNames::v_in), 0u);
    EXPECT_EQ(p.get(PortNames::v_out), 1u);
    EXPECT_EQ(p.get(PortNames::control), 2u);

    EXPECT_TRUE(p.has(PortNames::v_in));
    EXPECT_TRUE(p.has(PortNames::v_out));
    EXPECT_TRUE(p.has(PortNames::control));
    EXPECT_FALSE(p.has(PortNames::heat_in));  // not mapped
}

TEST(UnmappedPortRegression, IndexZero_IsValidAndDistinguishable) {
    // Critical regression: index 0 must be a valid mappable index,
    // not confused with "unmapped".
    JitProvider p;
    p.set(PortNames::v_in, 0);

    EXPECT_TRUE(p.has(PortNames::v_in));
    EXPECT_EQ(p.get(PortNames::v_in), 0u);

    // Another port that IS unmapped should NOT return 0
    EXPECT_FALSE(p.has(PortNames::v_out));
}

TEST(UnmappedPortRegression, SetOverwritesPrevious) {
    JitProvider p;
    p.set(PortNames::v_out, 10);
    EXPECT_EQ(p.get(PortNames::v_out), 10u);

    p.set(PortNames::v_out, 20);
    EXPECT_EQ(p.get(PortNames::v_out), 20u);
}

// =============================================================================
// Domain dispatch regression: HoldButton and ElectricPump domains
// =============================================================================

TEST(DomainDispatchRegression, HoldButton_IsElectricalDomain) {
    // [BUG-4]: HoldButton was Domain::Logical, which caused the simulator
    // to skip its solve_electrical() method entirely.
    using HB = HoldButton<JitProvider>;
    EXPECT_TRUE((static_cast<uint32_t>(HB::domain) & static_cast<uint32_t>(Domain::Electrical)) != 0)
        << "HoldButton must include Electrical domain to be dispatched by the electrical solver";
}

TEST(DomainDispatchRegression, ElectricPump_IncludesElectricalAndHydraulic) {
    // [BUG-5]: ElectricPump was Domain::Hydraulic only, missing Electrical.
    // Both electrical and hydraulic solvers must dispatch to it.
    using EP = ElectricPump<JitProvider>;
    uint32_t d = static_cast<uint32_t>(EP::domain);

    EXPECT_TRUE((d & static_cast<uint32_t>(Domain::Electrical)) != 0)
        << "ElectricPump must include Electrical domain";
    EXPECT_TRUE((d & static_cast<uint32_t>(Domain::Hydraulic)) != 0)
        << "ElectricPump must include Hydraulic domain";
}

TEST(DomainDispatchRegression, Controllers_AreLogicalDomain) {
    using PIDComp = PID<JitProvider>;
    using PIComp = PI<JitProvider>;
    using PDComp = PD<JitProvider>;
    using PComp = P<JitProvider>;

    EXPECT_EQ(PIDComp::domain, Domain::Logical);
    EXPECT_EQ(PIComp::domain, Domain::Logical);
    EXPECT_EQ(PDComp::domain, Domain::Logical);
    EXPECT_EQ(PComp::domain, Domain::Logical);
}
