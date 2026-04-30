#pragma once

// =============================================================================
// WASM Bridge Protocol Header (LEGACY JSON — will be replaced by binary)
// =============================================================================
//
// Defines the JSON protocol between SimConnect (host) and WASM bridge (MSFS).
// Shared by both sides — no platform-specific types.

#include <cstdint>
#include <optional>
#include <string>

/// CommBus event names for WASM ↔ SimConnect communication.
namespace BridgeEvents {
    constexpr const char* Request  = "An24Bridge_Request";
    constexpr const char* Response = "An24Bridge_Response";
}

/// Request types sent from SimConnect → WASM bridge.
enum class BridgeCmd : uint8_t {
    GetVar,       // {"cmd":"get_var","type":"LVar","name":"X","id":N}
    SetVar,       // {"cmd":"set_var","type":"LVar","name":"X","value":V,"id":N}
    ListVars,     // {"cmd":"list_vars","type":"LVar","id":N}
    TriggerHEvent,// {"cmd":"trigger_hevent","name":"H:X","id":N}
    GetAVars,     // {"cmd":"get_avars","names":["X","Y"],"id":N}
};

/// Response types sent from WASM bridge → SimConnect.
enum class BridgeResponseCmd : uint8_t {
    VarValue,   // {"cmd":"var_value","id":N,"value":V,"valid":true}
    VarSet,     // {"cmd":"var_set","id":N,"ok":true}
    VarList,    // {"cmd":"var_list","id":N,"vars":["X","Y"]}
    HEventDone, // {"cmd":"hevent_done","id":N,"ok":true}
    AVarValues, // {"cmd":"avar_values","id":N,"values":{"X":V,"Y":W}}
};

/// Parse a bridge command string to enum.
/// Returns std::nullopt for unrecognized commands.
inline std::optional<BridgeCmd> parse_bridge_cmd(const std::string& cmd) {
    if (cmd == "get_var") return BridgeCmd::GetVar;
    if (cmd == "set_var") return BridgeCmd::SetVar;
    if (cmd == "list_vars") return BridgeCmd::ListVars;
    if (cmd == "trigger_hevent") return BridgeCmd::TriggerHEvent;
    if (cmd == "get_avars") return BridgeCmd::GetAVars;
    return std::nullopt;
}

/// Parse a bridge response command string to enum.
/// Returns std::nullopt for unrecognized commands.
inline std::optional<BridgeResponseCmd> parse_bridge_response_cmd(const std::string& cmd) {
    if (cmd == "var_value") return BridgeResponseCmd::VarValue;
    if (cmd == "var_set") return BridgeResponseCmd::VarSet;
    if (cmd == "var_list") return BridgeResponseCmd::VarList;
    if (cmd == "hevent_done") return BridgeResponseCmd::HEventDone;
    if (cmd == "avar_values") return BridgeResponseCmd::AVarValues;
    return std::nullopt;
}
