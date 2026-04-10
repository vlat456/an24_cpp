#include <gtest/gtest.h>

#include "editor/visual/persist.h"
#include "json_parser/json_parser.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/codec/blueprint_codec.h"
#include <filesystem>
#include <fstream>
#include <sstream>

TEST(PersistValidation, RejectsInvalidWireEndpointOnLoad) {
    namespace fs = std::filesystem;

    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry parser_registry = load_type_registry("library/");

    fs::path tmp = fs::temp_directory_path() / "bp2_invalid_wire.blueprint";
    {
        std::ofstream out(tmp);
        out << R"({
  "format": "an24.blueprint",
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
    TypeRegistry parser_registry = load_type_registry("library/");

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
    TypeRegistry parser_registry = load_type_registry("library/");

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("persist_typed"));
    bp = bp.with_display_name("Persist Typed");

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
    TypeRegistry parser_registry = load_type_registry("library/");

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("integrity_ok"));
    bp = bp.with_display_name("Integrity OK");

    bp2::Blueprint::Node n;
    n.semantic.id = interner.intern("bat1");
    n.semantic.type = interner.intern("ElectricalSource");
    n.layout.x = 0.0f;
    n.layout.y = 0.0f;
    bp = bp.with_node(std::move(n));

    std::string err;
    EXPECT_TRUE(validate_blueprint_integrity(bp, interner, arena, parser_registry, &err));
    EXPECT_TRUE(err.empty());
}

TEST(PersistValidation, WireDomainMismatchIsToleratedWithoutThrow) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry parser_registry = load_type_registry("library/");

    bp2::Blueprint bp;
    bp = bp.with_id(interner.intern("integrity_wire_domain_mismatch"));
    bp = bp.with_display_name("Wire Domain Mismatch");

    bp2::Blueprint::Node a;
    a.semantic.id = interner.intern("a");
    a.semantic.type = interner.intern("ElectricalSource");
    bp = bp.with_node(std::move(a));

    bp2::Blueprint::Node b;
    b.semantic.id = interner.intern("b");
    b.semantic.type = interner.intern("Resistor");
    bp = bp.with_node(std::move(b));

    bp2::Blueprint::Wire w;
    w.id = interner.intern("wire_1");
    w.source = bp2::WireEndpoint{interner.intern("a"), interner.intern("v_out")};
    w.target = bp2::WireEndpoint{interner.intern("b"), interner.intern("v_in")};
    w.domain = Domain::Logical; // intentional mismatch for electrical endpoints
    bp = bp.with_wire(std::move(w));

    std::string err;
    EXPECT_NO_THROW({
        const bool ok = validate_blueprint_integrity(bp, interner, arena, parser_registry, &err);
        EXPECT_TRUE(ok);
    });
    EXPECT_TRUE(err.empty());
}

// ===========================================================================
// Regression: validate_blueprint_for_persist must accept embedded blueprint
// proxy nodes whose type name is not in the parser TypeRegistry.
// ===========================================================================



// ===========================================================================
// Strict v1 regression: legacy blueprints are intentionally rejected by the
// canonical codec. closed_circuit.blueprint is still a legacy fixture.
// ===========================================================================

TEST(PersistValidation, ClosedCircuitLegacyBlueprintIsRejectedByStrictCodec) {
    // Try multiple paths to find closed_circuit.blueprint
    const char* candidates[] = {
        "../../closed_circuit.blueprint",
        "../closed_circuit.blueprint",
        "closed_circuit.blueprint",
    };
    std::string bp_path;
    for (const char* c : candidates) {
        if (std::filesystem::exists(c)) {
            bp_path = c;
            break;
        }
    }
    if (bp_path.empty()) {
        GTEST_SKIP() << "closed_circuit.blueprint not found; skipping regression test";
    }

    // Read file content
    std::ifstream file(bp_path);
    ASSERT_TRUE(file.is_open()) << "Could not open: " << bp_path;
    std::stringstream buf;
    buf << file.rdbuf();
    std::string content = buf.str();

    // Use a fresh interner and parser registry from library/.
    ui::StringInterner interner;
    bp2::PathArena arena(interner);

    TypeRegistry parser_registry = load_type_registry("library/");

    bp2::DecodeError err;
    auto bp = bp2::BlueprintCodec::decode(content, interner, arena, parser_registry, &err);
    ASSERT_FALSE(bp.has_value());
    EXPECT_NE(err.message.find("Unsupported blueprint format"), std::string::npos);
}
