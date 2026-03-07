# Task: Re-enable All Editor Tests

## Context
After ComponentVariant integration and multi-domain scheduling implementation, many editor tests were disabled with comment "Disabled for AOT-only refactor - Systems removed". Now that `SimulationController` works with `ComponentVariant` and domain vectors instead of `Systems`, we need to re-enable all tests.

## Current State
- **Active tests**: 118 (114 passing, 4 failing = 97% pass rate)
- **Target**: 330+ tests (original count before AOT refactor)

## Disabled Tests in /Users/vladimir/an24_cpp/tests/CMakeLists.txt

Find all test targets commented out with:
```cmake
# add_executable(TEST_NAME
#     test_file.cpp
#     source_files.cpp
# )
```

## Tests to Re-enable

### Priority 1: Core Editor Tests (Critical)
1. **jit_solver_tests** - Basic JIT solver functionality
2. **editor_data_tests** - Editor data structures (TDD Step 1)
3. **editor_render_tests** - Rendering subsystem
4. **editor_hittest_tests** - Hit testing (TDD Step 6)
5. **editor_viewport_tests** - Viewport management (TDD Step 3)

### Priority 2: Interaction Tests
6. **editor_interaction_tests** - User interactions (TDD Step 4)
7. **editor_event_tests** - Event handling (TDD Step 7)
8. **editor_widget_tests** - Widget & layout tests

### Priority 3: Routing Tests
9. **editor_routing_tests** - Wire routing (TDD)
10. **editor_router_tests** - A* router tests

### Priority 4: GL Tests
11. **editor_gl_setup_tests** - OpenGL setup (TDD Step 9)

## Instructions

For each disabled test:

1. **Uncomment the test target** (remove `#` from all lines)

2. **Update include paths** - Change:
   ```cmake
   ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
   ```
   To:
   ```cmake
   ${CMAKE_BINARY_DIR}/_deps/json-src/include
   ```

3. **Add missing dependencies** if compilation fails:
   - Add `${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp` for Blueprint usage
   - Add other required .cpp files as needed

4. **Update comments** - Remove "Disabled for AOT-only refactor" comment

5. **Build and test**:
   ```bash
   cmake ..
   make TEST_NAME
   ./tests/TEST_NAME --gtest_brief=1
   ```

6. **Track results** - Note which tests:
   - Compile successfully
   - Pass all tests
   - Have failures (what failed?)
   - Need code fixes

## Expected Issues

### Issue 1: Missing `Systems` references
**Solution**: Most code already uses `SimulationController` instead. If tests still reference `Systems`, update to `SimulationController`.

### Issue 2: Missing source files
**Solution**: Add required .cpp files to target sources.

### Issue 3: Outdated test expectations
**Solution**: Update test expectations to match new multi-domain behavior.

## Example Conversion

**Before:**
```cmake
# Editor persist tests (TDD Step 2)
# add_executable(editor_persist_tests
#     test_persist.cpp
#     ${CMAKE_SOURCE_DIR}/src/editor/persist.cpp
#     ${CMAKE_SOURCE_DIR}/src/editor/visual_node.cpp
# )
# target_include_directories(editor_persist_tests PRIVATE
#     ${CMAKE_SOURCE_DIR}/src
#     ${CMAKE_SOURCE_DIR}/src/editor
#     ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
# )
# gtest_discover_tests(editor_persist_tests)
```

**After:**
```cmake
# Editor persist tests (TDD Step 2)
add_executable(editor_persist_tests
    test_persist.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/persist.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual_node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/render.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
)
target_compile_definitions(editor_persist_tests PRIVATE EDITOR_TESTING)
target_include_directories(editor_persist_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(editor_persist_tests PRIVATE
    json_parser
    jit_solver
    GTest::gtest_main
)
gtest_discover_tests(editor_persist_tests)
```

## Success Criteria

✅ All 330+ tests re-enabled
✅ Compile succeeds for all test targets
✅ Test count >300
✅ Pass rate >90%

## Validation

After re-enabling all tests:
```bash
cd /Users/vladimir/an24_cpp/build
cmake ..
make -j4
ctest --output-on-failure 2>&1 | grep "tests passed"
```

Expected output:
```
XX% tests passed, YY tests failed out of 330+
```
