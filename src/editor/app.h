#pragma once

// Temporary stub for app.h - TODO: rewrite for PushSolver
#include <string>
#include <unordered_map>

struct AppState {
    bool running = true;
};

// stub functions
inline void init_app_state(AppState& state) {}
inline void cleanup_app_state(AppState& state) {}
inline void handle_input(AppState& state) {}
