# Testing

## Test Framework

Uses Google Test (gtest). Tests are organized by feature area in `tests/`.

> **Note:** Test file names use `test_*.cpp` pattern. Many blueprint v2 tests live in `tests/blueprint_v2/`.

## Running Tests

```bash
# Run all tests
cd build && ctest

# Run specific test executable
./build/tests/editor_data_tests

# Run with regex filter
ctest -R "editor_data" --output-on-failure

# Run specific test case
./build/tests/editor_data_tests --gtest_filter="DataTest.TestName"
```

## Current Test Pattern

```cpp
#include <gtest/gtest.h>
#include "core/solvers/jit/components/all.h"
#include "core/solvers/common/port_registry.h"

// Helper factory
static AND<JitProvider> make_and() {
    AND<JitProvider> comp;
    comp.provider.set(PortNames::A, 0);
    comp.provider.set(PortNames::B, 1);
    comp.provider.set(PortNames::o, 2);
    return comp;
}

static SimulationState make_state() {
    SimulationState st;
    st.allocate_signal(0.0f);  // A
    st.allocate_signal(0.0f);  // B
    st.allocate_signal(0.0f);  // out
    return st;
}

TEST(ANDTest, BothTrue_ReturnsTrue) {
    auto comp = make_and();
    auto st = make_state();

    st.values[0] = 1.0f;  // A = TRUE
    st.values[1] = 1.0f;  // B = TRUE

    comp.execute(st, 1.0/60.0);

    EXPECT_NEAR(st.values[2], 1.0f, 0.001f);
}
```

## Test Inventory by Category

### Component Tests
Test individual component behavior:
- `test_logic_gates.cpp` — AND, OR, NOT, XOR, NAND
- `test_knob_switch.cpp` — KnobSwitch
- `test_pid.cpp` — PID controller
- `test_pd.cpp` — PD controller
- `test_pi_p.cpp` — PI controller
- `test_lut.cpp` — Lookup table
- `test_slew_rate.cpp` — SlewRate
- `test_asym_slew_rate.cpp` — AsymSlewRate
- `test_clamp_normalize.cpp` — Clamp, Normalize
- `test_time_delay.cpp` — TimeDelay
- `test_sample_hold.cpp` — SampleHold
- `test_monostable.cpp` — Monostable
- `test_spring.cpp` — Spring
- `test_lua_script.cpp` — LuaScript
- `test_lua_script_validate.cpp` — LuaScript validation

### Electrical / Solver Tests
- `test_electrical_primitives.cpp` — Electrical primitive components
- `test_electrical_island_build.cpp` — Island construction
- `test_push_scheduler.cpp` — PushScheduler
- `test_push_state.cpp` — Push state machine
- `test_push_build_validation.cpp` — Build validation
- `test_push_runtime_regression.cpp` — Runtime regression
- `test_logical_solver.cpp` — Logical domain solver
- `test_production_path_push_runtime.cpp` — Production path runtime
- `test_production_path_port_map.cpp` — Port mapping
- `test_production_path_parity.cpp` — Parity checks
- `test_cross_path_signal_equivalence.cpp` — Signal equivalence
- `test_port_queries_self_contained.cpp` — Port queries
- `test_port_map_regression.cpp` — Port map regression
- `test_unmapped_port_regression.cpp` — Unmapped port regression
- `test_pneumatic_integration.cpp` — Pneumatic integration

### Blueprint V2 Tests
Located in `tests/blueprint_v2/`:
- `test_blueprint.cpp` — Blueprint class
- `test_codec.cpp` — BlueprintCodec encode/decode
- `test_editor_model.cpp` — EditorModel
- `test_flattener.cpp` — Flattener
- `test_bake.cpp` — Bake/unbake
- `test_interface.cpp` — Interface, PortDescriptor
- `test_path.cpp` — Path parsing
- `test_library_index.cpp` — LibraryIndex
- `test_type_def_to_blueprint.cpp` — Type definition conversion
- `test_validation.cpp` — Validation suite
- `test_persist_validation.cpp` — Persistence validation
- `test_document_add_component_safety.cpp` — Component add safety
- `test_document_workspace_session.cpp` — Workspace session
- `test_workspace_blueprint_separation.cpp` — Blueprint/session separation
- `test_workspace_session.cpp` — Session persistence
- `test_embedded_subwindow_scene.cpp` — Embedded subwindow
- `test_embedded_editing_undo.cpp` — Embedded editing undo
- `test_subwindow_open_target.cpp` — Subwindow open target
- `test_issue_23_nested_inline_only.cpp` — Issue #23 regression
- `test_export_flattener_parity.cpp` — Export flattener parity
- `test_codegen_export_parity.cpp` — Codegen export parity

### Editor Tests
- `test_commands.cpp` — Command execution
- `test_canvas_input.cpp` — Canvas input FSM
- `test_scene_mutations.cpp` — Scene mutations
- `test_semantic_interaction.cpp` — Semantic interaction
- `test_semantic_interaction_session.cpp` — Interaction sessions
- `test_semantic_scene_snapshot.cpp` — Scene snapshots
- `test_semantic_scene_hittest.cpp` — Hit testing
- `test_semantic_input_machine.cpp` — Input state machine
- `test_semantic_input_reducer.cpp` — Input reducer
- `test_semantic_canvas_controller.cpp` — Canvas controller
- `test_semantic_canvas_host.cpp` — Canvas host
- `test_canvas_scene_snapshot.cpp` — Canvas scene snapshot
- `test_node_slot_layout.cpp` — Node slot layout
- `test_node_badge.cpp` — Node badges
- `test_window_invariants.cpp` — Window invariants
- `test_editor_ownership_isolation.cpp` — Ownership isolation
- `test_properties_window.cpp` — Properties window
- `test_script_editor_window.cpp` — Script editor window
- `test_oscilloscope_model.cpp` — Oscilloscope model
- `test_auto_layout.cpp` — Auto layout
- `test_presentation_compiler.cpp` — Presentation compiler
- `test_visual_primitives.cpp` — Visual primitives
- `test_visual_container.cpp` — Visual containers
- `test_visual_wire.cpp` — Wire rendering
- `test_ui_grid.cpp` — UI grid
- `test_ui_layout.cpp` — UI layout
- `test_ui_render_context.cpp` — Render context
- `test_ui_interning.cpp` — String interning

### SimConnect / Provider Tests
- `test_simconnect_bridge.cpp` — SimConnect bridge
- `test_simconnect_stub.cpp` — SimConnect stub
- `test_mock_provider.cpp` — Mock provider
- `test_simvar_backend.cpp` — SimVar backend
- `test_wire_protocol.cpp` — Wire protocol

### JIT / AOT Tests
- `test_jit_aot_bridge_equivalence.cpp` — JIT/AOT equivalence
- `test_aot_composite_tests.cpp` — AOT composites
- `test_lut_codegen.cpp` — LUT codegen
- `test_codegen_sanitize.cpp` — Codegen sanitization
- `test_factory_validation_tests.cpp` — Factory validation

### Regression Tests
- `test_architecture_regression.cpp` — Architecture regression
- `test_push_runtime_regression.cpp` — Push runtime regression
- `test_unmapped_port_regression.cpp` — Unmapped port regression
- `test_port_map_regression.cpp` — Port map regression
- `test_issue_91_blueprint_instance_iface_authority.cpp` — Issue #91

### Migration / Compatibility
- `test_v3_migration.cpp` — V3 format migration
- `test_bp2_codec.cpp` — BP2 codec
- `test_bp2_bake.cpp` — BP2 bake
- `test_bp2_flattener.cpp` — BP2 flattener
- `test_bp2_interface.cpp` — BP2 interface
- `test_bp2_path.cpp` — BP2 path

## Test Helpers

Common helpers are in:
- `tests/semantic_test_helpers.h` — Semantic test utilities
- `tests/jit_build_input_test_helper.h` — JIT build input helpers

## Files

| Category | Files |
|----------|-------|
| Components | `test_logic_gates.cpp`, `test_pid.cpp`, `test_pd.cpp`, `test_lut.cpp`, `test_slew_rate.cpp`, `test_clamp_normalize.cpp`, `test_lua_script.cpp`, `test_knob_switch.cpp` |
| Electrical/Solver | `test_electrical_primitives.cpp`, `test_electrical_island_build.cpp`, `test_push_scheduler.cpp`, `test_push_runtime_regression.cpp`, `test_pneumatic_integration.cpp` |
| Blueprint V2 | `tests/blueprint_v2/test_*.cpp` (21 files) |
| Editor | `test_commands.cpp`, `test_canvas_input.cpp`, `test_scene_mutations.cpp`, `test_semantic_*.cpp`, `test_auto_layout.cpp` |
| SimConnect | `test_simconnect_bridge.cpp`, `test_mock_provider.cpp`, `test_wire_protocol.cpp` |
| JIT/AOT | `test_jit_aot_bridge_equivalence.cpp`, `test_aot_composite_tests.cpp`, `test_lut_codegen.cpp` |
