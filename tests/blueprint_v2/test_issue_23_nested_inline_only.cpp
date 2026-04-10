#include <gtest/gtest.h>

#include "blueprint_v2/blueprint/blueprint.h"
#include "json_parser/json_parser.h"
#include "editor/visual/persist.h"

#include <filesystem>

namespace fs = std::filesystem;

static std::string resolve_library_blueprint_path(const std::string& relative) {
    const std::vector<fs::path> candidates = {
        fs::path("library") / relative,
        fs::path("../library") / relative,
        fs::path("../../library") / relative,
        fs::path("../../../library") / relative,
    };
    for (const auto& p : candidates) {
        if (fs::exists(p)) {
            return p.string();
        }
    }
    return (fs::path("library") / relative).string();
}



