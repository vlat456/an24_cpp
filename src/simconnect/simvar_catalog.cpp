#include "simvar_catalog.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

SimVarCatalog* SimVarCatalog::instance_ = nullptr;

SimVarCatalog& SimVarCatalog::instance() {
    if (!instance_) {
        instance_ = new SimVarCatalog();
    }
    return *instance_;
}

void SimVarCatalog::reset_instance() {
    delete instance_;
    instance_ = nullptr;
}

static std::string to_lower(std::string_view s) {
    std::string result;
    result.reserve(s.size());
    for (unsigned char c : s) {
        result.push_back(static_cast<char>(std::tolower(c)));
    }
    return result;
}

bool SimVarCatalog::load_bundled_from_string(const std::string& json_str) {
    try {
        auto j = nlohmann::json::parse(json_str);

        auto version_it = j.find("version");
        if (version_it == j.end() || !version_it->is_number_integer()) {
            return false;
        }
        if (*version_it != 1) {
            return false;
        }

        auto avars_it = j.find("avars");
        if (avars_it == j.end() || !avars_it->is_array()) {
            return false;
        }

        for (const auto& item : *avars_it) {
            Entry e;
            e.name = item.value("name", std::string());
            if (e.name.empty()) continue;

            e.var_type = VarType::AVar;
            std::string const vt = item.value("val_type", "Float32");
            if (!parse_val_type(vt, e.val_type)) {
                e.val_type = ValType::Float32;
            }

            e.unit = item.value("unit", std::string());
            e.description = item.value("description", std::string());
            avars_.push_back(std::move(e));
        }

        return true;
    } catch (const nlohmann::json::parse_error&) {
        return false;
    }
}

bool SimVarCatalog::load_bundled(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return load_bundled_from_string(content);
}

void SimVarCatalog::add_lvar(std::string_view name, ValType val_type) {
    Entry e;
    e.name = std::string(name);
    e.var_type = VarType::LVar;
    e.val_type = val_type;
    lvars_.push_back(std::move(e));
}

void SimVarCatalog::add_lvars(const std::vector<Entry>& lvars) {
    lvars_.reserve(lvars_.size() + lvars.size());
    for (const auto& e : lvars) {
        lvars_.push_back(e);
    }
}

void SimVarCatalog::clear_lvars() {
    lvars_.clear();
    lvars_.shrink_to_fit();
}

std::vector<SimVarCatalog::Entry> SimVarCatalog::find(
    std::string_view filter, std::optional<VarType> var_type) const {

    std::string const filter_lower = to_lower(filter);
    bool const filter_empty = filter.empty();

    std::vector<Entry> results;

    auto match_entry = [&](const Entry& e) {
        if (var_type.has_value() && e.var_type != *var_type) return;
        if (filter_empty) {
            results.push_back(e);
            return;
        }
        std::string const name_lower = to_lower(e.name);
        if (name_lower.find(filter_lower) != std::string::npos) {
            results.push_back(e);
        }
    };

    for (const auto& e : avars_) match_entry(e);
    for (const auto& e : lvars_) match_entry(e);

    return results;
}
