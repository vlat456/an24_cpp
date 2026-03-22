#include "codegen/codegen.h"
#include "json_parser/json_parser.h"
#include "jit_solver/jit_solver.h"
#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/path/path.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>

namespace {

std::optional<std::string> endpoint_to_legacy_port(bp2::Path const& p,
                                                   bp2::PathArena const& arena,
                                                   ui::StringInterner const& interner) {
    if (p.kind() != bp2::PathKind::Port) {
        return std::nullopt;
    }
    bp2::Path parent = arena.parent(p);
    if (parent.kind() != bp2::PathKind::Node && parent.kind() != bp2::PathKind::Nested) {
        return std::nullopt;
    }
    std::string node = std::string(interner.resolve(parent.segment()));
    std::string port = std::string(interner.resolve(p.segment()));
    return node + "." + port;
}

ParserContext parse_v3_to_parser_context(std::string const& content,
                                         TypeRegistry const& registry) {
    ParserContext ctx;

    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::TypeRegistry bp2_registry;
    auto bp_opt = bp2::BlueprintCodec::decode(content, interner, arena, bp2_registry);
    if (!bp_opt.has_value()) {
        return ctx;
    }
    bp2::Blueprint const& bp = *bp_opt;

    for (auto const& n : bp.nodes()) {
        DeviceInstance dev;
        dev.name = std::string(interner.resolve(n.id));
        dev.classname = std::string(interner.resolve(n.type));
        for (auto const& [k, v] : n.params) {
            dev.params[std::string(interner.resolve(k))] = std::to_string(v);
        }

        if (auto td = registry.get(dev.classname)) {
            dev = merge_device_instance(dev, *td);
        }
        ctx.devices.push_back(std::move(dev));
    }

    for (auto const& n : bp.nested()) {
        DeviceInstance dev;
        dev.name = std::string(interner.resolve(n.id));
        dev.classname = std::string(interner.resolve(n.blueprint_id));
        if (auto td = registry.get(dev.classname)) {
            dev = merge_device_instance(dev, *td);
        }
        ctx.devices.push_back(std::move(dev));
    }

    for (auto const& w : bp.wires()) {
        auto from = endpoint_to_legacy_port(w.source, arena, interner);
        auto to = endpoint_to_legacy_port(w.target, arena, interner);
        if (!from || !to) {
            continue;
        }
        ctx.connections.push_back(Connection{*from, *to});
    }

    return ctx;
}

} // namespace



int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <json_file> <output_dir>\n";
        return 1;
    }

    std::string json_file = argv[1];
    std::string out_dir = argv[2];

    // Generate port registry from library/
    auto registry = load_type_registry("library");
    std::string port_registry_path = "src/jit_solver/components/port_registry.h";
    CodeGen::generate_port_registry(registry, port_registry_path);

    // Load JSON
    std::ifstream file(json_file);
    if (!file.is_open()) {
        std::cerr << "Failed to open: " << json_file << "\n";
        return 1;
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    ParserContext ctx;
    try {
        auto j = nlohmann::json::parse(content);
        if (j.contains("version") && j["version"].is_string() && j["version"].get<std::string>() == "3.0") {
            ctx = parse_v3_to_parser_context(content, registry);
        } else {
            ctx = parse_json(content);
        }
    } catch (...) {
        ctx = parse_json(content);
    }
    std::cout << "Parsed: " << ctx.devices.size() << " devices\n";

    // Build systems to get port_to_signal
    std::vector<Connection> conn;
    for (const auto& c : ctx.connections) {
        conn.push_back({c.from, c.to});
    }

    // Convert to pair format for build_systems_dev
    std::vector<std::pair<std::string, std::string>> conn_pairs;
    for (const auto& c : ctx.connections) {
        conn_pairs.push_back({c.from, c.to});
    }

    auto result = build_systems_dev(ctx.devices, conn_pairs);

    std::cout << "Signals: " << result.signal_count << "\n";
    std::cout << "Fixed: " << result.fixed_signals.size() << "\n";

    // Generate code
    CodeGen::write_files(
        out_dir,
        json_file,
        ctx.devices,
        conn,  // use Connection vector
        result.port_to_signal,
        result.signal_count
    );

    std::cout << "Generated files in: " << out_dir << "\n";

    return 0;
}
