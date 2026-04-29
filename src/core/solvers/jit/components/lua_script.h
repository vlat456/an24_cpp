#pragma once

#include "core/solvers/common/provider.h"
#include "../state.h"
#include <cstdint>
#include <string>

struct lua_State;

/// Lua-scriptable component for rapid prototyping.
/// Fixed 16-in/16-out ports. User provides a Lua `process(inputs, dt)` function.
/// inputs[1]..inputs[16] map to ports in1..in16 (1-indexed).
/// Return a table with outputs[1]..outputs[N] for out1..outN.
template <typename Provider = JitProvider>
class LuaScript {
public:
    static constexpr Domain domain = Domain::Logical;
    static constexpr uint8_t MAX_PORTS = 16;

    Provider provider;

    std::string script;

    // Pre-resolved signal indices — populated at build time.
    uint32_t input_indices[MAX_PORTS] = {};
    uint32_t output_indices[MAX_PORTS] = {};
    uint8_t active_inputs = 0;
    uint8_t active_outputs = 0;

    LuaScript();
    ~LuaScript();

    LuaScript(const LuaScript&) = delete;
    LuaScript& operator=(const LuaScript&) = delete;
    LuaScript(LuaScript&& other) noexcept;
    LuaScript& operator=(LuaScript&& other) noexcept;

    void pre_load();
    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double /*dt*/) {}

    /// Hot-reload script between frames. Returns true on success.
    bool reload_script(const std::string& new_source);

private:
    lua_State* L_ = nullptr;
    int process_ref_ = 0;

    struct AllocState {
        size_t current = 0;
        static constexpr size_t maximum = 64 * 1024;
    };
    AllocState alloc_state_;

    friend void* lua_script_alloc(void* ud, void* ptr, size_t osize, size_t nsize);

    bool compile_script();
    lua_State* create_state();
};
