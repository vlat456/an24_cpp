#include <gtest/gtest.h>
#include "core/solvers/jit/components/all.h"
#include "core/solvers/common/port_registry.h"
#include "core/solvers/jit/state.h"

static constexpr double DT = 1.0 / 60.0;

static LuaScript<JitProvider> make_lua(
    const std::string& script,
    uint8_t n_in = 1,
    uint8_t n_out = 1)
{
    LuaScript<JitProvider> comp;
    comp.script = script;

    static constexpr PortNames input_ports[] = {
        PortNames::in1,  PortNames::in2,  PortNames::in3,  PortNames::in4,
        PortNames::in5,  PortNames::in6,  PortNames::in7,  PortNames::in8,
        PortNames::in9,  PortNames::in10, PortNames::in11, PortNames::in12,
        PortNames::in13, PortNames::in14, PortNames::in15, PortNames::in16,
    };
    static constexpr PortNames output_ports[] = {
        PortNames::out1,  PortNames::out2,  PortNames::out3,  PortNames::out4,
        PortNames::out5,  PortNames::out6,  PortNames::out7,  PortNames::out8,
        PortNames::out9,  PortNames::out10, PortNames::out11, PortNames::out12,
        PortNames::out13, PortNames::out14, PortNames::out15, PortNames::out16,
    };

    for (uint8_t i = 0; i < n_in; ++i) {
        comp.provider.set(input_ports[i], i);
    }
    for (uint8_t i = 0; i < n_out; ++i) {
        comp.provider.set(output_ports[i], n_in + i);
    }

    comp.pre_load();
    return comp;
}

static SimulationState make_state(size_t n_signals, float fill = 0.0f) {
    SimulationState st;
    st.values.resize(n_signals, fill);
    return st;
}

void step(LuaScript<JitProvider>& comp, SimulationState& st, double dt) {
    comp.execute(st, dt);
}

// =============================================================================
// Compilation
// =============================================================================

TEST(LuaScriptTest, ValidScriptCompiles) {
    auto comp = make_lua("function process(inputs, dt) return {inputs[1]} end");
    EXPECT_NE(comp.provider.get(PortNames::out1), JitProvider::UNMAPPED);
}

TEST(LuaScriptTest, InvalidScriptDoesNotCrash) {
    auto comp = make_lua("this is not valid lua!!!");
    auto st = make_state(2);
    step(comp, st, DT);
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
}

TEST(LuaScriptTest, MissingProcessFunctionDoesNotCrash) {
    auto comp = make_lua("x = 42");
    auto st = make_state(2);
    step(comp, st, DT);
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
}

// =============================================================================
// Basic execution
// =============================================================================

TEST(LuaScriptTest, PassThrough) {
    auto comp = make_lua("function process(inputs, dt) return {inputs[1]} end");
    auto st = make_state(2);
    st.values[0] = 42.0f;
    step(comp, st, DT);
    EXPECT_FLOAT_EQ(st.values[1], 42.0f);
}

TEST(LuaScriptTest, MultiplyByTwo) {
    auto comp = make_lua("function process(inputs, dt) return {inputs[1] * 2} end");
    auto st = make_state(2);
    st.values[0] = 3.5f;
    step(comp, st, DT);
    EXPECT_FLOAT_EQ(st.values[1], 7.0f);
}

TEST(LuaScriptTest, UsesDt) {
    auto comp = make_lua("function process(inputs, dt) return {inputs[1] * dt * 60} end");
    auto st = make_state(2);
    st.values[0] = 1.0f;
    step(comp, st, DT);
    EXPECT_NEAR(st.values[1], 1.0f, 0.01f);
}

// =============================================================================
// Multiple ports
// =============================================================================

TEST(LuaScriptTest, TwoInputsTwoOutputs) {
    auto comp = make_lua("function process(inputs, dt) return {inputs[1]+inputs[2], inputs[1]*inputs[2]} end", 2, 2);
    auto st = make_state(4);
    st.values[0] = 3.0f;
    st.values[1] = 4.0f;
    step(comp, st, DT);
    EXPECT_FLOAT_EQ(st.values[2], 7.0f);
    EXPECT_FLOAT_EQ(st.values[3], 12.0f);
}

// =============================================================================
// Error handling
// =============================================================================

TEST(LuaScriptTest, RuntimeErrorZerosOutputs) {
    auto comp = make_lua("function process(inputs, dt) error('boom') end");
    auto st = make_state(2);
    st.values[0] = 99.0f;
    step(comp, st, DT);
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
}

TEST(LuaScriptTest, InfiniteLoopKilledByHook) {
    auto comp = make_lua("function process(inputs, dt) while true do end end");
    auto st = make_state(2);
    st.values[0] = 1.0f;
    step(comp, st, DT);
    EXPECT_FLOAT_EQ(st.values[1], 0.0f);
}

// =============================================================================
// Hot reload
// =============================================================================

TEST(LuaScriptTest, HotReloadSucceeds) {
    auto comp = make_lua("function process(inputs, dt) return {1} end");
    auto st = make_state(2);
    step(comp, st, DT);
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);

    bool ok = comp.reload_script("function process(inputs, dt) return {2} end");
    EXPECT_TRUE(ok);

    step(comp, st, DT);
    EXPECT_FLOAT_EQ(st.values[1], 2.0f);
}

TEST(LuaScriptTest, HotReloadBadScriptKeepsOld) {
    auto comp = make_lua("function process(inputs, dt) return {42} end");
    auto st = make_state(2);
    step(comp, st, DT);
    EXPECT_FLOAT_EQ(st.values[1], 42.0f);

    bool ok = comp.reload_script("this is broken!!!");
    EXPECT_FALSE(ok);

    step(comp, st, DT);
    EXPECT_FLOAT_EQ(st.values[1], 42.0f);
}

// =============================================================================
// Math library available
// =============================================================================

TEST(LuaScriptTest, MathLibraryAvailable) {
    auto comp = make_lua("function process(inputs, dt) return {math.abs(inputs[1])} end");
    auto st = make_state(2);
    st.values[0] = -5.0f;
    step(comp, st, DT);
    EXPECT_FLOAT_EQ(st.values[1], 5.0f);
}

// =============================================================================
// Lua closures preserve state across frames
// =============================================================================

TEST(LuaScriptTest, ClosureStatePersistsAcrossFrames) {
    auto comp = make_lua(
        "local counter = 0\n"
        "function process(inputs, dt)\n"
        "  counter = counter + 1\n"
        "  return {counter}\n"
        "end");
    auto st = make_state(2);

    step(comp, st, DT);
    EXPECT_FLOAT_EQ(st.values[1], 1.0f);

    step(comp, st, DT);
    EXPECT_FLOAT_EQ(st.values[1], 2.0f);

    step(comp, st, DT);
    EXPECT_FLOAT_EQ(st.values[1], 3.0f);
}
