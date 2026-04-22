#include "workspace_session_persist.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace {

const char* to_persisted_mode(BlueprintWindowMode mode) {
    switch (mode) {
        case BlueprintWindowMode::RootDocument:   return "root";
        case BlueprintWindowMode::EmbeddedScope:  return "embedded";
        case BlueprintWindowMode::ExternalReference: return "external";
    }
    return "root";
}

std::optional<BlueprintWindowMode> parse_persisted_mode(const nlohmann::json& j) {
    if (!j.is_string()) {
        return std::nullopt;
    }
    const std::string value = j.get<std::string>();
    if (value == "root")     return BlueprintWindowMode::RootDocument;
    if (value == "embedded") return BlueprintWindowMode::EmbeddedScope;
    if (value == "external") return BlueprintWindowMode::ExternalReference;
    return std::nullopt;
}

std::string blueprint_path_to_workspace_path(const char* blueprint_path) {
    std::string bp_str(blueprint_path);
    // Remove .blueprint extension if present
    if (bp_str.ends_with(".blueprint")) {
        bp_str = bp_str.substr(0, bp_str.length() - 10);
    }
    return bp_str + ".workspace.json";
}

} // namespace

// ============================================================================
// Save
// ============================================================================

bool save_workspace_session(
    const WorkspaceSession& ws,
    const char* blueprint_path)
{
    nlohmann::json j;
    j["format"] = "an24.workspace_session";
    j["version"] = 2;

    // Viewport
    nlohmann::json viewport;
    viewport["pan_x"] = ws.viewport_pan_x;
    viewport["pan_y"] = ws.viewport_pan_y;
    viewport["zoom"] = ws.viewport_zoom;
    viewport["grid_step"] = ws.grid_step;
    j["viewport"] = viewport;

    // Editor state — open subwindow scopes
    nlohmann::json editor;
    nlohmann::json open_windows = nlohmann::json::array();
    for (const auto& scope : ws.open_windows) {
        open_windows.push_back({
            {"mode", to_persisted_mode(scope.mode)},
            {"path_segments", scope.path_segments},
        });
    }
    editor["open_windows"] = std::move(open_windows);
    j["editor"] = editor;

    std::string ws_path = blueprint_path_to_workspace_path(blueprint_path);
    try {
        std::ofstream out(ws_path);
        if (!out.is_open()) {
            return false;
        }
        out << j.dump(2) << std::endl;
        return out.good();
    } catch (...) {
        return false;
    }
}

// ============================================================================
// Load
// ============================================================================

std::optional<WorkspaceSession> load_workspace_session(
    const char* blueprint_path)
{
    std::string ws_path = blueprint_path_to_workspace_path(blueprint_path);

    if (!fs::exists(ws_path)) {
        return std::nullopt;
    }

    try {
        std::ifstream in(ws_path);
        if (!in.is_open()) {
            return std::nullopt;
        }

        nlohmann::json j;
        in >> j;

        // Validate format marker and version
        if (!j.contains("format") || j["format"] != "an24.workspace_session") {
            return std::nullopt;
        }
        if (!j.contains("version") || j["version"] != 2) {
            return std::nullopt;
        }

        WorkspaceSession ws;

        // Viewport
        if (j.contains("viewport")) {
            const auto& vp = j["viewport"];
            if (vp.contains("pan_x"))    ws.viewport_pan_x = vp["pan_x"].get<float>();
            if (vp.contains("pan_y"))    ws.viewport_pan_y = vp["pan_y"].get<float>();
            if (vp.contains("zoom"))     ws.viewport_zoom  = vp["zoom"].get<float>();
            if (vp.contains("grid_step")) ws.grid_step     = vp["grid_step"].get<float>();
        }

        // Editor state — open subwindow scopes
        if (j.contains("editor")) {
            const auto& ed = j["editor"];
            if (ed.contains("open_windows") && ed["open_windows"].is_array()) {
                for (const auto& win_scope : ed["open_windows"]) {
                    if (!win_scope.is_object()
                        || !win_scope.contains("mode")
                        || !win_scope.contains("path_segments")) {
                        return std::nullopt;
                    }
                    auto mode = parse_persisted_mode(win_scope["mode"]);
                    if (!mode.has_value() || !win_scope["path_segments"].is_array()) {
                        return std::nullopt;
                    }
                    std::vector<std::string> path_segments;
                    for (const auto& segment : win_scope["path_segments"]) {
                        if (!segment.is_string()) {
                            return std::nullopt;
                        }
                        path_segments.push_back(segment.get<std::string>());
                    }
                    ws.open_windows.push_back(PersistedWindowScope{
                        *mode,
                        std::move(path_segments),
                    });
                }
            }
        }

        return ws;
    } catch (...) {
        return std::nullopt;
    }
}
