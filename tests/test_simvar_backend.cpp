#include "core/solvers/common/simvar_backend.h"
#include "simconnect/wire_protocol.h"
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
    // Verify every Type value compiles and has correct 1-based numeric value
    SimVarHandle h;

    h.type = SimVarHandle::AVar;   EXPECT_EQ(static_cast<uint8_t>(h.type), 0x01u);
    h.type = SimVarHandle::LVar;   EXPECT_EQ(static_cast<uint8_t>(h.type), 0x02u);
    h.type = SimVarHandle::HEvent; EXPECT_EQ(static_cast<uint8_t>(h.type), 0x03u);
    h.type = SimVarHandle::BVar;   EXPECT_EQ(static_cast<uint8_t>(h.type), 0x04u);
    h.type = SimVarHandle::EVar;   EXPECT_EQ(static_cast<uint8_t>(h.type), 0x05u);
    h.type = SimVarHandle::IVar;   EXPECT_EQ(static_cast<uint8_t>(h.type), 0x06u);
    h.type = SimVarHandle::OVar;   EXPECT_EQ(static_cast<uint8_t>(h.type), 0x07u);
    h.type = SimVarHandle::ZVar;   EXPECT_EQ(static_cast<uint8_t>(h.type), 0x08u);
}

// Compile-time parity: SimVarHandle::Type and VarType MUST have identical values.
// No offset, no mapping function — they are the same thing.
static_assert(static_cast<uint8_t>(SimVarHandle::AVar)   == static_cast<uint8_t>(VarType::AVar));
static_assert(static_cast<uint8_t>(SimVarHandle::LVar)   == static_cast<uint8_t>(VarType::LVar));
static_assert(static_cast<uint8_t>(SimVarHandle::HEvent) == static_cast<uint8_t>(VarType::HEvent));
static_assert(static_cast<uint8_t>(SimVarHandle::BVar)   == static_cast<uint8_t>(VarType::BVar));
static_assert(static_cast<uint8_t>(SimVarHandle::EVar)   == static_cast<uint8_t>(VarType::EVar));
static_assert(static_cast<uint8_t>(SimVarHandle::IVar)   == static_cast<uint8_t>(VarType::IVar));
static_assert(static_cast<uint8_t>(SimVarHandle::OVar)   == static_cast<uint8_t>(VarType::OVar));
static_assert(static_cast<uint8_t>(SimVarHandle::ZVar)   == static_cast<uint8_t>(VarType::ZVar));

TEST(SimVarHandleTest, TypeEnumMatchesWireProtocolVarType) {
    // Runtime mirror of the static_asserts above — verifies at test time too.
    EXPECT_EQ(static_cast<uint8_t>(SimVarHandle::AVar),   static_cast<uint8_t>(VarType::AVar));
    EXPECT_EQ(static_cast<uint8_t>(SimVarHandle::LVar),   static_cast<uint8_t>(VarType::LVar));
    EXPECT_EQ(static_cast<uint8_t>(SimVarHandle::HEvent), static_cast<uint8_t>(VarType::HEvent));
    EXPECT_EQ(static_cast<uint8_t>(SimVarHandle::BVar),   static_cast<uint8_t>(VarType::BVar));
    EXPECT_EQ(static_cast<uint8_t>(SimVarHandle::EVar),   static_cast<uint8_t>(VarType::EVar));
    EXPECT_EQ(static_cast<uint8_t>(SimVarHandle::IVar),   static_cast<uint8_t>(VarType::IVar));
    EXPECT_EQ(static_cast<uint8_t>(SimVarHandle::OVar),   static_cast<uint8_t>(VarType::OVar));
    EXPECT_EQ(static_cast<uint8_t>(SimVarHandle::ZVar),   static_cast<uint8_t>(VarType::ZVar));
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
