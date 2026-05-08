#include "lua_script.h"
#include "core/solvers/common/port_names.h"

#include <lua.hpp>
#include <cassert>
#include <cstring>
#include <utility>

// =====================================================================
// Instruction-count hook — prevents infinite loops
// =====================================================================

static constexpr int MAX_INSTRUCTIONS = 10000;

static void instruction_hook(lua_State* L, lua_Debug*) {
    luaL_error(L, "instruction budget exceeded (%d)", MAX_INSTRUCTIONS);
}

// =====================================================================
// Construction / destruction / move
// =====================================================================

template <typename Provider>
LuaScript<Provider>::LuaScript() {
    std::memset(input_indices, 0xFF, sizeof(input_indices));
    std::memset(output_indices, 0xFF, sizeof(output_indices));
}

template <typename Provider>
LuaScript<Provider>::~LuaScript() {
    if (L_) lua_close(L_);
}

template <typename Provider>
LuaScript<Provider>::LuaScript(LuaScript&& other) noexcept
    : provider(std::move(other.provider))
    , script(std::move(other.script))
    , L_(other.L_)
    , process_ref_(other.process_ref_)
    , ports_mapped_(other.ports_mapped_)
{
    std::memcpy(input_indices, other.input_indices, sizeof(input_indices));
    std::memcpy(output_indices, other.output_indices, sizeof(output_indices));
    other.L_ = nullptr;
    other.process_ref_ = 0;
    other.ports_mapped_ = false;
}

template <typename Provider>
LuaScript<Provider>& LuaScript<Provider>::operator=(LuaScript&& other) noexcept {
    if (this != &other) {
        if (L_) lua_close(L_);
        provider = std::move(other.provider);
        script = std::move(other.script);
        std::memcpy(input_indices, other.input_indices, sizeof(input_indices));
        std::memcpy(output_indices, other.output_indices, sizeof(output_indices));
        L_ = other.L_;
        process_ref_ = other.process_ref_;
        ports_mapped_ = other.ports_mapped_;
        other.L_ = nullptr;
        other.process_ref_ = 0;
        other.ports_mapped_ = false;
    }
    return *this;
}

// =====================================================================
// State creation + safe library loading
// =====================================================================

template <typename Provider>
lua_State* LuaScript<Provider>::create_state() {
    lua_State* L = luaL_newstate();
    if (!L) return nullptr;

    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(L, 3);

    return L;
}

// =====================================================================
// Script compilation
// =====================================================================

template <typename Provider>
bool LuaScript<Provider>::compile_script() {
    if (luaL_dostring(L_, script.c_str()) != LUA_OK) {
        lua_pop(L_, 1);
        return false;
    }

    lua_getglobal(L_, "process");
    if (!lua_isfunction(L_, -1)) {
        lua_pop(L_, 1);
        return false;
    }
    process_ref_ = luaL_ref(L_, LUA_REGISTRYINDEX);
    return true;
}

// =====================================================================
// pre_load — param-dependent init only (no port access)
// =====================================================================

template <typename Provider>
void LuaScript<Provider>::pre_load() {
    if (L_) lua_close(L_);
    L_ = create_state();
    if (L_) {
        compile_script();
    }
}

// =====================================================================
// map_ports_once — deferred port mapping on first execute()
// =====================================================================

template <typename Provider>
void LuaScript<Provider>::map_ports_once() {
    if (ports_mapped_) return;
    ports_mapped_ = true;

    constexpr PortNames input_ports[MAX_PORTS] = {
        PortNames::in1,  PortNames::in2,  PortNames::in3,  PortNames::in4,
        PortNames::in5,  PortNames::in6,  PortNames::in7,  PortNames::in8,
        PortNames::in9,  PortNames::in10, PortNames::in11, PortNames::in12,
        PortNames::in13, PortNames::in14, PortNames::in15, PortNames::in16,
    };
    constexpr PortNames output_ports[MAX_PORTS] = {
        PortNames::out1,  PortNames::out2,  PortNames::out3,  PortNames::out4,
        PortNames::out5,  PortNames::out6,  PortNames::out7,  PortNames::out8,
        PortNames::out9,  PortNames::out10, PortNames::out11, PortNames::out12,
        PortNames::out13, PortNames::out14, PortNames::out15, PortNames::out16,
    };

    for (uint8_t i = 0; i < MAX_PORTS; ++i) {
        if (provider.has(input_ports[i])) {
            input_indices[i] = provider.get(input_ports[i]);
        }
        if (provider.has(output_ports[i])) {
            output_indices[i] = provider.get(output_ports[i]);
        }
    }
}

// =====================================================================
// execute — hot path, called every frame
// =====================================================================

template <typename Provider>
void LuaScript<Provider>::execute(SimulationState& st, double dt) {
    map_ports_once();

    if (!L_ || process_ref_ == 0) return;

    lua_rawgeti(L_, LUA_REGISTRYINDEX, process_ref_);

    lua_createtable(L_, static_cast<int>(MAX_PORTS), 0);
    for (uint8_t i = 0; i < MAX_PORTS; ++i) {
        float const val = (input_indices[i] != UNMAPPED) ? st.signal(input_indices[i]) : 0.0f;
        lua_pushnumber(L_, static_cast<lua_Number>(val));
        lua_rawseti(L_, -2, static_cast<int>(i + 1));
    }

    lua_pushnumber(L_, static_cast<lua_Number>(dt));

    lua_sethook(L_, instruction_hook, LUA_MASKCOUNT, MAX_INSTRUCTIONS);

    int const status = lua_pcall(L_, 2, 1, 0);

    lua_sethook(L_, nullptr, 0, 0);

    if (status != LUA_OK) {
        lua_pop(L_, 1);
        for (uint8_t i = 0; i < MAX_PORTS; ++i) {
            if (output_indices[i] != UNMAPPED) {
                st.signal(output_indices[i]) = 0.0f;
            }
        }
        return;
    }

    if (lua_istable(L_, -1)) {
        for (uint8_t i = 0; i < MAX_PORTS; ++i) {
            if (output_indices[i] != UNMAPPED) {
                lua_rawgeti(L_, -1, static_cast<int>(i + 1));
                st.signal(output_indices[i]) = static_cast<float>(lua_tonumber(L_, -1));
                lua_pop(L_, 1);
            }
        }
    }
    lua_pop(L_, 1);
}

// =====================================================================
// Explicit instantiation
// =====================================================================

template class LuaScript<JitProvider>;
