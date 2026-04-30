#include "simconnect_bridge.h"
#include "wasm/bridge_protocol.h"
#include "core/solvers/jit/components/all.h"
#include "core/strings/interned_id.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

SimConnectBridge::SimConnectBridge()
    : client_(create_simconnect_client()),
      send_buffer_(MAX_PACKET_SIZE) {
}

SimConnectBridge::~SimConnectBridge() {
    disconnect();
}

// ==...== Setup ==...==

void SimConnectBridge::build_mappings(const JitBuildInput& input, JIT_Simulator& sim) {
    input_mappings_.clear();
    output_mappings_.clear();
    input_buffer_.clear();
    id_to_signal_.clear();
    intern_table_.clear();
    output_shadow_.clear();

    for (const auto& device : input.devices) {
        if (device.kind != ComponentKind::SimVarInput &&
            device.kind != ComponentKind::SimVarOutput) {
            continue;
        }

        auto mapping = parse_mapping(device, input);
        if (!mapping) continue;

        // Intern the variable name — consistent ID across sessions
        mapping->intern_id = intern_table_.intern(mapping->var_type, mapping->var_name);
        id_to_signal_[mapping->intern_id] = mapping->signal_index;

        if (mapping->direction == SimVarDirection::Input) {
            input_buffer_[mapping->signal_index] = mapping->default_value;
            input_mappings_.push_back(std::move(*mapping));
        } else {
            output_shadow_[mapping->signal_index] = WireValue(0.0f);
            output_mappings_.push_back(std::move(*mapping));
        }
    }

    // Pre-allocate override list
    overrides_.reserve(input_mappings_.size());

    spdlog::info("[SimConnectBridge] Mapped {} inputs, {} outputs (V2 delta protocol)",
                 input_mappings_.size(), output_mappings_.size());
}

std::optional<SimVarMapping> SimConnectBridge::parse_mapping(
    const SolverDevice& device, const JitBuildInput& input) {

    SimVarMapping mapping;

    // Extract var_name (required)
    auto it = device.params.find("var_name");
    if (it == device.params.end() || it->second.empty()) {
        spdlog::warn("[SimConnectBridge] Device '{}' has empty var_name, skipping", device.name);
        return std::nullopt;
    }
    mapping.var_name = it->second;

    // Parse var_type to enum (defaults to AVar)
    it = device.params.find("var_type");
    std::string var_type_str = (it != device.params.end()) ? it->second : "AVar";
    if (!parse_var_type(var_type_str, mapping.var_type)) {
        spdlog::warn("[SimConnectBridge] Device '{}' has unknown var_type '{}', defaulting to AVar",
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

    // Parse tier (defaults to default_tier for the var type)
    it = device.params.find("tier");
    if (it != device.params.end()) {
        try { mapping.tier = static_cast<uint8_t>(std::stoi(it->second)); } catch (...) {
            mapping.tier = default_tier(mapping.var_type);
        }
    } else {
        mapping.tier = default_tier(mapping.var_type);
    }

    // Parse epsilon (defaults to default_epsilon for the var type)
    it = device.params.find("epsilon");
    if (it != device.params.end()) {
        try { mapping.epsilon = std::stof(it->second); } catch (...) {
            mapping.epsilon = default_epsilon(mapping.var_type);
        }
    } else {
        mapping.epsilon = default_epsilon(mapping.var_type);
    }

    // Determine direction from component kind
    mapping.direction = (device.kind == ComponentKind::SimVarInput)
        ? SimVarDirection::Input : SimVarDirection::Output;

    // Resolve signal key and index from the port mapping
    std::string port_name = (mapping.direction == SimVarDirection::Input) ? "out" : "in";
    std::string key_str = device.name + "." + port_name;
    mapping.signal_key = input.signal_key_interner.lookup(key_str);
    if (mapping.signal_key.empty()) {
        spdlog::warn("[SimConnectBridge] Device '{}' port '{}' key not interned, skipping",
                     device.name, port_name);
        return std::nullopt;
    }

    auto signal_it = input.port_to_signal.find(mapping.signal_key);
    if (signal_it == input.port_to_signal.end()) {
        spdlog::warn("[SimConnectBridge] Device '{}' port '{}' not mapped to signal, skipping",
                     device.name, port_name);
        return std::nullopt;
    }
    mapping.signal_index = signal_it->second;

    return mapping;
}

// ==...== Connection Lifecycle ==...==

bool SimConnectBridge::connect() {
    if (!client_->connect()) {
        spdlog::error("[SimConnectBridge] Failed to connect to SimConnect");
        return false;
    }

    // Register response callback
    client_->set_response_callback(
        [this](const std::string& payload) { on_response(payload); });

    spdlog::info("[SimConnectBridge] Connected to SimConnect");
    return true;
}

void SimConnectBridge::disconnect() {
    if (client_) {
        client_->disconnect();
    }
}

bool SimConnectBridge::is_connected() const {
    return client_ && client_->is_connected();
}

void SimConnectBridge::poll(double elapsed_time) {
    if (!client_ || !client_->is_connected()) return;

    current_time_ = elapsed_time;
    client_->poll(elapsed_time);

    // Heartbeat: send Ping if interval elapsed
    maybe_send_ping(current_time_);

    // Health check: no Pong for too long → mark unhealthy
    if (connection_healthy_ &&
        current_time_ - last_pong_recv_time_ > PONG_TIMEOUT_SEC &&
        last_pong_recv_time_ > 0.0) {
        connection_healthy_ = false;
        spdlog::warn("[SimConnectBridge] WASM bridge unhealthy — no Pong for {:.1f}s",
                     current_time_ - last_pong_recv_time_);
    }
}

// ==...== Simulation Integration ==...==

void SimConnectBridge::request_inputs() {
    if (input_mappings_.empty() || !is_connected()) return;

    // Compute tier mask for this frame
    uint16_t tier_mask = tier_mask_for_frame(frame_counter_);

    // Build 8-byte DeltaRead — WASM knows all interned IDs from registration
    size_t written = codec_.build_delta_read(
        send_buffer_.data(), send_buffer_.size(),
        tier_mask, host_epoch_);

    frame_counter_++;
    host_epoch_++;

    if (written > 0) {
        send_bytes(send_buffer_.data(), written);
    }
}

void SimConnectBridge::inject_inputs(JIT_Simulator& sim) {
    if (input_mappings_.empty()) return;

    overrides_.clear();
    for (const auto& m : input_mappings_) {
        auto it = input_buffer_.find(m.signal_index);
        if (it != input_buffer_.end()) {
            overrides_.emplace_back(m.signal_key, it->second);
        }
    }

    if (!overrides_.empty()) {
        sim.apply_typed_overrides(overrides_);
    }
}

void SimConnectBridge::extract_outputs(const JIT_Simulator& sim) {
    if (output_mappings_.empty() || !is_connected()) return;

    // Build changed records — epsilon-based change detection for ALL outputs
    output_records_.clear();

    for (const auto& m : output_mappings_) {
        float value = sim.get_signal_value(m.signal_key);
        WireValue current(value);

        auto shadow_it = output_shadow_.find(m.signal_index);
        if (shadow_it != output_shadow_.end()) {
            if (!value_changed(current, shadow_it->second, m.epsilon, ValType::Float32)) {
                continue;  // No significant change — skip
            }
        }

        // Changed: include in delta write and update shadow
        output_shadow_[m.signal_index] = current;
        VarRecord rec;
        rec.var_type = (m.mode == SimVarMode::Event) ? VarType::HEvent : m.var_type;
        rec.val_type = ValType::Float32;
        rec.name_id  = m.intern_id;
        rec.value    = current;
        output_records_.push_back(rec);
    }

    // Only send if there are changed records
    if (!output_records_.empty()) {
        size_t written = codec_.build_delta_write(
            send_buffer_.data(), send_buffer_.size(),
            output_records_, host_epoch_);

        if (written > 0) {
            send_bytes(send_buffer_.data(), written);
        }
    }
}

// ==...== Private ==...==

void SimConnectBridge::on_response(const std::string& payload) {
    // Detect binary packet by checking for valid header magic
    if (payload.size() >= sizeof(PacketHeader)) {
        const auto* data = reinterpret_cast<const uint8_t*>(payload.data());
        if (is_valid_header(data, payload.size())) {
            PacketHeader hdr;
            std::memcpy(&hdr, data, sizeof(PacketHeader));

            // Ping/Pong bypass the epoch check — they use independent seq_id
            // for ping/pong correlation, not frame-epoch ordering.
            auto cmd = header_cmd(hdr);
            if (cmd != Cmd::Pong) {
                uint16_t epoch_gap = host_epoch_ - hdr.seq_id;
                if (epoch_gap > 100) {
                    spdlog::debug("[SimConnectBridge] Stale response epoch={} (current={}, gap={}), ignoring",
                                  hdr.seq_id, host_epoch_, epoch_gap);
                    return;
                }
            }

            switch (cmd) {
                case Cmd::DeltaUpdate:
                case Cmd::FullSync:
                    handle_read_response(data, payload.size());
                    break;
                case Cmd::WriteAck:
                    spdlog::debug("[SimConnectBridge] Write ack: epoch={}", hdr.seq_id);
                    break;
                case Cmd::Pong:
                    handle_pong(hdr);
                    break;
                default:
                    spdlog::debug("[SimConnectBridge] Unexpected binary cmd: 0x{:02x}", hdr.cmd);
                    break;
            }
            return;
        }
    }

    // Fallback: JSON response (control channel)
    try {
        auto resp = nlohmann::json::parse(payload);
        if (resp.contains("cmd") && resp["cmd"].is_string()) {
            spdlog::debug("[SimConnectBridge] JSON response: {}", resp["cmd"].get<std::string>());
        }
    } catch (const nlohmann::json::parse_error&) {
        spdlog::warn("[SimConnectBridge] Unrecognized response payload ({} bytes)", payload.size());
    }
}

void SimConnectBridge::handle_read_response(const uint8_t* data, size_t len) {
    auto result = codec_.parse(data, len);
    if (!result.header || result.records.empty()) return;

    // Both DeltaUpdate and FullSync apply records the same way:
    //   DeltaUpdate: only changed records (sparse)
    //   FullSync:    all records (complete — host keeps last known for any missing)
    // This avoids value blips during sync and is safe because FullSync is periodic.
    for (const auto& rec : result.records) {
        auto it = id_to_signal_.find(rec.name_id);
        if (it != id_to_signal_.end()) {
            input_buffer_[it->second] = rec.value.f32;
        }
    }
}

void SimConnectBridge::send_bytes(const uint8_t* data, size_t len) {
    if (!client_ || !client_->is_connected()) return;
    // Wrap binary data as string — CommBus is a raw byte pipe
    client_->send_request(std::string(reinterpret_cast<const char*>(data), len));
}

void SimConnectBridge::send_json(const std::string& json_payload) {
    if (!client_ || !client_->is_connected()) return;
    client_->send_request(json_payload);
}

// ==...== Heartbeat ==...==

void SimConnectBridge::maybe_send_ping(double current_time) {
    if (current_time - last_ping_sent_time_ < PING_INTERVAL_SEC) return;

    size_t written = codec_.build_ping(
        send_buffer_.data(), send_buffer_.size(), next_ping_id_++);

    if (written > 0) {
        send_bytes(send_buffer_.data(), written);
        last_ping_sent_time_ = current_time;
        spdlog::trace("[SimConnectBridge] Ping sent (id={})", next_ping_id_ - 1);
    }
}

void SimConnectBridge::handle_pong(const PacketHeader& pong_hdr) {
    last_pong_recv_time_ = current_time_;

    if (!connection_healthy_) {
        connection_healthy_ = true;
        spdlog::info("[SimConnectBridge] WASM bridge healthy — Pong received (id={})",
                     pong_hdr.seq_id);
    } else {
        spdlog::trace("[SimConnectBridge] Pong received (id={})", pong_hdr.seq_id);
    }
}
