#include <gtest/gtest.h>

#include "editor/visual/persist.h"
#include "json_parser/json_parser.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/registry/type_registry.h"
#include <filesystem>
#include <fstream>

TEST(PersistValidation, RejectsInvalidWireEndpointOnLoad) {
    namespace fs = std::filesystem;

    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry parser_registry = load_type_registry("library/");

    fs::path tmp = fs::temp_directory_path() / "bp2_invalid_wire.blueprint";
    {
        std::ofstream out(tmp);
        out << R"({
  "version": "3.0",
  "id": "invalid_wire",
  "display_name": "Invalid Wire",
  "interface": [],
  "nodes": [
    {
      "id": "bat1",
      "type": "Battery",
      "ports": {
        "v_in": {"direction": "In", "type": 0},
        "v_out": {"direction": "Out", "type": 0}
      },
      "position": {"x": 0.0, "y": 0.0}
    }
  ],
  "wires": [
    {
      "id": "wire_1",
      "source": "/bat1:v_out",
      "target": "/ghost:v_in"
    }
  ],
  "nested": []
})";
    }

    auto loaded = load_blueprint_from_file_validated(tmp.c_str(), interner, arena, parser_registry);
    EXPECT_FALSE(loaded.has_value());

    std::error_code ec;
    fs::remove(tmp, ec);
}
