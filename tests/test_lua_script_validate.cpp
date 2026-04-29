#include <gtest/gtest.h>
#include "core/solvers/jit/components/lua_script_validate.h"

TEST(LuaValidate, ValidScript_ReturnsNullopt) {
    auto result = lua_validate_script(
        "function process(inputs, dt) return {inputs[1]} end");
    EXPECT_FALSE(result.has_value());
}

TEST(LuaValidate, EmptyScript_ReturnsError) {
    auto result = lua_validate_script("");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->empty());
}

TEST(LuaValidate, SyntaxError_ReturnsError) {
    auto result = lua_validate_script("function broken(");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->empty());
}

TEST(LuaValidate, MissingProcessFunction_ReturnsError) {
    auto result = lua_validate_script("x = 42");
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->find("process"), std::string::npos);
}

TEST(LuaValidate, ProcessNotFunction_ReturnsError) {
    auto result = lua_validate_script("process = 42");
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->find("process"), std::string::npos);
}

TEST(LuaValidate, ValidWithMathLib_ReturnsNullopt) {
    auto result = lua_validate_script(
        "function process(inputs, dt)\n"
        "  local v = math.abs(inputs[1] or 0)\n"
        "  return {v}\n"
        "end");
    EXPECT_FALSE(result.has_value());
}

TEST(LuaValidate, RuntimeCrashScript_CompilesFine) {
    auto result = lua_validate_script(
        "function process(inputs, dt)\n"
        "  error('boom')\n"
        "  return {}\n"
        "end");
    EXPECT_FALSE(result.has_value());
}

TEST(LuaValidate, ValidClosure_CompilesFine) {
    auto result = lua_validate_script(
        "local scale = 2.0\n"
        "function process(inputs, dt)\n"
        "  return {inputs[1] * scale}\n"
        "end");
    EXPECT_FALSE(result.has_value());
}
