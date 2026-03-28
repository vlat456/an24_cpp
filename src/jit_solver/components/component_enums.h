#pragma once

#include <cstdint>

// =============================================================================
// Component Enums
// =============================================================================

/// GS24 operating modes
enum class GS24Mode { OFF, STARTER, STARTER_WAIT, GENERATOR };

/// APU (ВСУ) operating states - РУ19А-300
enum class APUState { OFF, CRANKING, IGNITION, RUNNING, STOPPING };
