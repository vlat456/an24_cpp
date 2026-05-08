#include "simconnect_provider.h"
#include "simconnect/simconnect_coordinator.h"
#include "core/solvers/jit/bridge/simvar_provider_host.h"
#include "core/solvers/jit/components/all.h"
#include "core/strings/interned_id.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <cassert>

// ==...== SimVarProvider Interface ==...==

void SimConnectProvider::register_type() {
    SimvarProviderHost::register_provider("simconnect", [] {
        return std::make_unique<SimConnectProvider>();
    });
}

SimConnectProvider::SimConnectProvider()
    : SimConnectProvider(SimConnectCoordinator::instance()) {
}

SimConnectProvider::SimConnectProvider(SimConnectCoordinator& coordinator)
    : coordinator_(&coordinator),
      send_buffer_(MAX_PACKET_SIZE) {
}

SimConnectProvider::~SimConnectProvider() {
    disconnect();
}

void SimConnectProvider::build(const JitBuildInput& input, JIT_Simulator& /*sim*/) {
    build_mappings(input);
}

std::optional<SimConnectClient*> SimConnectProvider::client() {
    return coordinator_ ? coordinator_->client() : std::nullopt;
}

std::optional<const SimConnectClient*> SimConnectProvider::client() const {
    return coordinator_ ? coordinator_->client() : std::nullopt;
}

bool SimConnectProvider::connect() {
    if (!coordinator_) return false;
    coordinator_->add_provider(this);
    if (!coordinator_->connect(this)) {
        spdlog::error("[SimConnectProvider] Failed to connect to SimConnect");
        return false;
    }
    spdlog::info("[SimConnectProvider] Connected to SimConnect");
    return true;
}

void SimConnectProvider::disconnect() {
    if (coordinator_) {
        coordinator_->disconnect(this);
        coordinator_->remove_provider(this);
    }
}

bool SimConnectProvider::is_connected() const {
    return coordinator_ && coordinator_->is_connected();
}

void SimConnectProvider::poll(double elapsed_time) {
    if (!coordinator_ || !coordinator_->is_connected()) return;

    current_time_ = elapsed_time;
    coordinator_->poll(elapsed_time);

    maybe_send_ping(current_time_);

    if (connection_healthy_ &&
        current_time_ - last_pong_recv_time_ > PONG_TIMEOUT_SEC &&
        last_pong_recv_time_ > 0.0) {
        connection_healthy_ = false;
        spdlog::warn("[SimConnectProvider] WASM bridge unhealthy — no Pong for {:.1f}s",
                     current_time_ - last_pong_recv_time_);
    }
}

void SimConnectProvider::read_into(float* values, uint32_t count) {
    request_inputs();
    inject_inputs_raw(values, count);
}

void SimConnectProvider::write_from(const float* values, uint32_t count) {
    extract_outputs_raw(values, count);
}

void SimConnectProvider::handle_response(std::span<const uint8_t> payload) {
    on_response(payload);
}

std::optional<SignalType> SimConnectProvider::signal_type(uint32_t signal_index) const {
    ValType vt = val_type_for_signal(signal_index);
    switch (vt) {
        case ValType::Float32: return SignalType::Float32;
        case ValType::Int32:   return SignalType::Int32;
        case ValType::Bool:    return SignalType::Bool;
    }
    return std::nullopt;
}

// ==...== Setup ==...==

void SimConnectProvider::build_mappings(const JitBuildInput& input) {
    input_mappings_.clear();
    output_mappings_.clear();
    input_buffer_.clear();
    id_to_signal_.clear();
    output_shadow_.clear();
    signal_to_val_type_.clear();
    frame_counter_ = 0;
    host_epoch_ = 0;

    for (const auto& device : input.devices) {
        if (device.kind != ComponentKind::SimConnectInput &&
            device.kind != ComponentKind::SimConnectOutput) {
            continue;
        }

        auto mapping = parse_mapping(device, input);
        if (!mapping) continue;

        mapping->intern_id = coordinator_->intern_table().intern(mapping->var_type, mapping->var_name);
        id_to_signal_[mapping->intern_id] = mapping->signal_index;
        signal_to_val_type_[mapping->signal_index] = mapping->val_type;

        if (mapping->direction == SimVarDirection::Input) {
            input_buffer_[mapping->signal_index] = WireValue(mapping->default_value);
            input_mappings_.push_back(std::move(*mapping));
        } else {
            output_shadow_[mapping->signal_index] = WireValue(0.0f);
            output_mappings_.push_back(std::move(*mapping));
        }
    }

    spdlog::info("[SimConnectProvider] Mapped {} inputs, {} outputs (V2 delta protocol)",
                 input_mappings_.size(), output_mappings_.size());
}

std::optional<SimVarMapping> SimConnectProvider::parse_mapping(
    const SolverDevice& device, const JitBuildInput& input) {

    SimVarMapping mapping;

    auto it = device.params.find("var_name");
    if (it == device.params.end() || it->second.empty()) {
        spdlog::warn("[SimConnectProvider] Device '{}' has empty var_name, skipping", device.name);
        return std::nullopt;
    }
    mapping.var_name = it->second;

    it = device.params.find("var_type");
    std::string var_type_str = (it != device.params.end()) ? it->second : "AVar";
    if (!parse_var_type(var_type_str, mapping.var_type)) {
        spdlog::warn("[SimConnectProvider] Device '{}' has unknown var_type '{}', defaulting to AVar",
                     device.name, var_type_str);
        mapping.var_type = VarType::AVar;
    }

    it = device.params.find("unit");
    mapping.unit = (it != device.params.end()) ? it->second : "number";

    it = device.params.find("index");
    if (it != device.params.end()) {
        try { mapping.index = std::stoi(it->second); } catch (...) { mapping.index = 0; }
    }

    it = device.params.find("default_value");
    if (it != device.params.end()) {
        try { mapping.default_value = std::stof(it->second); } catch (...) { mapping.default_value = 0.0f; }
    }

    it = device.params.find("mode");
    mapping.mode = (it != device.params.end() && it->second == "event")
        ? SimVarMode::Event : SimVarMode::Data;

    it = device.params.find("tier");
    if (it != device.params.end()) {
        try { mapping.tier = static_cast<uint8_t>(std::stoi(it->second)); } catch (...) {
            mapping.tier = default_tier(mapping.var_type);
        }
    } else {
        mapping.tier = default_tier(mapping.var_type);
    }

    it = device.params.find("epsilon");
    if (it != device.params.end()) {
        try { mapping.epsilon = std::stof(it->second); } catch (...) {
            mapping.epsilon = default_epsilon(mapping.var_type);
        }
    } else {
        mapping.epsilon = default_epsilon(mapping.var_type);
    }

    it = device.params.find("val_type");
    if (it != device.params.end()) {
        const std::string& vt = it->second;
        if (vt == "Int32")   mapping.val_type = ValType::Int32;
        else if (vt == "Bool")   mapping.val_type = ValType::Bool;
        else if (vt == "Float32") mapping.val_type = ValType::Float32;
    }

    mapping.direction = (device.kind == ComponentKind::SimConnectInput)
        ? SimVarDirection::Input : SimVarDirection::Output;

    std::string port_name = (mapping.direction == SimVarDirection::Input) ? "out" : "in";
    std::string key_str = device.name + "." + port_name;
    mapping.signal_key = input.signal_key_interner.lookup(key_str);
    if (mapping.signal_key.empty()) {
        spdlog::warn("[SimConnectProvider] Device '{}' port '{}' key not interned, skipping",
                     device.name, port_name);
        return std::nullopt;
    }

    auto signal_it = input.port_to_signal.find(mapping.signal_key);
    if (signal_it == input.port_to_signal.end()) {
        spdlog::warn("[SimConnectProvider] Device '{}' port '{}' not mapped to signal, skipping",
                     device.name, port_name);
        return std::nullopt;
    }
    mapping.signal_index = signal_it->second;

    return mapping;
}

// ==...== Frame I/O ==...==

void SimConnectProvider::request_inputs() {
    if (input_mappings_.empty() || !is_connected()) return;

    uint16_t tier_mask = tier_mask_for_frame(frame_counter_);

    size_t written = WireCodec::build_delta_read(
        send_buffer_.data(), send_buffer_.size(),
        tier_mask, host_epoch_);

    frame_counter_++;
    host_epoch_++;

    if (written > 0) {
        send_bytes(send_buffer_.data(), written);
    }
}

float wire_value_to_float(const WireValue& wv, ValType vt) {
    switch (vt) {
        case ValType::Float32: return wv.f32;
        case ValType::Int32:   return static_cast<float>(wv.i32);
        case ValType::Bool:    return wv.u32 != 0 ? 1.0f : 0.0f;
    }
    spdlog::warn("[SimConnectProvider] Unknown ValType {} in wire_value_to_float, returning 0",
                 static_cast<uint8_t>(vt));
    assert(false && "Unknown ValType — protocol mismatch?");
    return 0.0f;
}

WireValue float_to_wire_value(float value, ValType vt) {
    switch (vt) {
        case ValType::Float32: return WireValue(value);
        case ValType::Int32:   return WireValue(static_cast<int32_t>(value));
        case ValType::Bool:    return WireValue(value > SIGNAL_BOOL_THRESHOLD);
    }
    spdlog::warn("[SimConnectProvider] Unknown ValType {} in float_to_wire_value, returning zero",
                 static_cast<uint8_t>(vt));
    assert(false && "Unknown ValType — protocol mismatch?");
    return WireValue{};
}

void SimConnectProvider::inject_inputs_raw(float* values, uint32_t values_count) {
    if (input_mappings_.empty() || !values) return;

    for (const auto& m : input_mappings_) {
        if (m.signal_index >= values_count) continue;

        auto it = input_buffer_.find(m.signal_index);
        if (it != input_buffer_.end()) {
            values[m.signal_index] = wire_value_to_float(it->second, m.val_type);
        }
    }
}

void SimConnectProvider::extract_outputs_raw(const float* values, uint32_t values_count) {
    if (output_mappings_.empty() || !is_connected() || !values) return;

    output_records_.clear();

    for (const auto& m : output_mappings_) {
        if (m.signal_index >= values_count) continue;

        float value = values[m.signal_index];
        WireValue current = float_to_wire_value(value, m.val_type);

        auto shadow_it = output_shadow_.find(m.signal_index);
        if (shadow_it != output_shadow_.end()) {
            if (!value_changed(current, shadow_it->second, m.epsilon, m.val_type)) {
                continue;
            }
        }

        output_shadow_[m.signal_index] = current;
        VarRecord rec;
        rec.var_type = (m.mode == SimVarMode::Event) ? VarType::HEvent : m.var_type;
        rec.val_type = m.val_type;
        rec.name_id  = m.intern_id;
        rec.value    = current;
        output_records_.push_back(rec);
    }

    if (!output_records_.empty()) {
        size_t written = WireCodec::build_delta_write(
            send_buffer_.data(), send_buffer_.size(),
            output_records_, host_epoch_);

        if (written > 0) {
            send_bytes(send_buffer_.data(), written);
        }
    }
}

// ==...== CommBus ==...==

void SimConnectProvider::on_response(std::span<const uint8_t> payload) {
    const auto* data = payload.data();
    size_t len = payload.size();

    if (len >= sizeof(PacketHeader)) {
        auto result = WireCodec::parse(data, len);
        if (result.header) {
            const PacketHeader& hdr = *result.header;
            auto cmd = header_cmd(hdr);

            if (cmd != Cmd::Pong) {
                uint16_t epoch_gap = host_epoch_ - hdr.seq_id;
                if (epoch_gap > 100) {
                    spdlog::debug("[SimConnectProvider] Stale response epoch={} (current={}, gap={}), ignoring",
                                  hdr.seq_id, host_epoch_, epoch_gap);
                    return;
                }
            }

            switch (cmd) {
                case Cmd::DeltaUpdate:
                case Cmd::FullSync:
                    for (const auto& rec : result.records) {
                        auto it = id_to_signal_.find(rec.name_id);
                        if (it != id_to_signal_.end()) {
                            input_buffer_[it->second] = rec.value;
                        }
                    }
                    break;
                case Cmd::WriteAck:
                    spdlog::debug("[SimConnectProvider] Write ack: epoch={}", hdr.seq_id);
                    break;
                case Cmd::Pong:
                    handle_pong(hdr);
                    break;
                default:
                    spdlog::debug("[SimConnectProvider] Unexpected binary cmd: 0x{:02x}", hdr.cmd);
                    break;
            }
            return;
        }
        // Header-sized payload but invalid binary (bad magic/version) — don't try JSON.
        spdlog::warn("[SimConnectProvider] Invalid binary packet ({} bytes, bad magic/version)", len);
        return;
    }

    // Short payloads are treated as JSON (control channel).
    handle_json_response(payload);
}

void SimConnectProvider::handle_json_response(std::span<const uint8_t> payload) {
    try {
        std::string_view sv(reinterpret_cast<const char*>(payload.data()), payload.size());
        auto resp = nlohmann::json::parse(sv);
        if (resp.contains("cmd") && resp["cmd"].is_string()) {
            spdlog::debug("[SimConnectProvider] JSON response: {}", resp["cmd"].get<std::string>());
        }
    } catch (const nlohmann::json::parse_error&) {
        spdlog::warn("[SimConnectProvider] Unrecognized response payload ({} bytes)", payload.size());
    }
}

void SimConnectProvider::send_bytes(const uint8_t* data, size_t len) {
    if (!coordinator_ || !coordinator_->is_connected()) return;
    if (!coordinator_->send_bytes(data, len)) {
        spdlog::warn("[SimConnectProvider] send_bytes failed ({} bytes), packet dropped", len);
    }
}

ValType SimConnectProvider::val_type_for_signal(uint32_t signal_index) const {
    auto it = signal_to_val_type_.find(signal_index);
    return (it != signal_to_val_type_.end()) ? it->second : ValType::Float32;
}

// ==...== Heartbeat ==...==

void SimConnectProvider::maybe_send_ping(double current_time) {
    if (current_time - last_ping_sent_time_ < PING_INTERVAL_SEC) return;

    uint16_t ping_id = next_ping_id_++;
    size_t written = WireCodec::build_ping(
        send_buffer_.data(), send_buffer_.size(), ping_id);

    if (written > 0) {
        send_bytes(send_buffer_.data(), written);
        last_ping_sent_time_ = current_time;
        last_ping_id_ = ping_id;
        spdlog::trace("[SimConnectProvider] Ping sent (id={})", ping_id);
    }
}

void SimConnectProvider::handle_pong(const PacketHeader& pong_hdr) {
    if (pong_hdr.seq_id != last_ping_id_) {
        spdlog::debug("[SimConnectProvider] Stale pong (id={}, expected={})",
                      pong_hdr.seq_id, last_ping_id_);
        return;
    }

    last_pong_recv_time_ = current_time_;

    if (!connection_healthy_) {
        connection_healthy_ = true;
        spdlog::info("[SimConnectProvider] WASM bridge healthy — Pong received (id={})",
                     pong_hdr.seq_id);
    } else {
        spdlog::trace("[SimConnectProvider] Pong received (id={})", pong_hdr.seq_id);
    }
}
