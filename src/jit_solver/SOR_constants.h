#pragma once

/// Central SOR (Successive Over-Relaxation) solver constants.
/// Used by JIT, AOT codegen, editor simulation, tests, and examples.
namespace SOR {

/// Over-relaxation factor.
/// 1.0 = Gauss-Seidel, >1.0 = over-relaxed update.
/// Canonical project default (kept for regression compatibility).
constexpr float OMEGA = 1.3f;

/// Number of relaxation sweeps per simulation frame.
/// Keep at 1 with the current stamp-then-solve pipeline.
/// Increasing this without re-stamping between sweeps changes physics.
constexpr int INNER_SWEEPS = 1;

} // namespace SOR

/// Adaptive omega tuning for runtime JIT solver stability.
/// Keep these centralized to avoid magic numbers in simulator loop.
namespace SORAdaptive {

constexpr bool ENABLED = true;
constexpr float OMEGA_MIN = 1.0f;
constexpr float ERROR_WORSE_FACTOR = 1.05f;
constexpr float ERROR_BETTER_FACTOR = 0.85f;
constexpr float OMEGA_DOWNSCALE = 0.90f;
constexpr float OMEGA_UPSCALE = 1.01f;

} // namespace SORAdaptive

/// JIT/editor build-time electrical warning thresholds.
/// These are diagnostics only (do not affect solver math).
namespace JitElectricalWarnings {

constexpr float HIGH_CURRENT_INFO_A = 300.0f;
constexpr float NEAR_SHORT_WARN_CURRENT_A = 1500.0f;
constexpr float GROUND_REF_EPS = 1e-4f;

} // namespace JitElectricalWarnings

/// Domain scheduling constants — single source of truth for JIT, AOT, and editor.
/// Sub-rate domains fire every Nth step of the main loop.
/// Solvers receive accumulated dt over those N steps (FPS-independent).
namespace DomainSchedule {

constexpr int MECHANICAL_PERIOD = 3;   // every 3rd step
constexpr int HYDRAULIC_PERIOD  = 12;  // every 12th step
constexpr int THERMAL_PERIOD    = 60;  // every 60th step
constexpr int CYCLE_LENGTH      = 60;  // LCM of all periods — one full scheduling cycle

} // namespace DomainSchedule
