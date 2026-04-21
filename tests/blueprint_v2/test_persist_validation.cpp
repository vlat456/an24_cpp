#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "editor/visual/persist.h"
#include "io/json/component_registry_json_loader.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/interface/type_definition_interface.h"
#include <filesystem>
#include <fstream>
#include <sstream>

TEST(PersistValidation, RejectsInvalidWireEndpointOnLoad) {
    namespace fs = std::filesystem;

    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    ComponentRegistry parser_registry = load_component_registry("library/");

    fs::path tmp = fs::temp_directory_path() / "bp2_invalid_wire.blueprint";
    {
        std::ofstream out(tmp);
        out << R"({
  "format": "blueprint",
  "version": 1,
  "blueprint_id": "invalid_wire",
  "name": "Invalid Wire",
  "interface": [],
  "nodes": [
    {
      "id": "bat1",
      "kind": "component",
      "component": "ElectricalSource",
      "layout": {"x": 0.0, "y": 0.0}
    }
  ],
  "wires": [
    {
      "id": "wire_1",
      "from": {"node": "bat1", "port": "v_out"},
      "to": {"node": "ghost", "port": "v_in"}
    }
  ]
})";
    }

    auto loaded = load_blueprint_from_file_validated(tmp.c_str(), interner, arena, parser_registry);
    EXPECT_FALSE(loaded.has_value());

    std::error_code ec;
    fs::remove(tmp, ec);
}

TEST(PersistValidation, ValidateBlueprintForPersistRejectsUnknownType) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    ComponentRegistry parser_registry = load_component_registry("library/");

    bp2::Blueprint bp;
    bp2::Blueprint::Node n;
    n.semantic.id = interner.intern("n1");
    n.semantic.type = interner.intern("DefinitelyUnknownType");
    bp = bp.with_node(std::move(n));

    std::string err;
    bool ok = validate_blueprint_for_persist(bp, interner, arena, parser_registry, &err);
    EXPECT_FALSE(ok);
    EXPECT_NE(err.find("unknown node type"), std::string::npos);
}

TEST(PersistValidation, SaveUsesTypedParamNormalizationWhenRegistryAvailable) {
    namespace fs = std::filesystem;

    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    ComponentRegistry parser_registry = load_component_registry("library/");

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("persist_typed"));
    bp = bp.with_name("Persist Typed");

    bp2::Blueprint::Node n;
    n.semantic.id = interner.intern("n1");
     n.semantic.type = interner.intern("Text");
     n.layout.x = 0.0f;
     n.layout.y = 0.0f;
     n.semantic.params[interner.intern("font_size")] = 16.0f;
     n.semantic.string_params["table"] = "0:0;1:2";
    bp = bp.with_node(std::move(n));

    fs::path tmp = fs::temp_directory_path() / "bp2_save_typed.blueprint";
    ASSERT_TRUE(save_blueprint_to_file(bp, interner, arena, parser_registry, tmp.c_str()));

    std::ifstream in(tmp);
    ASSERT_TRUE(in.is_open());
    nlohmann::json j;
    in >> j;

    ASSERT_EQ(j["nodes"].size(), 1u);
    const auto& node = j["nodes"][0];
    ASSERT_TRUE(node.contains("params"));
    EXPECT_TRUE(node["params"].contains("font_size"));
    EXPECT_TRUE(node["params"]["font_size"].is_number());
    EXPECT_TRUE(node["params"].contains("table"));
    EXPECT_EQ(node["params"]["table"].get<std::string>(), "0:0;1:2");

    std::error_code ec;
    fs::remove(tmp, ec);
}

TEST(PersistValidation, ValidateBlueprintIntegrityPassesForValidBlueprint) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    ComponentRegistry parser_registry = load_component_registry("library/");

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("integrity_ok"));
    bp = bp.with_name("Integrity OK");

    bp2::Blueprint::Node n;
    n.semantic.id = interner.intern("bat1");
    n.semantic.type = interner.intern("ElectricalSource");
    n.layout.x = 0.0f;
    n.layout.y = 0.0f;
    // Populate interface from registry
    const auto* def = parser_registry.get("ElectricalSource");
    if (def) {
        n.content = bp2::Blueprint::Node::ComponentData{
            bp2::interface_from_type_definition(*def, interner)
        };
    }
    bp = bp.with_node(std::move(n));

    std::string err;
    EXPECT_TRUE(validate_blueprint_integrity(bp, interner, arena, parser_registry, &err));
    EXPECT_TRUE(err.empty());
}

TEST(PersistValidation, WireDomainMismatchFailsValidation) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    ComponentRegistry parser_registry = load_component_registry("library/");

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("integrity_wire_domain_mismatch"));
    bp = bp.with_name("Wire Domain Mismatch");

    bp2::Blueprint::Node a;
    a.semantic.id = interner.intern("a");
    a.semantic.type = interner.intern("ElectricalSource");
    const auto* def_a = parser_registry.get("ElectricalSource");
    if (def_a) {
        a.content = bp2::Blueprint::Node::ComponentData{
            bp2::interface_from_type_definition(*def_a, interner)
        };
    }
    bp = bp.with_node(std::move(a));

    bp2::Blueprint::Node b;
    b.semantic.id = interner.intern("b");
    b.semantic.type = interner.intern("Resistor");
    const auto* def_b = parser_registry.get("Resistor");
    if (def_b) {
        b.content = bp2::Blueprint::Node::ComponentData{
            bp2::interface_from_type_definition(*def_b, interner)
        };
    }
    bp = bp.with_node(std::move(b));

    bp2::Blueprint::Wire w;
    w.id = interner.intern("wire_1");
    w.source = bp2::WireEndpoint{interner.intern("a"), interner.intern("v_out")};
    w.target = bp2::WireEndpoint{interner.intern("b"), interner.intern("v_in")};
    w.domain = Domain::Logical; // intentional mismatch for electrical endpoints
    bp = bp.with_wire(std::move(w));

    std::string err;
    // Issue #88: Strict v1 validation must reject wire domain mismatches
    const bool ok = validate_blueprint_integrity(bp, interner, arena, parser_registry, &err);
    EXPECT_FALSE(ok);
    EXPECT_NE(err.find("domain"), std::string::npos);
}

// ===========================================================================
// Regression: validate_blueprint_for_persist must accept embedded blueprint
// proxy nodes whose type name is not in the parser ComponentRegistry.
// ===========================================================================



// ===========================================================================
// Strict v1 regression: legacy blueprints are intentionally rejected by the
// canonical codec. Old schematic files have been deleted from the repository.
// ===========================================================================
