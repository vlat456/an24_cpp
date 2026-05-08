#include "mock_provider.h"
#include "simvar_provider_host.h"
#include "core/strings/interned_id.h"

#include <spdlog/spdlog.h>

void MockProvider::register_type() {
    SimvarProviderHost::register_provider("mock", [] {
        return std::make_unique<MockProvider>();
    });
}

// ==...== Type conversion helpers ==...==

float MockProvider::to_float(const TypedValue& tv) {
    switch (tv.type) {
        case SignalType::Float32: return tv.f32;
        case SignalType::Int32:   return static_cast<float>(tv.i32);
        case SignalType::Bool:    return tv.u32 != 0 ? 1.0f : 0.0f;
    }
    return 0.0f;  // unreachable
}

MockProvider::TypedValue MockProvider::from_float(float value, SignalType type) {
    switch (type) {
        case SignalType::Float32: return TypedValue(value);
        case SignalType::Int32:   return TypedValue(static_cast<int32_t>(value));
        case SignalType::Bool:    return TypedValue(value > SIGNAL_BOOL_THRESHOLD);
    }
    return TypedValue{};  // unreachable
}

// ==...== Build ==...==

void MockProvider::build(const JitBuildInput& input, JIT_Simulator& /*sim*/) {
    input_indices_.clear();
    output_indices_.clear();
    default_values_.clear();
    signal_types_.clear();
    mock_inputs_.clear();
    mock_outputs_.clear();

    for (const auto& device : input.devices) {
        if (device.kind != ComponentKind::SimConnectInput &&
            device.kind != ComponentKind::SimConnectOutput) {
            continue;
        }

        const bool is_input = (device.kind == ComponentKind::SimConnectInput);
        const std::string port_name = is_input ? "out" : "in";

        // Resolve signal key
        const std::string key_str = device.name + "." + port_name;
        core::InternedId const signal_key = input.signal_key_interner.lookup(key_str);
        if (signal_key.empty()) {
            spdlog::warn("[MockProvider] Device '{}' port '{}' key not interned, skipping",
                         device.name, port_name);
            continue;
        }

        auto it = input.port_to_signal.find(signal_key);
        if (it == input.port_to_signal.end()) {
            spdlog::warn("[MockProvider] Device '{}' port '{}' not mapped to signal, skipping",
                         device.name, port_name);
            continue;
        }
        const uint32_t signal_index = it->second;

        // Parse default_value
        float default_value = 0.0f;
        auto param_it = device.params.find("default_value");
        if (param_it != device.params.end()) {
            try {
                default_value = std::stof(param_it->second);
            } catch (const std::exception& /*e*/) {
                spdlog::warn("[MockProvider] Device '{}' has invalid default_value '{}', using 0.0",
                             device.name, param_it->second);
            }
        }

        // Parse val_type (default Float32)
        SignalType stype = SignalType::Float32;
        auto type_it = device.params.find("val_type");
        if (type_it != device.params.end()) {
            const std::string& tstr = type_it->second;
            if (tstr == "Int32")   stype = SignalType::Int32;
            else if (tstr == "Bool")   stype = SignalType::Bool;
            else if (tstr == "Float32") stype = SignalType::Float32;
        }
        signal_types_[signal_index] = stype;

        if (is_input) {
            input_indices_.push_back(signal_index);
            default_values_.push_back(default_value);
        } else {
            output_indices_.push_back(signal_index);
        }
    }

    spdlog::info("[MockProvider] Mapped {} inputs, {} outputs",
                 input_indices_.size(), output_indices_.size());
}

// ==...== I/O ==...==

void MockProvider::read_into(float* values, uint32_t count) {
    if (!values || count == 0) return;
    for (size_t i = 0; i < input_indices_.size(); ++i) {
        const uint32_t idx = input_indices_[i];
        if (idx >= count) continue;

        auto it = mock_inputs_.find(idx);
        if (it != mock_inputs_.end()) {
            values[idx] = to_float(it->second);
        } else {
            values[idx] = default_values_[i];
        }
    }
}

void MockProvider::write_from(const float* values, uint32_t count) {
    if (!values || count == 0) return;
    for (uint32_t const idx : output_indices_) {
        if (idx >= count) continue;
        auto type_it = signal_types_.find(idx);
        SignalType const stype = (type_it != signal_types_.end()) ? type_it->second : SignalType::Float32;
        mock_outputs_[idx] = from_float(values[idx], stype);
    }
}

std::optional<SignalType> MockProvider::signal_type(uint32_t signal_index) const {
    auto it = signal_types_.find(signal_index);
    if (it != signal_types_.end()) return it->second;
    return std::nullopt;
}

// ==...== Test-only API ==...==

void MockProvider::set_input(uint32_t signal_index, float value) {
    mock_inputs_[signal_index] = TypedValue(value);
}

void MockProvider::set_input(uint32_t signal_index, int32_t value) {
    mock_inputs_[signal_index] = TypedValue(value);
}

void MockProvider::set_input(uint32_t signal_index, bool value) {
    mock_inputs_[signal_index] = TypedValue(value);
}

float MockProvider::get_output_f(uint32_t signal_index) const {
    auto it = mock_outputs_.find(signal_index);
    return (it != mock_outputs_.end()) ? to_float(it->second) : 0.0f;
}

int32_t MockProvider::get_output_i(uint32_t signal_index) const {
    auto it = mock_outputs_.find(signal_index);
    if (it == mock_outputs_.end()) return 0;
    switch (it->second.type) {
        case SignalType::Float32: return static_cast<int32_t>(it->second.f32);
        case SignalType::Int32:   return it->second.i32;
        case SignalType::Bool:    return it->second.u32 != 0 ? 1 : 0;
    }
    return 0;
}

bool MockProvider::get_output_b(uint32_t signal_index) const {
    auto it = mock_outputs_.find(signal_index);
    if (it == mock_outputs_.end()) return false;
    switch (it->second.type) {
        case SignalType::Float32: return it->second.f32 > SIGNAL_BOOL_THRESHOLD;
        case SignalType::Int32:   return it->second.i32 != 0;
        case SignalType::Bool:    return it->second.u32 != 0;
    }
    return false;
}
