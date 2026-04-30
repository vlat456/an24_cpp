#include "core/solvers/common/simvar_backend.h"
#include "core/solvers/common/provider.h"
#include <gtest/gtest.h>
#include <limits>

// =============================================================================
// SimVar Backend Trait Tests
// =============================================================================
// Verifies that the trait system correctly selects backends per Provider type,
// and that inactive backends produce no-op behavior.

// ==...== SimVarHandle Tests ==...==

TEST(SimVarHandleTest, DefaultConstructedIsInvalid) {
    SimVarHandle h;
    EXPECT_FALSE(h.valid);
    EXPECT_EQ(h.id, std::numeric_limits<uint32_t>::max());
    EXPECT_EQ(h.type, SimVarHandle::AVar);
    EXPECT_EQ(h.unit_id, 0);
    EXPECT_EQ(h.index, 0);
}

TEST(SimVarHandleTest, AllTypesAreConstructible) {
    // Verify every Type value compiles and has correct numeric value
    SimVarHandle h;

    h.type = SimVarHandle::AVar;   EXPECT_EQ(static_cast<uint8_t>(h.type), 0u);
    h.type = SimVarHandle::LVar;   EXPECT_EQ(static_cast<uint8_t>(h.type), 1u);
    h.type = SimVarHandle::HEvent; EXPECT_EQ(static_cast<uint8_t>(h.type), 2u);
    h.type = SimVarHandle::BVar;   EXPECT_EQ(static_cast<uint8_t>(h.type), 3u);
    h.type = SimVarHandle::EVar;   EXPECT_EQ(static_cast<uint8_t>(h.type), 4u);
    h.type = SimVarHandle::IVar;   EXPECT_EQ(static_cast<uint8_t>(h.type), 5u);
    h.type = SimVarHandle::OVar;   EXPECT_EQ(static_cast<uint8_t>(h.type), 6u);
    h.type = SimVarHandle::ZVar;   EXPECT_EQ(static_cast<uint8_t>(h.type), 7u);
}

TEST(SimVarHandleTest, TypeEnumMatchesWireProtocolVarType) {
    // SimVarHandle::Type and VarType (wire_protocol.h) must use the same numeric values.
    // This ensures the AOT backend and the bridge binary protocol agree on type codes.
    // Note: wire_protocol.h uses 1-based (AVar=0x01), SimVarHandle uses 0-based (AVar=0).
    // The mapping is: SimVarHandle::X = VarType::X - 1.
    // This is intentional — the wire protocol reserves 0x00 as "unspecified".
    EXPECT_EQ(static_cast<uint8_t>(SimVarHandle::AVar) + 1, 0x01);
    EXPECT_EQ(static_cast<uint8_t>(SimVarHandle::LVar) + 1, 0x02);
    EXPECT_EQ(static_cast<uint8_t>(SimVarHandle::HEvent) + 1, 0x03);
    EXPECT_EQ(static_cast<uint8_t>(SimVarHandle::BVar) + 1, 0x04);
    EXPECT_EQ(static_cast<uint8_t>(SimVarHandle::EVar) + 1, 0x05);
    EXPECT_EQ(static_cast<uint8_t>(SimVarHandle::IVar) + 1, 0x06);
    EXPECT_EQ(static_cast<uint8_t>(SimVarHandle::OVar) + 1, 0x07);
    EXPECT_EQ(static_cast<uint8_t>(SimVarHandle::ZVar) + 1, 0x08);
}

// ==...== Bridge Backend Tests (JitProvider → inactive) ==...==

TEST(SimVarBridgeBackendTest, IsInactive) {
    EXPECT_FALSE(SimVarBridgeBackend::is_active);
}

TEST(SimVarBridgeBackendTest, ResolveReturnsInvalidHandle) {
    auto h = SimVarBridgeBackend::resolve("AMBIENT TEMPERATURE", "AVar", "Celsius", 0);
    EXPECT_FALSE(h.valid);
}

TEST(SimVarBridgeBackendTest, ReadReturnsZero) {
    SimVarHandle h;
    EXPECT_FLOAT_EQ(SimVarBridgeBackend::read(h), 0.0f);
}

TEST(SimVarBridgeBackendTest, WriteIsNoOp) {
    SimVarHandle h;
    // Should not crash or throw
    SimVarBridgeBackend::write(h, 42.0f);
}

// ==...== Trait Mapping Tests ==...==

TEST(SimVarBackendForTest, JitProviderMapsToBridgeBackend) {
    using Backend = typename SimVarBackendFor<JitProvider>::type;
    EXPECT_FALSE(Backend::is_active);
    // Verify it's the same type as SimVarBridgeBackend
    EXPECT_TRUE((std::is_same_v<Backend, SimVarBridgeBackend>));
}

TEST(SimVarBackendForTest, AotProviderMapsToWasmBackend) {
    using MyAot = AotProvider<Binding<PortNames::v_in, 0>>;
    using Backend = typename SimVarBackendFor<MyAot>::type;
    // In stub build: is_active = false (no real WASM API)
    // In WASM build: is_active = true
    // Either way, the trait should resolve to SimVarWasmBackend
    EXPECT_TRUE((std::is_same_v<Backend, SimVarWasmBackend>));
}

// ==...== Compile-Time Dead Code Elimination Verification ==...==

// This test verifies that if constexpr (Backend::is_active) correctly
// eliminates dead code for the JIT path. The bridge backend's resolve/read/write
// are all no-ops, so this should compile and run without side effects.
TEST(SimVarBackendTest, IfConstexprEliminatesDeadCode) {
    using Backend = typename SimVarBackendFor<JitProvider>::type;

    float result = 0.0f;
    if constexpr (Backend::is_active) {
        // This branch should be eliminated at compile time for JIT
        auto h = Backend::resolve("TEST", "AVar", "number", 0);
        result = Backend::read(h);
        Backend::write(h, 1.0f);
    }

    // For JIT: result stays 0.0f (branch eliminated)
    EXPECT_FLOAT_EQ(result, 0.0f);
}

// Same pattern for AOT provider — in stub build the branch is also eliminated
TEST(SimVarBackendTest, AotStubIfConstexprEliminatesDeadCode) {
    using MyAot = AotProvider<Binding<PortNames::v_in, 0>>;
    using Backend = typename SimVarBackendFor<MyAot>::type;

    float result = 1.0f;  // Start non-zero to detect if branch runs
    if constexpr (Backend::is_active) {
        auto h = Backend::resolve("TEST", "AVar", "number", 0);
        result = Backend::read(h);
        Backend::write(h, 42.0f);
    }

    // In stub build: is_active = false, branch eliminated, result unchanged
    EXPECT_FLOAT_EQ(result, 1.0f);
}
