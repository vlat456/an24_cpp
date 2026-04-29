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

    static constexpr uint32_t UNMAPPED = UINT32_MAX;

    uint32_t input_indices[MAX_PORTS];
    uint32_t output_indices[MAX_PORTS];

    LuaScript();
    ~LuaScript();

    LuaScript(const LuaScript&) = delete;
    LuaScript& operator=(const LuaScript&) = delete;
    LuaScript(LuaScript&& other) noexcept;
    LuaScript& operator=(LuaScript&& other) noexcept;

    void pre_load();
    void execute(SimulationState& st, double dt);
    void commit(SimulationState& st, double /*dt*/) {}

private:
    lua_State* L_ = nullptr;
    int process_ref_ = 0;
    bool ports_mapped_ = false;

    bool compile_script();
    lua_State* create_state();
    void map_ports_once();
};
