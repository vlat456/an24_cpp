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
  "version": "3.0",
  "id": "invalid_wire",
  "display_name": "Invalid Wire",
  "interface": [],
  "nodes": [
    {
      "id": "bat1",
      "type": "ElectricalSource",
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

TEST(PersistValidation, ValidateBlueprintForPersistRejectsUnknownType) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry parser_registry = load_type_registry("library/");

    bp2::Blueprint bp;
    bp2::Blueprint::Node n;
    n.id = interner.intern("n1");
    n.type = interner.intern("DefinitelyUnknownType");
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
    n.id = interner.intern("n1");
    n.type = interner.intern("Text");
    n.x = 0.0f;
    n.y = 0.0f;
    n.params[interner.intern("font_size")] = 16.0f;
    n.string_params["table"] = "0:0;1:2";
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
    // Unknown descriptor keys still persist through string_params.
    ASSERT_TRUE(node.contains("string_params"));
    EXPECT_TRUE(node["string_params"].contains("table"));
    EXPECT_EQ(node["string_params"]["table"].get<std::string>(), "0:0;1:2");

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
    n.id = interner.intern("bat1");
    n.type = interner.intern("ElectricalSource");
    n.x = 0.0f;
    n.y = 0.0f;
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
    a.id = interner.intern("a");
    a.type = interner.intern("ElectricalSource");
    bp = bp.with_node(std::move(a));

    bp2::Blueprint::Node b;
    b.id = interner.intern("b");
    b.type = interner.intern("Resistor");
    bp = bp.with_node(std::move(b));

    bp2::Blueprint::Wire w;
    w.id = interner.intern("wire_1");
    const auto root = arena.root();
    const auto a_node = arena.make_node(root, interner.intern("a"));
    const auto b_node = arena.make_node(root, interner.intern("b"));
    w.source = arena.make_port(a_node, interner.intern("v_out"));
    w.target = arena.make_port(b_node, interner.intern("v_in"));
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

TEST(PersistValidation, ValidatePersistAcceptsEmbeddedProxyNode) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry parser_registry = load_type_registry("library/");

    bp2::Blueprint bp;

    // A regular known node to ensure the basic path works.
    bp2::Blueprint::Node bat;
    bat.id = interner.intern("bat1");
    bat.type = interner.intern("ElectricalSource");
    bp = bp.with_node(std::move(bat));

    // An embedded blueprint proxy node with a user-given type name.
    bp2::Blueprint::Node proxy;
    proxy.id = interner.intern("exciter_inst");
    proxy.type = interner.intern("RN-180-Exciter");
    proxy.expandable = true;
    bp = bp.with_node(std::move(proxy));

    bp2::Blueprint::Nested nested;
    nested.id = interner.intern("exciter_inst");
    nested.embedded = true;
    nested.inline_def = std::make_unique<bp2::Blueprint>();
    *nested.inline_def = nested.inline_def->with_id(interner.intern("RN-180-Exciter"));
    bp = bp.with_nested(std::move(nested));

    std::string err;
    bool ok = validate_blueprint_for_persist(bp, interner, arena, parser_registry, &err);
    EXPECT_TRUE(ok) << "validate_blueprint_for_persist rejected embedded proxy: " << err;
}

TEST(PersistValidation, ValidatePersistStillRejectsNonProxyUnknownType) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry parser_registry = load_type_registry("library/");

    bp2::Blueprint bp;

    // A node with unknown type that is NOT an embedded proxy.
    bp2::Blueprint::Node n;
    n.id = interner.intern("n1");
    n.type = interner.intern("TotallyFakeType");
    n.expandable = false;
    bp = bp.with_node(std::move(n));

    std::string err;
    bool ok = validate_blueprint_for_persist(bp, interner, arena, parser_registry, &err);
    EXPECT_FALSE(ok);
    EXPECT_NE(err.find("unknown node type"), std::string::npos);
}

// ===========================================================================
// Regression: closed_circuit.blueprint must load through bp2 codec without
// wire domain mismatch errors, especially after InertiaNode port type changes.
// ===========================================================================

TEST(PersistValidation, ClosedCircuitBlueprintLoadsViaBp2Codec) {
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
    ASSERT_TRUE(bp.has_value())
        << "Failed to load closed_circuit.blueprint via bp2 codec: " << err.message;

    // Verify InertiaNode is present
    bool found_inertia = false;
    for (const auto& node : bp->nodes()) {
        if (interner.resolve(node.type) == "InertiaNode") {
            found_inertia = true;
            break;
        }
    }
    EXPECT_TRUE(found_inertia)
        << "InertiaNode not found in loaded blueprint";
}
