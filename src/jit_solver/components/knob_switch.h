#pragma once

#include "provider.h"
#include "component_enums.h"
#include "../state.h"
#include "../subsolvers/subsolver_types.h"

/// KnobSwitch - Multi-position rotary selector switch (2-5 positions).
/// Electrically: N-1 ConductanceBranch elements between common terminal and t1..tN.
/// The selected position has g_closed, all others have g_open.
/// Position is controlled via the 'control' input signal (0-based integer as float).
/// The 'position' output reflects the current selected position.
///
/// Ports:
///   - common  (InOut, Electrical) - shared terminal
///   - t1..tN  (InOut, Electrical) - selectable terminals (N = positions)
///   - control (In, Logical)       - sets position (0-based, clamped to [0, positions-1])
///   - position (Out, Logical)     - current position output
///
/// Params:
///   - positions: int 2-5 (number of selectable terminals)
///   - initial_position: int (default 0)
///   - g_open:  float (default 1e-6)
///   - g_closed: float (default 1000.0)
template <typename Provider = JitProvider>
class KnobSwitch {
public:
    static constexpr Domain domain = Domain::Electrical;
    static constexpr int MAX_POSITIONS = 5;

    Provider provider;

    /// One electrical handle per branch (common-to-t1, common-to-t2, etc.)
    ElectricalPrimitiveHandle electrical_handles[MAX_POSITIONS];
    int num_handles = 0;  ///< actual number of valid handles (= positions)

    int positions = 2;         ///< number of selectable terminals (2-5)
    int selected = 0;          ///< current selected position (0-based)
    float last_control = -1.0f; ///< last control value (for edge detection)
    float g_open = 1e-6f;
    float g_closed = 1000.0f;

    KnobSwitch() = default;

    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double dt);
    void pre_load();
};
