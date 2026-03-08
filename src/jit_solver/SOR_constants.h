#pragma once

/// Central SOR (Successive Over-Relaxation) solver constants.
/// Used by JIT, AOT codegen, editor simulation, tests, and examples.
namespace SOR {

/// Over-relaxation factor.
/// 1.0 = Gauss-Seidel, 1.2-1.5 = stable for aircraft electrical systems.
/// Values above 1.5 risk divergence on stiff circuits.
constexpr float OMEGA = 1.3f;

} // namespace SOR

/// Domain scheduling constants — single source of truth for JIT, AOT, and editor.
/// Sub-rate domains fire every Nth step of the main loop.
/// Solvers receive accumulated dt over those N steps (FPS-independent).
namespace DomainSchedule {

constexpr int MECHANICAL_PERIOD = 3;   // every 3rd step
constexpr int HYDRAULIC_PERIOD  = 12;  // every 12th step
constexpr int THERMAL_PERIOD    = 60;  // every 60th step
constexpr int CYCLE_LENGTH      = 60;  // LCM of all periods — one full scheduling cycle

} // namespace DomainSchedule
