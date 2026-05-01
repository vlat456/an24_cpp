#pragma once

#include "core/solvers/jit/jit_build_input.h"
#include "core/solvers/jit/simulator.h"

#include <cstdint>
#include <optional>

/// Type of a signal at the provider boundary.
/// The simulator uses float[] internally; conversion happens at the boundary.
/// Values match ValType from the wire protocol for zero-cost translation.
enum class SignalType : uint8_t {
    Float32 = 0x00,
    Int32   = 0x01,
    Bool    = 0x02,
};

/// Threshold for float→bool conversion at provider boundary.
/// Values strictly greater than this are considered true.
static constexpr float SIGNAL_BOOL_THRESHOLD = 0.5f;

// Compile-time parity: SignalType values must match ValType from wire protocol.
// These asserts ensure zero-cost conversion between the two enums.
// If any assert fails, update the enum values to maintain parity.
static_assert(static_cast<uint8_t>(SignalType::Float32) == 0x00, "Float32 parity");
static_assert(static_cast<uint8_t>(SignalType::Int32)   == 0x01, "Int32 parity");
static_assert(static_cast<uint8_t>(SignalType::Bool)    == 0x02, "Bool parity");

/// Abstract interface for a simulation variable data provider.
///
/// A provider bridges between the simulator's signal array (values[]) and an
/// external data source (MSFS 2024 via SimConnect, mock for testing, etc.).
///
/// Lifecycle:
///   1. build(input, sim)  — scan blueprint, resolve signal indices
///   2. connect()          — open connection to external source
///   3. Per-frame loop:
///      a. poll(dt)
///      b. read_into(values, count)   — external → simulator
///      c. [Simulator::step()]
///      d. write_from(values, count)  — simulator → external
///   4. disconnect()
///
/// The interface has exactly one virtual call per operation per frame.
/// No per-variable virtual dispatch.
class SimVarProvider {
public:
    virtual ~SimVarProvider() = default;

    SimVarProvider(const SimVarProvider&) = delete;
    SimVarProvider& operator=(const SimVarProvider&) = delete;
    SimVarProvider(SimVarProvider&&) = delete;
    SimVarProvider& operator=(SimVarProvider&&) = delete;

    /// Provider type name (for diagnostics).
    virtual const char* name() const = 0;

    /// Scan build input for SimConnectInput/SimConnectOutput nodes and resolve
    /// signal indices. Called once before connect().
    virtual void build(const JitBuildInput& input, JIT_Simulator& sim) = 0;

    /// Open connection to the external data source.
    virtual bool connect() = 0;

    /// Close connection.
    virtual void disconnect() = 0;

    /// True if the connection is currently open.
    virtual bool is_connected() const = 0;

    /// Process pending external messages (call every frame).
    virtual void poll(double elapsed_time) = 0;

    /// Read external input values into the simulator's values array.
    /// Called after poll(), before Simulator::step().
    virtual void read_into(float* values, uint32_t count) = 0;

    /// Read output values from the simulator's values array and send
    /// them to the external source. Called after Simulator::step().
    virtual void write_from(const float* values, uint32_t count) = 0;

    /// Return the signal type for a given signal index, or std::nullopt
    /// if this provider does not manage the index.
    virtual std::optional<SignalType> signal_type(uint32_t signal_index) const = 0;

protected:
    SimVarProvider() = default;
};
