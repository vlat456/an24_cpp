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
| `src/simconnect/simconnect_provider.h/.cpp` | MSFS adapter: implements `SimVarProvider` with direct V2 delta protocol (replaces former `simconnect_bridge`) |
| `src/core/solvers/jit/bridge/mock_provider.h/.cpp` | Headless test provider with typed storage |
| `src/core/solvers/jit/components/sim_connect_input.h/.cpp` | JIT passthrough component (AOT uses Vars API directly) |
| `src/core/solvers/jit/components/sim_connect_output.h/.cpp` | JIT passthrough component |

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
1. `build(input, sim)` — scan connector nodes, resolve signal indices
2. `connect()` — open connection to external sim
3. Per-frame loop:
   - `poll(dt)` — process messages
   - `read_into(values, count)` — external → simulator
   - `[Simulator::step()]`
   - `write_from(values, count)` — simulator → external
4. `disconnect()`

**Exactly one virtual call per operation per frame.** No per-variable virtual dispatch.

## Provider Host

`SimvarProviderHost` owns zero or more provider instances and delegates the frame loop to each.

**Connection model:**
- **Enabled** = persistent user preference, toggled via the Adapters menu. Survives sim start/stop/rebuild.
- **Providers** are created at sim start from blueprint connector nodes. When built, auto-connected if their type is enabled.
- **Sim stop** destroys providers but does NOT clear the enabled set.
- **Menu** shows `[On]` / `[Off]` for each registered type. Always visible, always clickable.

**Registration (once at app startup):**
```cpp
SimConnectProvider::register_type();  // registers "simconnect" factory
```

This must be called once at application startup **before** `registered_types()` or `build()` is used. In the editor, this happens in `EditorApp::run()`. In test executables and example hosts, call it in `main()` before creating a `SimvarProviderHost`.

**Query registered types (editor UI):**
```cpp
auto types = SimvarProviderHost::registered_types();  // sorted vector<string>
// e.g. {"simconnect"}
```

**Toggle enabled (menu click):**
```cpp
bool now_enabled = host.toggle_enabled("simconnect");
host.is_enabled("simconnect");  // true
```

**Routing by ComponentKind (not by param):**
```cpp
static std::string provider_type_for_kind(ComponentKind kind) {
    switch (kind) {
        case ComponentKind::SimConnectInput:
        case ComponentKind::SimConnectOutput:
            return "simconnect";
        // future: case ComponentKind::XPlaneInput: return "xplane";
        default: return "";
    }
```

The host groups all `SimConnectInput`/`SimConnectOutput` nodes together and creates **one** `SimConnectProvider` instance for the group.

## Editor Integration

### Adapters Menu

The editor main menu bar contains an **Adapters** menu (always visible when a document is open). It lists all registered provider types with their enabled state:

- `simconnect   [On]` — adapter is enabled; will auto-connect when sim starts with connector nodes
- `simconnect   [Off]` — adapter is disabled; no data flows

Click toggles the enabled preference. This is independent of simulation state — you can enable/disable adapters before starting or while simulation is running.

**Implementation:** `src/editor/visual/menu/main_menu.cpp::renderAdaptersMenu()`

### SimulationBridge Lifecycle

`SimulationBridge` (owned by `Document`) contains a `SimvarProviderHost`. The lifecycle:

1. **Menu click** → `host.toggle_enabled("simconnect")` → enabled set updated. If providers exist, connects/disconnects immediately.
2. **`Document::startSimulation()`** → `SimulationBridge::start()`
   - `provider_host_.build(input, sim)` — creates providers from connector nodes
   - If a type is enabled, its provider auto-connects
3. **Every frame:** `provider_host_.poll(dt)` → `read_into()` → `sim.step()` → `write_from()`
4. **`Document::stopSimulation()`** → `SimulationBridge::stop()`
   - Providers destroyed with sim instance
   - Enabled set **NOT cleared** — user preference persists

## Connector Nodes (JIT Mode)

In JIT/Editor mode, connector components are **passthrough graph nodes**:

```cpp
void SimConnectInput<JitProvider>::execute(SimulationState& st, double /*dt*/) {
    // JIT: no-op — SimConnectProvider injected values into values[] before step
    // AOT: reads directly from Vars API via resolved handle
}
```

The node exists in the graph to:
1. Declare which signal index the provider writes to
2. Carry MSFS-specific params (`var_name`, `var_type`, `unit`, `tier`, `epsilon`, `val_type`)
3. Appear in the editor as a labeled node with ports

## Connector Nodes (AOT/WASM Mode)

In AOT/WASM, the same component template resolves the variable at `pre_load()` and reads/writes directly via the MSFS 2024 Vars API:

```cpp
void pre_load() {
    handle_ = Backend::resolve(var_name, var_type, unit, index);
}

void execute(SimulationState& st, double) {
    if (handle_.valid) {
        st.values[provider.get(PortNames::out)] = Backend::read(handle_);
    }
}
```

No provider, no bridge, no wire protocol — direct API calls.

## SimConnect Blueprint Params

### SimConnectInput

```json
{
    "var_name": "",          // MSFS variable name (e.g., "AMBIENT TEMPERATURE")
    "var_type": "AVar",      // AVar|LVar|HEvent|BVar|EVar|IVar|OVar|ZVar
    "unit": "number",        // MSFS unit string
    "index": "0",            // 0-based index
    "default_value": "0.0",  // Value when not connected
    "val_type": "Float32",   // Float32|Int32|Bool — wire value type
    "tier": "1",             // Polling tier: 0=fast(every frame), 1=medium(every 5th), 2=slow(every 30th)
    "epsilon": "0.01"        // Change detection threshold for delta protocol
}
```

### SimConnectOutput

```json
{
    "var_name": "",
    "var_type": "AVar",
    "unit": "number",
    "index": "0",
    "mode": "data",          // "data" (set value) or "event" (trigger event)
    "event_name": "",        // For event mode
    "event_id": "0",         // For event mode
    "val_type": "Float32"    // Float32|Int32|Bool
}
```

## Type Conversion at Boundary

The simulator uses `float values[]` internally. Type conversion happens at the provider boundary:

```
MSFS (int32 AVar) ←→ WireValue(i32) ←→ SimConnectProvider
                                      ↓
                                   float(values[idx])
                                      ↓
                                   Simulator
```

**WireValue union (4 bytes):**
```cpp
union WireValue {
    float    f32;
    int32_t  i32;
    uint32_t u32;  // Bool stored as 0/1
};
```

**Conversion helpers:**
```cpp
static float wire_value_to_float(const WireValue& wv, ValType vt) {
    switch (vt) {
        case ValType::Float32: return wv.f32;
        case ValType::Int32:   return static_cast<float>(wv.i32);
        case ValType::Bool:    return wv.u32 != 0 ? 1.0f : 0.0f;
    }
}

static WireValue float_to_wire_value(float value, ValType vt) {
    switch (vt) {
        case ValType::Float32: return WireValue(value);
        case ValType::Int32:   return WireValue(static_cast<int32_t>(value));
        case ValType::Bool:    return WireValue(value > SIGNAL_BOOL_THRESHOLD);
    }
}
```

## Adding a New Connector (e.g., X-Plane)

1. **Create blueprints:**
   ```
   library/connectors/xplane/XPlaneInput.blueprint
   library/connectors/xplane/XPlaneOutput.blueprint
   ```
   Use X-Plane-specific params (`dataref`, `dataref_type`, etc.).

2. **Create component C++ files:**
   ```
   src/core/solvers/jit/components/xplane_input.h/.cpp
   src/core/solvers/jit/components/xplane_output.h/.cpp
   ```
   Template classes following `SimConnectInput` pattern.

3. **Add to `all.h` and `CMakeLists.txt`**

4. **Regenerate code:**
   ```bash
   cmake --build build --target update_port_registry
   ./build/tools/update_port_registry library
   ```
   This regenerates `component_kind.h`, `build_factory.cpp`, `port_registry.h`.

5. **Create provider:**
   ```
   src/xplane/xplane_provider.h/.cpp
   ```
   Implement `SimVarProvider` interface. Use XPLM DataRef API.

6. **Register provider type:**
   ```cpp
   XPlaneProvider::register_type();  // before host.build()
   ```

7. **Update `provider_type_for_kind()` in `simvar_provider_host.cpp`:**
   ```cpp
   case ComponentKind::XPlaneInput:
   case ComponentKind::XPlaneOutput:
       return "xplane";
   ```

## Testing

Use `MockProvider` for headless tests:

```cpp
MockProvider provider;
provider.build(input, sim);
provider.set_input(0, int32_t{42});  // typed input
provider.read_into(values, count);
EXPECT_FLOAT_EQ(values[0], 42.0f);   // converted to float
```

Test file: `tests/test_mock_provider.cpp` — 20 tests covering typed round-trip, bounds, lifecycle.

## Related Files

- `src/simconnect/wire_protocol.h` — V2 delta protocol: `PacketHeader`, `VarRecord`, `WireValue`, `ValType`
- `src/simconnect/wire_codec.h` — Zero-allocation packet builder/parser
- `src/simconnect/simconnect_mapping.h` — `SimVarMapping` with `val_type`, `tier`, `epsilon` fields
- `examples/an24_simconnect_host/main.cpp` — Example host using `SimvarProviderHost`

## GitHub Issues

| Issue | Description |
|-------|-------------|
| #459 | Milestone: Pluggable SimVarProvider interface |
| #460 | Phase 1: Extract SimVarProvider interface + MockProvider |
| #461 | Phase 2: ProviderHost + SimConnect adapter |
| #462 | Provider type awareness — int/bool/float conversion |
| #464 | Connector-specific I/O nodes |
| #465 | Umbrella: Connection protocol implementation cleanup (8 sub-issues) |
| #466 | Merge SimConnectBridge into SimConnectProvider |
| #467 | Delete dead JIT_Simulator-coupled methods |
| #468 | Remove unused `JIT_Simulator&` from `build_mappings()` |
| #469 | Update stale comments |
| #470 | SignalType/ValType compile-time parity checks |
| #471 | WireCodec methods static |
| #472 | Avoid PacketHeader copy in hot path |
| #473 | Binary `send_bytes` API on SimConnectClient |
| #474 | Umbrella: Adapters menu in editor |
| #475 | Expose registered provider types from registry |
| #476 | Integrate ProviderHost into SimulationBridge |
| #477 | Render Adapters menu in MainMenu |
| #478 | Auto-connect adapters on simulation start |
