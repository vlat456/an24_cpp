#pragma once

#include "simvar_provider.h"
#include "core/model/component_kind.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

/// Mock provider for headless testing.
///
/// Simulates an external data source without any SimConnect code.
/// build() scans JitBuildInput for SimConnectInput/SimConnectOutput nodes and stores
/// their signal indices and types. read_into() writes mock values into the
/// signal array with type-aware conversion; write_from() captures output
/// values for test inspection.
class MockProvider final : public SimVarProvider {
public:
    MockProvider() = default;
    ~MockProvider() override = default;

    const char* name() const override { return "Mock"; }

    void build(const JitBuildInput& input, JIT_Simulator& /*sim*/) override;

    bool connect() override { connected_ = true; return true; }
    void disconnect() override { connected_ = false; }
    bool is_connected() const override { return connected_; }

    void poll(double /*elapsed_time*/) override {}

    void read_into(float* values, uint32_t count) override;
    void write_from(const float* values, uint32_t count) override;

    std::optional<SignalType> signal_type(uint32_t signal_index) const override;

    // ==...== Test-only API ==...==

    /// Set a mock input value (float overload).
    void set_input(uint32_t signal_index, float value);
    /// Set a mock input value (int32_t overload).
    void set_input(uint32_t signal_index, int32_t value);
    /// Set a mock input value (bool overload).
    void set_input(uint32_t signal_index, bool value);

    /// Get the last captured output value as float.
    float get_output_f(uint32_t signal_index) const;
    /// Get the last captured output value as int32_t.
    int32_t get_output_i(uint32_t signal_index) const;
    /// Get the last captured output value as bool.
    bool get_output_b(uint32_t signal_index) const;

    /// Number of input (SimConnectInput) signal indices mapped.
    size_t input_count() const { return input_indices_.size(); }

    /// Number of output (SimConnectOutput) signal indices mapped.
    size_t output_count() const { return output_indices_.size(); }

    /// Register "mock" provider type with the host registry.
    static void register_type();

private:
    bool connected_ = false;

    /// Per-signal type information.
    std::unordered_map<uint32_t, SignalType> signal_types_;

    std::vector<uint32_t> input_indices_;   ///< Signal indices for SimConnectInput nodes
    std::vector<uint32_t> output_indices_;  ///< Signal indices for SimConnectOutput nodes
    std::vector<float> default_values_;     ///< Default values per input index

    // Internal typed value storage
    struct TypedValue {
        SignalType type = SignalType::Float32;
        union {
            float    f32;
            int32_t  i32;
            uint32_t u32;  ///< Bool stored as u32 (0/1), matching WireValue convention
        };
        TypedValue() : f32(0.0f) {}
        explicit TypedValue(float v)    : type(SignalType::Float32), f32(v) {}
        explicit TypedValue(int32_t v)  : type(SignalType::Int32),   i32(v) {}
        explicit TypedValue(bool v)     : type(SignalType::Bool),    u32(v ? 1u : 0u) {}
    };

    std::unordered_map<uint32_t, TypedValue> mock_inputs_;
    std::unordered_map<uint32_t, TypedValue> mock_outputs_;

    /// Convert a TypedValue to float for the simulator.
    static float to_float(const TypedValue& tv);
    /// Convert a float from the simulator to a TypedValue.
    static TypedValue from_float(float value, SignalType type);
};
