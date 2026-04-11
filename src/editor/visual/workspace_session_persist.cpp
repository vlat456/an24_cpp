#include "workspace_session_persist.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

static std::string blueprint_path_to_workspace_path(const char* blueprint_path) {
    std::string bp_str(blueprint_path);
    // Remove .blueprint extension if present
    if (bp_str.ends_with(".blueprint")) {
        bp_str = bp_str.substr(0, bp_str.length() - 10);
    }
    return bp_str + ".workspace.json";
}

bool save_workspace_session(
    const WorkspaceSession& ws,
    const char* blueprint_path)
{
    nlohmann::json j;
    j["format"] = "an24.workspace_session";
    j["version"] = 1;

    nlohmann::json viewport;
    viewport["pan_x"] = ws.viewport_pan_x;
    viewport["pan_y"] = ws.viewport_pan_y;
    viewport["zoom"] = ws.viewport_zoom;
    viewport["grid_step"] = ws.grid_step;
    j["viewport"] = viewport;

    nlohmann::json editor;
    editor["open_windows"] = ws.open_windows;
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

std::optional<WorkspaceSession> load_workspace_session(
    const char* blueprint_path)
{
    std::string ws_path = blueprint_path_to_workspace_path(blueprint_path);

    // Check if file exists
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

        // Validate format
        if (!j.contains("format") || j["format"] != "an24.workspace_session") {
            return std::nullopt;
        }
        if (!j.contains("version") || j["version"] != 1) {
            return std::nullopt;
        }

        WorkspaceSession ws;

        // Load viewport state if present
        if (j.contains("viewport")) {
            const auto& vp = j["viewport"];
            if (vp.contains("pan_x")) {
                ws.viewport_pan_x = vp["pan_x"].get<float>();
            }
            if (vp.contains("pan_y")) {
                ws.viewport_pan_y = vp["pan_y"].get<float>();
            }
            if (vp.contains("zoom")) {
                ws.viewport_zoom = vp["zoom"].get<float>();
            }
            if (vp.contains("grid_step")) {
                ws.grid_step = vp["grid_step"].get<float>();
            }
        }

        // Load editor state if present
        if (j.contains("editor")) {
            const auto& ed = j["editor"];
            if (ed.contains("open_windows") && ed["open_windows"].is_array()) {
                for (const auto& win_id : ed["open_windows"]) {
                    ws.open_windows.push_back(win_id.get<std::string>());
                }
            }
        }

        return ws;
    } catch (...) {
        return std::nullopt;
    }
}
