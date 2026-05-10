# Stack Allocation in Hot Paths

The simulation runs at 60 Hz with a multi-stage pipeline. Every frame triggers execute/commit on all active components plus domain subsolvers. Dynamic heap allocation (`new`, `std::vector` growth) in these hot paths causes GC-style pauses and cache misses. This document covers techniques to keep data on the stack in hot paths.

## Rules of Thumb

| Situation | Technique |
|-----------|-----------|
| Fixed-size array needed | `std::array<T, N>` instead of `std::vector` |
| Bounded dynamic array (N ≤ 1024 typical) | `llvm::SmallVector<T, N>` (LLVM) or manual `alloca` with guard |
| Container needed with custom backing | `std::pmr::monotonic_buffer_resource` with stack buffer |
| Cross-frame lifetime needed | Pre-allocate in `BuildResult`, reuse per frame |
| Size unknown but has reasonable upper bound | Inline small-buffer fallback in class |

**Priority**: stack allocation in hot paths is **required**; heap allocation in the step loop is a correctness issue, not just a performance issue.

## Core Techniques

### 1. `std::array<T, N>` over `std::vector`

`std::vector` always heap-allocates (even for small sizes). If the size is known at compile time, `std::array` lives entirely on the stack:

```cpp
// Bad: heap allocation on every call
void compute_residuals(const std::vector<float>& v) { ... }

// Good: stack-only, no indirection
void compute_residuals(const std::array<float, 64>& v) { ... }
```

### 2. `llvm::SmallVector<T, N>` (LLVM)

For cases where the element count may occasionally exceed a small threshold, `llvm::SmallVector<T, N>` stores the first N elements inline on the stack and only heap-allocates when the capacity exceeds N:

```cpp
#include <llvm/ADT/SmallVector.h>

SmallVector<float, 16> temps; // Up to 16 floats on stack
temps.push_back(1.0f);          // No heap until push #17
```

In this project: see `src/core/solvers/common/` — this pattern is already used in nodal build plans where island counts are bounded but variable.

### 3. `alloca` for temporary buffers (with guard)

For one-shot temporary arrays of unknown but bounded size, `alloca` allocates from the current stack frame. The memory is automatically reclaimed on function return:

```cpp
void solve_nodal(double dt) {
    const size_t n = compute_temp_size();
    assert(n < 8192 && "stack buffer overflow protection");
    auto* buf = static_cast<float*>(alloca(n * sizeof(float)));
    // use buf...
} // buf freed automatically
```

**Never use `alloca` without an explicit upper bound check.** Violation results in silent stack overflow.

### 4. `[[no_unique_address]]` for zero-size members

C++20's `[[no_unique_address]]` on empty base classes prevents them from consuming space in the containing struct. Useful for provider objects in components:

```cpp
struct MyComponent {
    JitProvider provider;              // ~0 bytes if provider is empty in the common case
    std::array<float, 32> local_state;  // stays compact on the stack
};
```

This keeps the component object small, improving stack density in `PushScheduler`'s `std::vector<ScheduledComponent>`.

### 5. Stack-backed `std::pmr` containers

For containers that need dynamic size but should prefer the stack:

```cpp
#include <memory_resource>

void execute(SimulationState& st, double /*dt*/) {
    alignas(8) char buffer[256];
    std::pmr::monotonic_buffer_resource res(buffer, sizeof(buffer));
    std::pmr::vector<float> local(&res); // Uses stack buffer first

    // Only heap-allocates if push_back exceeds 256 bytes
    local.push_back(compute());
}
```

`monotonic_buffer_resource` never frees — only grows. Perfect for per-frame scratch that doesn't need persistence.

### 6. Escape analysis hints

Clang performs escape analysis to determine whether heap-allocated objects can be promoted to stack. Help the compiler:

- **Restrict pointers**: `T* restrict p` tells the compiler no aliasing, enabling stack promotion
- **Avoid passing stack addresses to opaque functions** (functions not visible to the compiler in the same TU)
- **Minimize `std::function`** in hot paths — captures heap-allocate and prevent inlining

### 7. `__attribute__((always_inline))`

Forced inlining exposes local variables to the caller's stack frame, enabling escape analysis across function boundaries:

```cpp
__attribute__((always_inline)) inline void
apply_patch_electrical(const PatchOp* ops, size_t count, float* values) noexcept {
    for (size_t i = 0; i < count; ++i) {
        values[ops[i].signal] = ops[i].value;
    }
}
```

Without `always_inline`, the compiler may allocate `ops` and `values` on the heap due to conservative analysis of call sites.

## What Goes on the Stack vs Heap

### Stack (hot path — required)

- Temporary scratch arrays in `execute`/`commit` (≤ 4 KB recommended)
- Small local vectors with `SmallVector<T, N>`
- POD structs passed by value (≤ 32 bytes triggers register placement)
- `NodalRuntimeState` scratch vectors (heap, pre-reserved at build time, reused every frame via `resize()` — matrix A, rhs b, node arrays)

### Heap (pre-allocated, reused)

- `SimulationState::values[]` — always heap, pre-allocated at build time, reused every frame
- `BuildResult::devices` — heap, allocated at load, lives for session
- `PushScheduler::sources_` / `consumers_` vectors — heap, allocated at build, reused every frame
- Any component state that must persist across frames (battery charge, integrator accumulators)

### Never allocate in hot path

- Inside `execute()` / `commit()` — forbidden unless backed by pre-allocated stack buffer
- Inside `solve_nodal()` — forbidden; scratch must be either stack-local scalars (NodalRuntimeState locals point into pre-reserved heap vectors — no per-frame allocation, no issue) or explicitly stack-allocated with `alloca` + guard for bounded temporary buffers
- Inside any function called more than 60× per second — audit for allocations

## Verifying Stack Usage

Use Compiler Explorer (godbolt.org) or pass these flags to see what's happening:

```bash
# Report inlining decisions
clang++ -O2 -Rpass=inline -Rpass=loop-vectorize -S -o - source.cpp

# Check for heap allocation in hot path
clang++ -O2 -fsanitize=address -fno-omit-frame-pointer source.cpp  # run and watch for allocator calls
```

In the assembly output:
- `call malloc` / `operator new` → heap allocation (bad in hot path)
- `sub rsp, N` → stack allocation (expected for locals)
- `movaps` / `addss` with stack-based operands → data on stack

## Checklist for New Hot-Path Code

Before committing any code that runs inside `execute`/`commit` or inside `solve_nodal`:

1. Does this add a `std::vector` or `std::list` construction inside the step loop? → redesign
2. Is there a dynamic `new` / `make_unique` in the hot path? → move to build phase
3. Is the temporary buffer size bounded? → use `std::array` or `SmallVector<T, N>`
4. Is the buffer size unbounded? → use `alloca` with `assert` guard, or pre-allocate in `SimulationState`
5. Is a closure / `std::function` needed in the hot path? → redesign; prefer type-erased dispatch with `ErasedStep`
6. Can the compiler inline the function? → add `always_inline` if linkage allows
7. Is this scratch used per-island inside `solve_nodal`? → use `NodalRuntimeState` vectors pre-reserved at build time (already implemented — do not force-stack-allocate large matrices with `alloca`; pre-reserved heap with indexed access is equivalent in practice)

## UI Hot Paths

The editor UI runs on the same 60 Hz frame loop (via ImGui rendering) but on the main thread — frame hitches here cause visible stutter. The same heap-allocation discipline applies to the render hot path.

### Confirmed clean

- `compute_wire_crossings()` — uses `ui::SmallVector<Wire*, 64>` inline for wire collection (stack for typical blueprints)
- `Wire::crossings_` — `ui::SmallVector<WireCrossing, 4>` with SBO for 0–4 crossings per wire
- `Wire::render()` fast path — `static thread_local std::vector<Pt>` reused across calls (thread-local, stable capacity after warm-up)
- `NodeSpriteCache` — FBO-backed sprite baking; cache hit = single draw call, no per-frame allocation
- `Scene::render()` — range-for over `visual_roots()`, no allocations

### Fixed issue

`render_channel_plot()` in `src/editor/visual/oscilloscope_plot.cpp` allocated two `std::vector<float>` per probe per frame (300 × 4 bytes each = 1.2 KB heap + free per probe). Replaced with caller-owned `std::array<float, kVisibleSamples>` passed by reference:

- `OscilloscopeWindow` owns one `std::array` member, passed to `render_channel_plot()`
- `CanvasRenderer` owns one `std::array` member, passed to `render_channel_plot()`
- `render_channel_plot_empty()` uses `fill(0.0f)` then delegates — no separate `std::vector` in the empty-samples branch

### `ui::SmallVector<T, N>` in this project

`src/ui/core/small_vector.h` provides a custom SBO vector used correctly throughout the codebase (`WireCrossing`, `Wire*` collections in `compute_wire_crossings`). Do not replace it with `std::vector` — the SBO is intentional.