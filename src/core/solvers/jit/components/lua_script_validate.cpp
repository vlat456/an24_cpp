#include "lua_script_validate.h"

#include <lua.hpp>
#include <cstdlib>

static void* default_alloc(void*, void* ptr, size_t, size_t nsize) {
    if (nsize == 0) {
        std::free(ptr);
        return nullptr;
    }
    return std::realloc(ptr, nsize);
}

std::optional<std::string> lua_validate_script(std::string_view script) {
    if (script.empty()) {
        return "Script is empty";
    }

    lua_State* L = lua_newstate(default_alloc, nullptr);
    if (!L) {
        return "Failed to create Lua state";
    }

    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(L, 3);

    std::optional<std::string> result;

    std::string const script_str(script);
    if (luaL_dostring(L, script_str.c_str()) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        result = err ? std::string(err) : "Unknown compilation error";
        lua_pop(L, 1);
        lua_close(L);
        return result;
    }

    lua_getglobal(L, "process");
    if (!lua_isfunction(L, -1)) {
        result = "Script must define a 'process' function";
        lua_pop(L, 1);
        lua_close(L);
        return result;
    }
    lua_pop(L, 1);

    lua_close(L);
    return std::nullopt;
}
