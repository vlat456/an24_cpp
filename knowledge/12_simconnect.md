# SimConnect Integration

MSFS 2024 integration via SimConnect with a custom V2 delta wire protocol.

## Overview

```
┌─────────────┐     V2 Delta Wire      ┌──────────────┐
│  an24_editor │ ←→ Protocol (binary)  │  WASM Bridge │
│  (host)      │     on frame channel   │  (in-sim)    │
└─────────────┘                        └──────────────┘
       ↑                                        ↑
       │                                        │
  SimConnectProvider                      SimConnect API
       │                                        │
  read_into() / write_from()           Variable registration
```

## Architecture

### SimConnectProvider
Implements `SimVarProvider` interface. Editor-only, not part of WASM build.

```cpp
class SimConnectProvider final : public SimVarProvider {
public:
    void build(const JitBuildInput& input, JIT_Simulator& sim) override;
    bool connect() override;
    void disconnect() override;
    bool is_connected() const override;
    void poll(double elapsed_time) override;
    void read_into(float* values, uint32_t count) override;
    void write_from(const float* values, uint32_t count) override;
    std::optional<SignalType> signal_type(uint32_t signal_index) const override;

    bool is_alive() const;  // WASM bridge responding to pings
};
```

File: `src/simconnect/simconnect_provider.h`

### V2 Delta Wire Protocol

Instead of sending all variable values every frame:

**Host → WASM:** `DeltaRead` (8 bytes, tier bitmask)
**WASM → Host:** `DeltaUpdate` (only changed records) or `FullSync` (all records, periodic safety net)

**Channels:**
- **Frame channel** (`An24Bridge_Frame`): Binary packed structs, high frequency
- **Control channel** (`An24Bridge_Control`): JSON, setup/registration

### Wire Protocol Structs

```cpp
// Host requests read for specific tiers
struct DeltaRead {
    uint32_t tier_mask;  // Bitmask of tiers to read
};

// WASM responds with changed records
struct DeltaUpdate {
    uint16_t record_count;
    // Followed by Record[]: { uint16_t index; float value; }
};

// Periodic full sync (safety net)
struct FullSync {
    uint16_t record_count;
    // Followed by float[] values for all records
};

// Host writes outputs
struct DeltaWrite {
    uint16_t record_count;
    // Followed by Record[]: { uint16_t index; float value; }
};
```

File: `src/simconnect/wire_protocol.h`

### WireCodec
Encodes/decodes wire protocol structs:
```cpp
class WireCodec {
public:
    static std::vector<uint8_t> encode_delta_read(const DeltaRead& msg);
    static std::vector<uint8_t> encode_delta_write(const DeltaWrite& msg);
    static std::optional<DeltaUpdate> decode_delta_update(const uint8_t* data, size_t len);
    static std::optional<FullSync> decode_full_sync(const uint8_t* data, size_t len);
};
```

File: `src/simconnect/wire_codec.h`

## Data Flow

### Build Phase
1. `build_mappings()` — scan `JitBuildInput` for SimConnectInput/Output nodes
2. Intern variable names via `InternTable`
3. Resolve signal indices from port_to_signal map
4. Categorize by tier (frequency band)

### Connect Phase
1. Open SimConnect pipe
2. Register variables with WASM bridge via control channel (JSON)
3. Start frame channel communication

### Per-Frame Loop
1. `poll(dt)` — process SimConnect messages, heartbeat
2. `read_into(values, count)` — request inputs via DeltaRead, inject buffered DeltaUpdate/FullSync into values[]
3. `write_from(values, count)` — extract output signals, send DeltaWrite to WASM bridge

## Mapping

Variable name → signal index mapping:
```cpp
class SimConnectMapping {
    std::unordered_map<std::string, uint32_t> name_to_index_;
    std::vector<std::string> index_to_name_;
public:
    uint32_t resolve(const std::string& name) const;
    const std::string& name(uint32_t index) const;
};
```

File: `src/simconnect/simconnect_mapping.h`

## Intern Table

String interning for wire protocol to reduce bandwidth:
```cpp
class InternTable {
    std::unordered_map<std::string, uint32_t> table_;
    std::vector<std::string> reverse_;
public:
    uint32_t intern(const std::string& s);
    const std::string& resolve(uint32_t id) const;
};
```

File: `src/simconnect/intern_table.h`

## Files

| File | Purpose |
|------|---------|
| `src/simconnect/simconnect_provider.h/.cpp` | SimConnectProvider |
| `src/simconnect/wire_protocol.h` | Protocol structs |
| `src/simconnect/wire_codec.h/.cpp` | Encode/decode |
| `src/simconnect/simconnect_client.h/.cpp` | SimConnect client abstraction |
| `src/simconnect/simconnect_client_stub.cpp` | Stub client for testing |
| `src/simconnect/simconnect_mapping.h/.cpp` | Variable mapping |
| `src/simconnect/intern_table.h/.cpp` | String interning |
| `src/core/solvers/jit/bridge/simvar_provider.h` | SimVarProvider interface |
| `src/core/solvers/jit/bridge/simvar_provider_host.h/.cpp` | Provider host |
| `src/core/solvers/jit/components/sim_connect_input.h/.cpp` | Input component |
| `src/core/solvers/jit/components/sim_connect_output.h/.cpp` | Output component |
| `src/simconnect/simvar_catalog.h/.cpp` | SimVar catalog singleton for UI dropdowns |
| `src/simconnect/simconnect_coordinator.h/.cpp` | Shared SimConnect client coordinator |

## SimVar Catalog

The `SimVarCatalog` singleton provides a unified variable registry for the editor's SimConnectInput/SimConnectOutput property dropdowns:

- **AVars** — loaded from `resources/simvar_catalog.json` (~108 curated variables) at editor startup
- **LVars** — populated live via `send_enumerate_vars_request()` / `EnumerateVars` response from the WASM bridge
- **UI** — `PropertiesWindow::render_simvar_name_param()` renders an ImGui combo with filtering, tooltips, and manual-entry fallback

```cpp
// Editor startup (editor_app.cpp)
SimVarCatalog::instance().load_bundled("resources/simvar_catalog.json");

// Request LVar enumeration from WASM bridge
coordinator.send_enumerate_vars_request(VarType::LVar);

// WASM bridge responds — catalog updates automatically
// {"cmd":"EnumerateVars","var_type":"LVar","vars":[...]}
```

File: `src/simconnect/simvar_catalog.h/.cpp`

## WASM Bridge Module

The `an24_bridge.wasm` module runs inside MSFS 2024 and acts as the SimVars
data conduit. It receives V2 delta protocol requests on CommBus channels,
reads/writes MSFS Vars API, and sends delta/full-sync responses back.

### Module Files

| File | Purpose |
|------|---------|
| `wasm/an24_bridge.h` | Module header — identity constants, extern state |
| `wasm/an24_bridge.cpp` | Main module — entry points, Vars API dispatch, protocol handlers |
| `wasm/bridge_protocol.h` | WASM-side protocol helpers — shadow buffer, variable registration |
| `wasm/CMakeLists.txt` | WASM build target (disabled on macOS by default) |
| `wasm/panel.cfg.example` | Example panel.cfg for gauge deployment |
| `wasm/systems.cfg.example` | Example systems.cfg for system deployment |

### Module Lifecycle

MSFS 2024 resolves these callbacks by name at runtime:

| Function | Type | Called When |
|----------|------|-------------|
| `module_init()` | Module | Module loaded (CommBus channel registration) |
| `module_deinit()` | Module | Module unloaded (CommBus cleanup) |
| `an24_bridge_system_init()` | System | System instance created |
| `an24_bridge_system_update()` | System | Every frame (drives bridge protocol) |
| `an24_bridge_system_kill()` | System | System instance destroyed |
| `an24_bridge_gauge_init()` | Gauge | Gauge instance created (1x1 texture) |
| `an24_bridge_gauge_update()` | Gauge | Every frame (no-op) |
| `an24_bridge_gauge_draw()` | Gauge | Draw pass (no-op, non-visual) |
| `an24_bridge_gauge_kill()` | Gauge | Gauge instance destroyed |

### Variable Registration (Control Channel)

The host sends a JSON registration message on `An24Bridge_Control`:

```json
{"cmd":"register_names","vars":[
  {"name":"AIRSPEED INDICATED","type":"AVar","tier":0,"epsilon":0.5},
  {"name":"ELECTRICAL MAIN BUS VOLTAGE","type":"AVar","tier":0,"epsilon":0.1}
]}
```

The WASM module resolves each name to a Vars API ID via `fsVarsGetAVarId()` /
`fsVarsGetLVarId()` and stores it in the shadow buffer with tier/epsilon config.

### Vars API Dispatch (Frame Channel)

Variable reads are dispatched by `VarType`:

| VarType | Read function | Write function |
|---------|--------------|----------------|
| `AVar` | `fsVarsAVarGet(id)` | `fsVarsAVarSet(id, val)` |
| `LVar` | `fsVarsLVarGet(id)` | `fsVarsLVarSet(id, val)` |
| `BVar` | `fsVarsBVarGet(id)` | `fsVarsBVarSet(id, val)` |
| `EVar` | `fsVarsEnvironmentVarGet(id)` | _(read-only)_ |

### Build Requirements

- MSFS 2024 WASM SDK (provides `MSFS.h`, `MSFS_Vars.h`, `MSFS_CommBus.h`)
- `MSFS_WasmVersions.a` for MSFS 2024 WASM detection
- emscripten toolchain or Visual Studio 2022 with MSFS Platform Toolset
- Compile with: `-DENABLE_WASM_BRIDGE=ON -DMSFS_WASM_SDK_PATH=...`

### Deployment

The `.wasm` file can be placed in either the `wasm/` or `panel/` folder of the
SimObject package. The module supports both panel.cfg (gauge registration) and
systems.cfg (system registration). Using systems.cfg avoids VRAM allocation
since no texture is needed.
