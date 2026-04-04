# Phase 4: TypeRegistry

## Goal

Create a new injectable `bp2::TypeRegistry` that replaces the old `TypeRegistry` (function-local static, string-based, non-injectable). The new registry stores `bp2::Interface` objects keyed by `InternedId`, supports both C++ components and composite blueprints, and can be constructed in tests without touching the filesystem.

## Files To Create

```
src/blueprint_v2/registry/type_registry.h     <- TypeRegistry class, Entry struct
src/blueprint_v2/registry/type_registry.cpp   <- implementation
tests/blueprint_v2/test_registry.cpp          <- all tests for this phase
```

## Prerequisites

- Phase 2 complete (Interface, PortDescriptor, Direction)
- Phase 3 complete (Blueprint canonical type)
- `Domain` enum from `src/json_parser/json_parser.h` available

## Step-by-Step Instructions

### Step 4.1: CMake setup

1. Add `registry/type_registry.cpp` to `src/blueprint_v2/CMakeLists.txt`:

```cmake
add_library(blueprint_v2 STATIC
    path/path.cpp
    interface/interface.cpp
    blueprint/blueprint.cpp
    registry/type_registry.cpp
)
```

2. Add test target in `tests/CMakeLists.txt` after the `bp2_blueprint_tests` block:

```cmake
add_executable(bp2_registry_tests
    blueprint_v2/test_registry.cpp
)
target_include_directories(bp2_registry_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
)
target_link_libraries(bp2_registry_tests PRIVATE
    blueprint_v2
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(bp2_registry_tests)
```

3. Create placeholder files:
   - `src/blueprint_v2/registry/type_registry.h`: `#pragma once` + `namespace bp2 {}`
   - `src/blueprint_v2/registry/type_registry.cpp`: `#include "type_registry.h"`
   - `tests/blueprint_v2/test_registry.cpp`:
     ```cpp
     #include <gtest/gtest.h>
     #include "blueprint_v2/registry/type_registry.h"

     TEST(TypeRegistry, Placeholder) {
         EXPECT_TRUE(true);
     }
     ```

4. Build and run. Placeholder passes.

### Step 4.2: TypeRegistry::Entry struct

**Write test first** in `test_registry.cpp`:

```cpp
#include "ui/core/interned_id.h"
#include "blueprint_v2/interface/interface.h"

TEST(TypeRegistryEntry, ConstructCppComponent) {
    ui::StringInterner interner;
    bp2::TypeRegistry::Entry entry;
    entry.type_id = interner.intern("Battery");
    entry.iface = bp2::Interface({
        {interner.intern("v_in"), Domain::Electrical, bp2::Direction::Input},
        {interner.intern("v_out"), Domain::Electrical, bp2::Direction::Output},
    });
    entry.description = "DC battery source";
    entry.is_blueprint = false;
    EXPECT_EQ(interner.resolve(entry.type_id), "Battery");
    EXPECT_FALSE(entry.is_blueprint);
    EXPECT_EQ(entry.iface.size(), 2u);
}
```

Build. Confirm fail (no `TypeRegistry::Entry`).

**Write production code** in `type_registry.h`:

```cpp
#pragma once
#include "ui/core/interned_id.h"
#include "blueprint_v2/interface/interface.h"
#include <unordered_map>
#include <optional>
#include <string>
#include <functional>

namespace bp2 {

class TypeRegistry {
public:
    struct Entry {
        ui::InternedId type_id;
        Interface iface;
        std::string description;
        bool is_blueprint = false;   // false = C++ component, true = composite
        Blueprint const* blueprint = nullptr;  // non-null for blueprint types (reference-mode nested lookup)
    };

    TypeRegistry() = default;
};

} // namespace bp2
```

Build. Run. Pass.

### Step 4.3: register_component, has, find

**Write test first:**

```cpp
TEST(TypeRegistry, RegisterAndFind) {
    ui::StringInterner interner;
    bp2::TypeRegistry reg;

    auto bat_id = interner.intern("Battery");
    bp2::Interface iface({
        {interner.intern("v_in"), Domain::Electrical, bp2::Direction::Input},
        {interner.intern("v_out"), Domain::Electrical, bp2::Direction::Output},
    });
    reg.register_component(bat_id, iface, "DC battery");

    EXPECT_TRUE(reg.has(bat_id));
    auto* entry = reg.find(bat_id);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->iface.size(), 2u);
    EXPECT_FALSE(entry->is_blueprint);
}

TEST(TypeRegistry, FindReturnsNullForMissing) {
    bp2::TypeRegistry reg;
    ui::StringInterner interner;
    EXPECT_EQ(reg.find(interner.intern("Nonexistent")), nullptr);
    EXPECT_FALSE(reg.has(interner.intern("Nonexistent")));
}
```

Build. Confirm fail (no `register_component`, `has`, `find`).

**Write production code.** Add to `TypeRegistry` in `type_registry.h`:

```cpp
    void register_component(ui::InternedId type_id, Interface iface,
                            std::string description = "");

    Entry const* find(ui::InternedId type_id) const;
    bool has(ui::InternedId type_id) const;

private:
    std::unordered_map<ui::InternedId, Entry> entries_;
```

Implement in `type_registry.cpp`:

```cpp
#include "type_registry.h"

namespace bp2 {

void TypeRegistry::register_component(
    ui::InternedId type_id, Interface iface, std::string description) {
    Entry entry;
    entry.type_id = type_id;
    entry.iface = std::move(iface);
    entry.description = std::move(description);
    entry.is_blueprint = false;
    entries_[type_id] = std::move(entry);
}

TypeRegistry::Entry const* TypeRegistry::find(ui::InternedId type_id) const {
    auto it = entries_.find(type_id);
    if (it == entries_.end()) return nullptr;
    return &it->second;
}

bool TypeRegistry::has(ui::InternedId type_id) const {
    return entries_.count(type_id) > 0;
}

} // namespace bp2
```

Build. Run. Pass.

### Step 4.4: register_blueprint

**Write test first:**

```cpp
TEST(TypeRegistry, RegisterBlueprint) {
    ui::StringInterner interner;
    bp2::TypeRegistry reg;

    auto ps_id = interner.intern("power_system");
    bp2::Interface iface({
        {interner.intern("main_power"), Domain::Electrical, bp2::Direction::Input},
        {interner.intern("bus_28v"), Domain::Electrical, bp2::Direction::Output},
    });
    reg.register_blueprint(ps_id, iface, "Power distribution");

    auto* entry = reg.find(ps_id);
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->is_blueprint);
    EXPECT_EQ(entry->iface.size(), 2u);
}
```

Build. Confirm fail.

**Write production code.** Add to `TypeRegistry`:

```cpp
    void register_blueprint(ui::InternedId type_id, Interface iface,
                            std::string description = "",
                            Blueprint const* bp = nullptr);
```

Implement in `type_registry.cpp`:

```cpp
void TypeRegistry::register_blueprint(
    ui::InternedId type_id, Interface iface, std::string description,
    Blueprint const* bp) {
    Entry entry;
    entry.type_id = type_id;
    entry.iface = std::move(iface);
    entry.description = std::move(description);
    entry.is_blueprint = true;
    entry.blueprint = bp;
    entries_[type_id] = std::move(entry);
}
```

Build. Run. Pass.

### Step 4.5: interface_of (convenience accessor)

**Write test first:**

```cpp
TEST(TypeRegistry, InterfaceOfReturnsInterface) {
    ui::StringInterner interner;
    bp2::TypeRegistry reg;
    auto id = interner.intern("Resistor");
    bp2::Interface iface({
        {interner.intern("in"), Domain::Electrical, bp2::Direction::Input},
        {interner.intern("out"), Domain::Electrical, bp2::Direction::Output},
    });
    reg.register_component(id, iface);

    bp2::Interface const& result = reg.interface_of(id);
    EXPECT_EQ(result.size(), 2u);
}

TEST(TypeRegistry, InterfaceOfThrowsForMissing) {
    bp2::TypeRegistry reg;
    ui::StringInterner interner;
    auto id = interner.intern("Nope");
    EXPECT_THROW(reg.interface_of(id), std::runtime_error);
}
```

Build. Confirm fail.

**Write production code.** Add to `TypeRegistry`:

```cpp
    Interface const& interface_of(ui::InternedId type_id) const;
```

Implement:

```cpp
Interface const& TypeRegistry::interface_of(ui::InternedId type_id) const {
    auto* entry = find(type_id);
    if (!entry) {
        throw std::runtime_error("TypeRegistry: unknown type");
    }
    return entry->iface;
}
```

Add `#include <stdexcept>` to `type_registry.cpp`.

Build. Run. Pass.

### Step 4.6: on_missing callback

**Write test first:**

```cpp
TEST(TypeRegistry, OnMissingCallbackInvoked) {
    ui::StringInterner interner;
    bp2::TypeRegistry reg;
    bool called = false;
    ui::InternedId missing_id;

    reg.set_on_missing([&](ui::InternedId id) {
        called = true;
        missing_id = id;
        // Simulate lazy loading: register the type
        reg.register_component(id, bp2::Interface({}), "lazy-loaded");
    });

    auto id = interner.intern("LazyComponent");
    auto* entry = reg.find(id);
    // First call: not found, triggers callback
    // Note: find() does NOT auto-invoke on_missing. Use find_or_load().
    EXPECT_EQ(entry, nullptr);
    EXPECT_FALSE(called);

    // find_or_load DOES invoke callback
    entry = reg.find_or_load(id);
    EXPECT_TRUE(called);
    ASSERT_NE(entry, nullptr);
}

TEST(TypeRegistry, OnMissingNotCalledWhenPresent) {
    ui::StringInterner interner;
    bp2::TypeRegistry reg;
    bool called = false;
    reg.set_on_missing([&](ui::InternedId) { called = true; });

    auto id = interner.intern("X");
    reg.register_component(id, bp2::Interface({}));
    reg.find_or_load(id);
    EXPECT_FALSE(called);
}
```

Build. Confirm fail.

**Write production code.** Add to `TypeRegistry`:

```cpp
    void set_on_missing(std::function<void(ui::InternedId)> callback);
    Entry const* find_or_load(ui::InternedId type_id);

private:
    std::function<void(ui::InternedId)> on_missing_;
```

Implement:

```cpp
void TypeRegistry::set_on_missing(std::function<void(ui::InternedId)> callback) {
    on_missing_ = std::move(callback);
}

TypeRegistry::Entry const* TypeRegistry::find_or_load(ui::InternedId type_id) {
    auto* entry = find(type_id);
    if (entry) return entry;

    if (on_missing_) {
        on_missing_(type_id);
        return find(type_id);
    }
    return nullptr;
}
```

Build. Run. Pass.

### Step 4.7: size, iteration

**Write test first:**

```cpp
TEST(TypeRegistry, SizeAndIteration) {
    ui::StringInterner interner;
    bp2::TypeRegistry reg;
    EXPECT_EQ(reg.size(), 0u);

    reg.register_component(interner.intern("A"), bp2::Interface({}));
    reg.register_component(interner.intern("B"), bp2::Interface({}));
    reg.register_blueprint(interner.intern("C"), bp2::Interface({}));
    EXPECT_EQ(reg.size(), 3u);

    size_t count = 0;
    for (auto const& [id, entry] : reg) {
        (void)id;
        (void)entry;
        ++count;
    }
    EXPECT_EQ(count, 3u);
}
```

Build. Confirm fail (no `size()`, no `begin()`/`end()`).

**Write production code.** Add to `TypeRegistry`:

```cpp
    size_t size() const { return entries_.size(); }

    auto begin() const { return entries_.begin(); }
    auto end() const { return entries_.end(); }
```

Build. Run. Pass.

### Step 4.8: create_test_registry factory

**Write test first:**

```cpp
TEST(TypeRegistry, TestFactoryHasBasicTypes) {
    ui::StringInterner interner;
    auto reg = bp2::TypeRegistry::create_test_registry(interner);
    EXPECT_TRUE(reg.has(interner.intern("Battery")));
    EXPECT_TRUE(reg.has(interner.intern("Resistor")));
    EXPECT_TRUE(reg.has(interner.intern("Ground")));
    EXPECT_GE(reg.size(), 3u);
}
```

Build. Confirm fail.

**Write production code.** Add static factory to `TypeRegistry`:

```cpp
    static TypeRegistry create_test_registry(ui::StringInterner& interner);
```

Implement in `type_registry.cpp`:

```cpp
TypeRegistry TypeRegistry::create_test_registry(ui::StringInterner& interner) {
    TypeRegistry reg;

    reg.register_component(
        interner.intern("Battery"),
        Interface({
            {interner.intern("v_in"), Domain::Electrical, Direction::Input},
            {interner.intern("v_out"), Domain::Electrical, Direction::Output},
        }),
        "DC battery source"
    );

    reg.register_component(
        interner.intern("Resistor"),
        Interface({
            {interner.intern("in"), Domain::Electrical, Direction::Input},
            {interner.intern("out"), Domain::Electrical, Direction::Output},
        }),
        "Resistor"
    );

    reg.register_component(
        interner.intern("Ground"),
        Interface({
            {interner.intern("gnd"), Domain::Electrical, Direction::InOut},
        }),
        "Ground reference"
    );

    reg.register_component(
        interner.intern("LED"),
        Interface({
            {interner.intern("v_in"), Domain::Electrical, Direction::Input},
            {interner.intern("ground"), Domain::Electrical, Direction::InOut},
        }),
        "Indicator light"
    );

    return reg;
}
```

Build. Run. Pass.

## Final Verification

```bash
cmake --build build -j$(sysctl -n hw.ncpu)
cd build && ctest --output-on-failure
```

## Files Created This Phase

```
src/blueprint_v2/registry/type_registry.h
src/blueprint_v2/registry/type_registry.cpp
tests/blueprint_v2/test_registry.cpp
```

## Lines Modified

- `src/blueprint_v2/CMakeLists.txt`: add `registry/type_registry.cpp`
- `tests/CMakeLists.txt`: add `bp2_registry_tests` target
