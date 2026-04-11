#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "editor/visual/persist.h"
#include "json_parser/json_parser.h"

namespace {

std::string find_closed_circuit_blueprint() {
    namespace fs = std::filesystem;

    const fs::path candidates[] = {
        "../../closed_circuit.blueprint",
        "../closed_circuit.blueprint",
        "closed_circuit.blueprint",
    };

    for (const fs::path& candidate : candidates) {
        if (fs::exists(candidate)) {
            return candidate.string();
        }
    }

    throw std::runtime_error("Could not find closed_circuit.blueprint");
}

} // namespace

TEST(StrictBlueprintPersistence, ClosedCircuitDocumentLoadsThroughHydratedPath) {
    const std::string path = find_closed_circuit_blueprint();

    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry registry = load_type_registry("library/");

    auto bp = load_hydrated_blueprint_from_file(path.c_str(), interner, arena, registry);
    ASSERT_TRUE(bp.has_value()) << "Failed to load strict closed_circuit.blueprint";
    EXPECT_FALSE(bp->nodes().empty());
    EXPECT_FALSE(bp->wires().empty());

    const auto* exciter = bp->find_blueprint_instance(interner.lookup("extract_inst_1"));
    ASSERT_TRUE(exciter != nullptr);
    EXPECT_TRUE(exciter->has_embedded_blueprint());

    const auto* lag = bp->find_blueprint_instance(interner.lookup("firstorderlag_1"));
    ASSERT_TRUE(lag != nullptr);
    EXPECT_TRUE(lag->has_embedded_blueprint());
}
