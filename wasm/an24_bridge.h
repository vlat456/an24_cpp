#pragma once

#include <cstdint>
#include <cstddef>

/// Module identity — used in panel.cfg / systems.cfg:
///   htmlgaugeNN=...wasm_module=an24_bridge.wasm&wasm_gauge=an24_bridge
///   [WASM_SYSTEM.N] ModulePath=an24_bridge  SystemName=an24_bridge
static constexpr const char* MODULE_NAME = "an24_bridge";
static constexpr const char* GAUGE_NAME  = "an24_bridge";
static constexpr const char* SYSTEM_NAME = "an24_bridge";

/// The bridge gauge is a data conduit (not a visual instrument).
/// Minimal 1x1 texture to avoid wasting VRAM.
static constexpr int GAUGE_SIZE_X = 1;
static constexpr int GAUGE_SIZE_Y = 1;

/// Module-global frame counter, incremented each system_update call.
extern uint32_t g_module_frame_count;
extern bool     g_module_initialized;
