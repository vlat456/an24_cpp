/// One-shot tool to convert all library/*.json files to v2 .blueprint format.
///
/// Usage: convert_library [library_dir]
/// Default: library_dir = "library/"
///
/// For each .json file in the library directory:
/// 1. Parse as v1 TypeDefinition (using parse_type_definition)
/// 2. Convert to BlueprintV2 (using type_definition_to_v2)
/// 3. Serialize to JSON (using serialize_blueprint_v2)
/// 4. Write to .blueprint file (same directory, same base name)
///
/// Does NOT delete the .json files — that's a manual step.

#include "json_parser.h"
#include "blueprint_v2.h"
#include "convert.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace v2;

using json = nlohmann::json;

int main(int argc, char* argv[]) {
    std::string library_dir = argc > 1 ? argv[1] : "library/";
    std::filesystem::path library_path(library_dir);

    if (!std::filesystem::exists(library_path)) {
        std::cerr << "Error: library directory '" << library_dir << "' does not exist\n";
        return 1;
    }

    size_t converted = 0;
    size_t skipped = 0;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(library_path)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }

        // Skip the root blueprint.json (editor save, not a type definition)
        if (entry.path().filename() == "blueprint.json") {
            std::cout << "SKIP (editor save): " << entry.path().string() << "\n";
            skipped++;
            continue;
        }

        try {
            // Read and parse v1 JSON
            std::ifstream file(entry.path());
            json j;
            file >> j;

            TypeDefinition td = parse_type_definition(j);

            // Convert to v2
            BlueprintV2 bp = type_definition_to_v2(td);

            // Serialize
            std::string v2_json = serialize_blueprint_v2(bp);

            // Write .blueprint file
            auto blueprint_path = entry.path();
            blueprint_path.replace_extension(".blueprint");

            std::ofstream out(blueprint_path);
            out << v2_json;
            out.close();

            std::cout << "OK: " << entry.path().filename().string()
                      << " -> " << blueprint_path.filename().string() << "\n";
            converted++;
        }
        catch (const std::exception& e) {
            std::cerr << "ERROR: " << entry.path().string() << ": " << e.what() << "\n";
            skipped++;
        }
    }

    std::cout << "\nDone: " << converted << " converted, " << skipped << " skipped\n";
    return 0;
}
