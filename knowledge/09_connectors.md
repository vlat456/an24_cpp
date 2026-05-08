# Connector/Provider I/O Architecture

## Overview

The simulator communicates with external flight simulators (MSFS 2024, X-Plane, etc.) through a **pluggable provider architecture**. Each simulator gets its own connector directory with provider-specific I/O nodes.

```
External Simulator ←→ Provider ←→ Connector Nodes ←→ Simulator Core
     (MSFS)          (SimConnect)  (SimConnectInput)   (values[])
```

## Directory Structure

```
library/connectors/
  └── simconnect/
      ├── SimConnectInput.blueprint    # Reads MSFS variable → signal
      └── SimConnectOutput.blueprint   # Writes signal → MSFS variable

  # Future:
  # └── xplane/
  #     ├── XPlaneInput.blueprint
  #     └── XPlaneOutput.blueprint
```

## Key Files

| File | Purpose |
|------|---------|
| `src/core/solvers/jit/bridge/simvar_provider.h` | `SimVarProvider` abstract interface + `SignalType` enum |
| `src/core/solvers/jit/bridge/simvar_provider_host.h/.cpp` | Orchestrator: scans build input, partitions by ComponentKind, delegates frame loop |
| `src/simconnect/simconnect_provider.h/.cpp` | MSFS adapter: implements `SimVarProvider` with direct V2 delta protocol |
| `src/core/solvers/jit/bridge/mock_provider.h/.cpp` | Headless test provider with typed storage |
| `src/core/solvers/jit/components/sim_connect_input.h/.cpp` | JIT passthrough component |
| `src/core/solvers/jit/components/sim_connect_output.h/.cpp` | JIT passthrough component |
| `src/simconnect/wire_protocol.h` | V2 delta wire protocol structs |
| `src/simconnect/wire_codec.h/.cpp` | Wire codec encode/decode |
| `src/simconnect/simconnect_client.h/.cpp` | SimConnect client abstraction |
| `src/simconnect/simconnect_mapping.h/.cpp` | Variable name → signal mapping |
| `src/simconnect/intern_table.h/.cpp` | String interning for wire protocol |

## SignalType Enum

Provider-agnostic type at the boundary. Matches `ValType` from wire protocol.

```cpp
enum class SignalType : uint8_t {
    Float32 = 0x00,
    Int32   = 0x01,
    Bool    = 0x02,
};
```

**Conversion rules:**
- `Float32` → `float`: pass through
- `Int32` → `float`: `static_cast<float>(i32)`
- `Bool` → `float`: `u32 != 0 ? 1.0f : 0.0f`

**Bool threshold:** `SIGNAL_BOOL_THRESHOLD = 0.5f` — values strictly greater than this are considered `true` when converting float→bool.

## SimVarProvider Interface

```cpp
class SimVarProvider {
public:
    virtual const char* name() const = 0;
    virtual void build(const JitBuildInput& input, JIT_Simulator& sim) = 0;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool is_connected() const = 0;
    virtual void poll(double elapsed_time) = 0;
    virtual void read_into(float* values, uint32_t count) = 0;
    virtual void write_from(const float* values, uint32_t count) = 0;
    virtual std::optional<SignalType> signal_type(uint32_t signal_index) const = 0;
};
```

**Lifecycle:**
1. `build(input, sim)` — scan connector nodes, intern var names, resolve signal indices
2. `connect()` — open transport, register variables
3. Per-frame loop:
   - `poll(dt)` — process messages, heartbeat
   - `read_into(values, count)` — inject inputs into values[]
   - `write_from(values, count)` — extract outputs to provider
4. `disconnect()` — cleanup

## SimConnect V2 Delta Protocol

Instead of sending all variable values every frame, the host sends an 8-byte `DeltaRead` with a tier bitmask. The WASM bridge responds with `DeltaUpdate` (only changed records) or `FullSync` (all records, periodic safety net).

**Wire protocol:** binary packed structs on the frame channel (`An24Bridge_Frame`).
**Setup/registration:** JSON on the control channel (`An24Bridge_Control`).

See `knowledge/12_simconnect.md` for full protocol details.

## Files

| File | Purpose |
|------|---------|
| `src/core/solvers/jit/bridge/simvar_provider.h` | SimVarProvider interface |
| `src/core/solvers/jit/bridge/simvar_provider_host.h` | Provider host orchestrator |
| `src/core/solvers/jit/bridge/mock_provider.h` | Mock provider for tests |
| `src/simconnect/simconnect_provider.h` | SimConnectProvider implementation |
| `src/simconnect/wire_protocol.h` | Wire protocol structs |
| `src/simconnect/wire_codec.h` | Wire codec |
| `src/simconnect/simconnect_client.h` | SimConnect client |
| `src/simconnect/simconnect_mapping.h` | Variable mapping |
| `src/simconnect/intern_table.h` | String intern table |
