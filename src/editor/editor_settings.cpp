#include "editor_settings.h"
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

void EditorSettings::loadFrom(const std::string& filepath) {
    recent_files_.clear();
    open_tabs_.clear();
    active_tab_.clear();

    {
        std::error_code ec;
        if (!std::filesystem::exists(filepath, ec)) return;
    }

    std::ifstream f(filepath);
    if (!f.is_open()) return;

    try {
        json j;
        f >> j;

        auto has_embedded_nul = [](const std::string& p) -> bool {
            return p.find('\0') != std::string::npos;
        };

        auto path_exists = [](const std::string& p) -> bool {
            std::error_code ec;
            return std::filesystem::exists(p, ec);
        };

        if (j.contains("recentFiles") && j["recentFiles"].is_array()) {
            for (const auto& p : j["recentFiles"]) {
                if (!p.is_string()) continue;
                std::string path = p.get<std::string>();
                if (!has_embedded_nul(path) && path_exists(path)) {
                    recent_files_.push_back(path);
                }
            }
        }

        if (j.contains("openTabs") && j["openTabs"].is_array()) {
            for (const auto& p : j["openTabs"]) {
                if (!p.is_string()) continue;
                std::string path = p.get<std::string>();
                if (!has_embedded_nul(path) && path_exists(path)) {
                    open_tabs_.push_back(path);
                }
            }
        }

        if (j.contains("activeTab") && j["activeTab"].is_string()) {
            std::string path = j["activeTab"].get<std::string>();
            if (!has_embedded_nul(path)) {
                active_tab_ = std::move(path);
            }
        }
    } catch (const std::exception&) {
        // Invalid JSON, filesystem errors, or unexpected structure — reset and ignore
        recent_files_.clear();
        open_tabs_.clear();
        active_tab_.clear();
    }
}

void EditorSettings::saveTo(const std::string& filepath) const {
    try {
        json j;

        j["recentFiles"] = recent_files_;
        j["openTabs"] = open_tabs_;
        if (!active_tab_.empty()) {
            j["activeTab"] = active_tab_;
        }

        std::string serialized = j.dump(2, ' ', false, json::error_handler_t::replace);

        std::ofstream f(filepath);
        if (f.is_open()) {
            f << serialized << "\n";
        }
    } catch (const std::exception& e) {
        // Do not crash the editor if settings cannot be saved
        fprintf(stderr, "[EditorSettings] Failed to save settings to %s: %s\n",
                filepath.c_str(), e.what());
    }
}
