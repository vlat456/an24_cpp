#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "json_parser/json_parser.h"
#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/path/path.h"
#include "blueprint_v2/registry/type_registry.h"

namespace {

static bp2::Direction to_bp2_direction(PortDirection dir) {
    switch (dir) {
        case PortDirection::In: return bp2::Direction::Input;
        case PortDirection::Out: return bp2::Direction::Output;
        case PortDirection::InOut: return bp2::Direction::InOut;
    }
    return bp2::Direction::Output;
}

static Domain to_bp2_domain_from_port_type(PortType t, Domain fallback) {
    switch (t) {
        case PortType::V:
        case PortType::I:
        case PortType::Any:
            return Domain::Electrical;
        case PortType::Bool:
            return Domain::Logical;
        case PortType::RPM:
        case PortType::Position:
            return Domain::Mechanical;
        case PortType::Pressure:
            return Domain::Hydraulic;
        case PortType::Temperature:
            return Domain::Thermal;
    }
    return fallback;
}

static bp2::TypeRegistry build_bp2_registry(ui::StringInterner& interner) {
    bp2::TypeRegistry out;
    TypeRegistry parsed = load_type_registry("library/");

    for (const auto& [classname, def] : parsed.types) {
        std::vector<bp2::PortDescriptor> ports;
        ports.reserve(def.ports.size());

        Domain inferred_domain = Domain::Electrical;
        if (def.domains.has_value() && !def.domains->empty()) {
            inferred_domain = (*def.domains)[0];
        }

        for (const auto& [name, port] : def.ports) {
            bp2::PortDescriptor pd;
            pd.name = interner.intern(name);
            pd.domain = to_bp2_domain_from_port_type(port.type, inferred_domain);
            pd.direction = to_bp2_direction(port.direction);
            ports.push_back(pd);
        }

        const ui::InternedId type_id = interner.intern(classname);
        if (def.cpp_class) {
            out.register_component(type_id, bp2::Interface(std::move(ports)), def.description);
        } else {
            out.register_blueprint(type_id, bp2::Interface(std::move(ports)), def.description, nullptr);
        }
    }

    return out;
}

} // namespace

static std::string read_file(std::string const& path) {
    std::ifstream f(path);
    EXPECT_TRUE(f.is_open()) << path;
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

TEST(V3Migration, LibraryLoaderParsesConvertedLibraryAsV3Only) {
    TypeRegistry registry = load_type_registry("library");

    EXPECT_TRUE(registry.has("Battery"));
    EXPECT_TRUE(registry.has("Generator"));
}

TEST(V3Migration, CodecRejectsLegacyVersion2Json) {
    std::string old_schema = R"({
        "version": 2,
        "meta": {"name": "old_schema"},
        "nodes": {},
        "wires": []
    })";

    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry registry;
    bp2::DecodeError err;

    auto bp = bp2::BlueprintCodec::decode(old_schema, interner, arena, registry, &err);
    EXPECT_FALSE(bp.has_value());
    EXPECT_NE(err.message.find("Unsupported blueprint version"), std::string::npos);
}

TEST(V3Migration, GSCIsV3AndDecodes) {
    std::string content = read_file("/Users/vladimir/an24_cpp/GSC.blueprint");

    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry registry = build_bp2_registry(interner);
    bp2::DecodeError err;

    auto bp = bp2::BlueprintCodec::decode(content, interner, arena, registry, &err);
    ASSERT_TRUE(bp.has_value()) << err.message;
    EXPECT_FALSE(bp->nodes().empty());
    EXPECT_FALSE(bp->wires().empty());
}
