# SOR Solver Safety Improvements

## Current Implementation (state.cpp:48-54)
```cpp
void SimulationState::solve_signals_balance(float sor_omega) {
    for (size_t i = 0; i < across.size(); ++i) {
        if (!signal_types[i].is_fixed && inv_conductance[i] > 0.0f) {
            across[i] += through[i] * inv_conductance[i] * sor_omega;
        }
    }
}
```

## Proposed Safety Enhancements

### 1. Delta Clamping (Priority: HIGH)
Prevent excessive voltage jumps during relay switching or short circuits.

```cpp
void SimulationState::solve_signals_balance_safe(float sor_omega) {
    constexpr float MAX_DELTA = 5.0f;  // Max 5V change per iteration

    for (size_t i = 0; i < across.size(); ++i) {
        if (!signal_types[i].is_fixed && inv_conductance[i] > 0.0f) {
            float delta = through[i] * inv_conductance[i] * sor_omega;
            delta = std::clamp(delta, -MAX_DELTA, MAX_DELTA);  // Clamp delta
            across[i] += delta;
        }
    }
}
```

**Benefits:**
- Prevents voltage explosions during topology changes
- DMR400 relay switching becomes smooth
- Short circuits don't cause NaN cascades

### 2. NaN/Inf Detection (Priority: HIGH)
Detect and recover from solver explosions.

```cpp
void SimulationState::solve_signals_balance_with_safety(float sor_omega) {
    constexpr float MAX_DELTA = 5.0f;
    bool solver_exploded = false;

    for (size_t i = 0; i < across.size(); ++i) {
        if (!signal_types[i].is_fixed && inv_conductance[i] > 0.0f) {
            float delta = through[i] * inv_conductance[i] * sor_omega;
            delta = std::clamp(delta, -MAX_DELTA, MAX_DELTA);

            float new_v = across[i] + delta;

            // Check for NaN or Inf
            if (!std::isfinite(new_v)) {
                spdlog::error("[SOR] Node {} exploded! v={:.2f} -> NaN/Inf. Resetting to 0V.", i, across[i]);
                across[i] = 0.0f;
                solver_exploded = true;
            } else {
                across[i] = new_v;
            }
        }
    }

    if (solver_exploded) {
        spdlog::warn("[SOR] Solver exploded this frame, recovered.");
    }
}
```

### 3. Parasitic Conductance (Priority: MEDIUM)
Add "leakage" to prevent floating nodes and divide-by-zero.

```cpp
void SimulationState::precompute_inv_conductance_safe() {
    constexpr float PARASITIC_G = 1e-7f;  // 10 MOhm leakage to ground

    for (size_t i = 0; i < conductance.size(); ++i) {
        if (signal_types[i].is_fixed) {
            inv_conductance[i] = 0.0f;
        } else {
            // Add parasitic conductance to prevent floating nodes
            float total_g = conductance[i] + PARASITIC_G;

            if (total_g > 1e-9f) {
                inv_conductance[i] = 1.0f / total_g;
            } else {
                // Even with parasitic G, this shouldn't happen
                inv_conductance[i] = 0.0f;
            }
        }
    }
}
```

**Benefits:**
- No more divide-by-zero when all relays are open
- Floating nodes default to 0V (ground reference)
- Prevents NaN cascades from topology changes

### 4. Adaptive Omega (Priority: LOW)
Dynamically adjust relaxation factor based on convergence.

```cpp
struct SolverStats {
    float last_max_change = 0.0f;
    uint32_t stable_frames = 0;
};

float get_adaptive_omega(float base_omega, const SolverStats& stats) {
    // Start conservative, increase if stable
    if (stats.stable_frames > 10) {
        return std::min(base_omega * 1.5f, 1.5f);  // Cap at 1.5
    } else if (stats.last_max_change > 10.0f) {
        return 1.0f;  // Back to Gauss-Seidel if unstable
    }
    return base_omega;
}
```

## Implementation Priority

### Phase 1: Critical Safety (Do Now)
1. **Delta clamping** - Prevent voltage explosions
2. **NaN detection** - Graceful degradation
3. **Parasitic conductance** - Prevent floating nodes

### Phase 2: Stability Monitoring (Do Later)
1. Adaptive omega
2. Convergence tracking
3. Solver health metrics

## Testing Strategy

```cpp
// Test case: DMR400 relay switching with extreme values
TEST(SORSafetyTest, RelaySwitching_DoesNotExplode) {
    // Create circuit with DMR400
    // Close relay (sudden topology change)
    // Verify no NaN/Inf in results
    // Verify max delta < 5V
}

// Test case: Short circuit recovery
TEST(SORSafetyTest, ShortCircuit_ClampsVoltage) {
    // Create short circuit
    // Run SOR for 100 iterations
    // Verify voltages stay within reasonable bounds
}
```

## Performance Impact

- **Delta clamping**: ~0% (just a clamp instruction)
- **NaN detection**: ~2% (branch prediction friendly)
- **Parasitic conductance**: ~1% (add during precompute)

**Total overhead**: ~3% for much better stability!

## Recommendation

Start with **Phase 1** - it's low-risk and high-reward. The delta clamping and NaN detection alone would prevent most solver explosions we see in production.

Adaptive omega can wait until we have real stability data from runtime usage.
