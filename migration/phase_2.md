# Phase 2: PortDescriptor, Direction, and Interface

## Goal

Create the port/interface system. A `PortDescriptor` is a typed connection point (name + domain + direction). An `Interface` is an ordered collection of ports with O(1) lookup by name. These types are used by Blueprint nodes, nested instances, and the flattener.

## Files To Create

```
src/blueprint_v2/interface/port_descriptor.h   <- PortDescriptor, Direction enum
src/blueprint_v2/interface/interface.h         <- Interface class
src/blueprint_v2/interface/interface.cpp        <- Interface implementation
tests/blueprint_v2/test_interface.cpp          <- all tests for this phase
```

## Prerequisites

- Phase 1 completed (path.h compiles, tests pass)
- `ui::InternedId` available
- `Domain` enum from `src/json_parser/json_parser.h` available

## Step-by-Step Instructions

### Step 2.1: Add test target to CMake

1. Open `tests/CMakeLists.txt`. After the `bp2_path_tests` block, add:

```cmake
add_executable(bp2_interface_tests
    blueprint_v2/test_interface.cpp
    ${CMAKE_SOURCE_DIR}/src/blueprint_v2/interface/interface.cpp
)
target_include_directories(bp2_interface_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
)
target_link_libraries(bp2_interface_tests PRIVATE
    blueprint_v2
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(bp2_interface_tests)
```

2. Create file `tests/blueprint_v2/test_interface.cpp` with:

```cpp
#include <gtest/gtest.h>
#include "blueprint_v2/interface/port_descriptor.h"
#include "blueprint_v2/interface/interface.h"

TEST(Interface, Placeholder) {
    EXPECT_TRUE(true);
}
```

3. Create empty files:
   - `src/blueprint_v2/interface/port_descriptor.h` (just `#pragma once` and `namespace bp2 {}`)
   - `src/blueprint_v2/interface/interface.h` (just `#pragma once` and `namespace bp2 {}`)
   - `src/blueprint_v2/interface/interface.cpp` (just `#include "interface.h"`)

4. Add `interface/interface.cpp` to the `blueprint_v2` library sources in `src/blueprint_v2/CMakeLists.txt`:

```cmake
add_library(blueprint_v2 STATIC
    path/path.cpp
    interface/interface.cpp
)
```

5. Build and run. Placeholder passes. All existing tests pass.

### Step 2.2: Direction enum

**Write test first** in `test_interface.cpp`:

```cpp
TEST(Direction, HasThreeValues) {
    EXPECT_NE(bp2::Direction::Input, bp2::Direction::Output);
    EXPECT_NE(bp2::Direction::Input, bp2::Direction::InOut);
    EXPECT_NE(bp2::Direction::Output, bp2::Direction::InOut);
}
```

Build. Confirm fail (no `bp2::Direction`).

**Write production code** in `port_descriptor.h`:

```cpp
#pragma once
#include "ui/core/interned_id.h"
#include "json_parser.h"  // for Domain enum
#include <cstdint>

namespace bp2 {

enum class Direction : uint8_t {
    Input,
    Output,
    InOut
};

} // namespace bp2
```

Build. Run. Pass.

### Step 2.3: PortDescriptor struct

**Write test first:**

```cpp
TEST(PortDescriptor, ConstructAndAccess) {
    ui::StringInterner interner;
    auto name = interner.intern("v_out");
    bp2::PortDescriptor pd{name, Domain::Electrical, bp2::Direction::Output};
    EXPECT_EQ(pd.name, name);
    EXPECT_EQ(pd.domain, Domain::Electrical);
    EXPECT_EQ(pd.direction, bp2::Direction::Output);
}

TEST(PortDescriptor, EqualityByName) {
    ui::StringInterner interner;
    auto n1 = interner.intern("v_out");
    auto n2 = interner.intern("v_out");
    bp2::PortDescriptor a{n1, Domain::Electrical, bp2::Direction::Output};
    bp2::PortDescriptor b{n2, Domain::Electrical, bp2::Direction::Output};
    EXPECT_EQ(a, b);
}

TEST(PortDescriptor, InequalityByDomain) {
    ui::StringInterner interner;
    auto n = interner.intern("x");
    bp2::PortDescriptor a{n, Domain::Electrical, bp2::Direction::Output};
    bp2::PortDescriptor b{n, Domain::Logical, bp2::Direction::Output};
    EXPECT_NE(a, b);
}
```

Build. Confirm fail.

**Write production code** in `port_descriptor.h`, inside `namespace bp2`:

```cpp
struct PortDescriptor {
    ui::InternedId name;
    Domain domain;
    Direction direction;

    bool operator==(PortDescriptor const& o) const {
        return name == o.name && domain == o.domain && direction == o.direction;
    }
    bool operator!=(PortDescriptor const& o) const { return !(*this == o); }
};
```

Build. Run. Pass.

### Step 2.4: Interface -- construct from vector, size, iterate

**Write test first:**

```cpp
TEST(Interface, EmptyByDefault) {
    bp2::Interface iface;
    EXPECT_EQ(iface.size(), 0u);
    EXPECT_EQ(iface.begin(), iface.end());
}

TEST(Interface, ConstructFromVector) {
    ui::StringInterner interner;
    std::vector<bp2::PortDescriptor> ports = {
        {interner.intern("a"), Domain::Electrical, bp2::Direction::Input},
        {interner.intern("b"), Domain::Electrical, bp2::Direction::Output},
    };
    bp2::Interface iface(ports);
    EXPECT_EQ(iface.size(), 2u);
}

TEST(Interface, IterationOrder) {
    ui::StringInterner interner;
    auto a = interner.intern("a");
    auto b = interner.intern("b");
    std::vector<bp2::PortDescriptor> ports = {
        {a, Domain::Electrical, bp2::Direction::Input},
        {b, Domain::Electrical, bp2::Direction::Output},
    };
    bp2::Interface iface(ports);
    auto it = iface.begin();
    EXPECT_EQ(it->name, a);
    ++it;
    EXPECT_EQ(it->name, b);
}
```

Build. Confirm fail.

**Write production code** in `interface.h`:

```cpp
#pragma once
#include "port_descriptor.h"
#include <vector>
#include <unordered_map>
#include <optional>

namespace bp2 {

class Interface {
public:
    Interface() = default;
    explicit Interface(std::vector<PortDescriptor> ports);

    size_t size() const { return ports_.size(); }
    bool empty() const { return ports_.empty(); }

    auto begin() const { return ports_.begin(); }
    auto end() const { return ports_.end(); }

    std::vector<PortDescriptor> const& ports() const { return ports_; }

private:
    std::vector<PortDescriptor> ports_;
    std::unordered_map<ui::InternedId, size_t> name_to_idx_;
};

} // namespace bp2
```

**Write implementation** in `interface.cpp`:

```cpp
#include "interface.h"

namespace bp2 {

Interface::Interface(std::vector<PortDescriptor> ports)
    : ports_(std::move(ports)) {
    name_to_idx_.reserve(ports_.size());
    for (size_t i = 0; i < ports_.size(); ++i) {
        name_to_idx_[ports_[i].name] = i;
    }
}

} // namespace bp2
```

Build. Run. Pass.

### Step 2.5: Interface -- find, has, at

**Write test first:**

```cpp
TEST(Interface, FindByName) {
    ui::StringInterner interner;
    auto a = interner.intern("a");
    auto b = interner.intern("b");
    auto c = interner.intern("c");
    std::vector<bp2::PortDescriptor> ports = {
        {a, Domain::Electrical, bp2::Direction::Input},
        {b, Domain::Logical, bp2::Direction::Output},
    };
    bp2::Interface iface(ports);

    auto found = iface.find(b);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->domain, Domain::Logical);

    EXPECT_FALSE(iface.find(c).has_value());
}

TEST(Interface, HasByName) {
    ui::StringInterner interner;
    auto a = interner.intern("a");
    auto z = interner.intern("z");
    bp2::Interface iface({{a, Domain::Electrical, bp2::Direction::Input}});
    EXPECT_TRUE(iface.has(a));
    EXPECT_FALSE(iface.has(z));
}

TEST(Interface, AtByName) {
    ui::StringInterner interner;
    auto a = interner.intern("a");
    bp2::Interface iface({{a, Domain::Electrical, bp2::Direction::Input}});
    EXPECT_EQ(iface.at(a).direction, bp2::Direction::Input);
}
```

Build. Confirm fail.

**Write production code.** Add to `Interface` class in `interface.h`:

```cpp
    std::optional<PortDescriptor> find(ui::InternedId name) const;
    bool has(ui::InternedId name) const;
    PortDescriptor const& at(ui::InternedId name) const;
```

Add to `interface.cpp`:

```cpp
std::optional<PortDescriptor> Interface::find(ui::InternedId name) const {
    auto it = name_to_idx_.find(name);
    if (it == name_to_idx_.end()) return std::nullopt;
    return ports_[it->second];
}

bool Interface::has(ui::InternedId name) const {
    return name_to_idx_.count(name) > 0;
}

PortDescriptor const& Interface::at(ui::InternedId name) const {
    auto it = name_to_idx_.find(name);
    return ports_[it->second];
}
```

Build. Run. Pass.

### Step 2.6: Interface -- equality

**Write test first:**

```cpp
TEST(Interface, EqualInterfaces) {
    ui::StringInterner interner;
    auto a = interner.intern("a");
    std::vector<bp2::PortDescriptor> ports = {
        {a, Domain::Electrical, bp2::Direction::Input},
    };
    bp2::Interface i1(ports);
    bp2::Interface i2(ports);
    EXPECT_EQ(i1, i2);
}

TEST(Interface, UnequalInterfaces) {
    ui::StringInterner interner;
    auto a = interner.intern("a");
    auto b = interner.intern("b");
    bp2::Interface i1({{a, Domain::Electrical, bp2::Direction::Input}});
    bp2::Interface i2({{b, Domain::Electrical, bp2::Direction::Input}});
    EXPECT_NE(i1, i2);
}
```

Build. Confirm fail.

**Write production code.** Add to `Interface` in `interface.h`:

```cpp
    bool operator==(Interface const& o) const { return ports_ == o.ports_; }
    bool operator!=(Interface const& o) const { return !(*this == o); }
```

Build. Run. Pass.

## Final Verification

```bash
cmake --build build -j$(sysctl -n hw.ncpu)
cd build && ctest --output-on-failure
```

All old tests pass. All new `bp2_interface_tests` pass.

## Files Created This Phase

```
src/blueprint_v2/interface/port_descriptor.h
src/blueprint_v2/interface/interface.h
src/blueprint_v2/interface/interface.cpp
tests/blueprint_v2/test_interface.cpp
```

## Lines Modified in Existing Files

- `src/blueprint_v2/CMakeLists.txt`: add `interface/interface.cpp` to sources
- `tests/CMakeLists.txt`: add `bp2_interface_tests` target (~12 lines)
