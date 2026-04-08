#include "core/solvers/aot/codegen.h"
#include "json_parser/json_parser.h"
#include "core/solvers/jit/jit_solver.h"
#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/path/path.h"
#include <iostream>
#include <fstream>
#include <set>
#include <unordered_set>

namespace {

std::optional<std::string> endpoint_to_port_string(bp2::Path const& p,
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

ParserContext parse_blueprint_v3(std::string const& content,
                                 TypeRegistry const& registry) {
    ParserContext ctx;

    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    auto bp_opt = bp2::BlueprintCodec::decode(content, interner, arena, registry);
    if (!bp_opt.has_value()) {
        return ctx;
    }

    bp2::Blueprint const& bp = *bp_opt;

    TypeDefinition root_def;
    root_def.classname = "__root";
    root_def.cpp_class = false;

    std::unordered_set<std::string> explicit_nested_ids;
    for (auto const& n : bp.nested()) {
        explicit_nested_ids.insert(std::string(interner.resolve(n.id)));
    }

    for (auto const& n : bp.nodes()) {
        std::string node_id = std::string(interner.resolve(n.semantic.id));
        std::string node_type = std::string(interner.resolve(n.semantic.type));

        if (explicit_nested_ids.count(node_id) > 0) {
            continue;
        }

        if (auto td = registry.get(node_type); td && !td->cpp_class) {
            SubBlueprintRef ref;
            ref.id = node_id;
            ref.type_name = node_type;
            ref.blueprint_path = node_type;
            root_def.sub_blueprints.push_back(std::move(ref));
            continue;
        }

        DeviceInstance dev;
        dev.name = node_id;
        dev.classname = node_type;
        for (auto const& [k, v] : n.semantic.params) {
            dev.params[std::string(interner.resolve(k))] = std::to_string(v);
        }
        if (auto td = registry.get(dev.classname)) {
            dev = merge_device_instance(dev, *td);
        }
        ctx.devices.push_back(std::move(dev));
    }

    for (auto const& w : bp.wires()) {
        auto from = endpoint_to_port_string(w.source, arena, interner);
        auto to = endpoint_to_port_string(w.target, arena, interner);
        if (!from || !to) {
            continue;
        }
        ctx.connections.push_back(Connection{*from, *to});
    }

    root_def.devices = ctx.devices;
    root_def.connections = ctx.connections;

    for (auto const& n : bp.nested()) {
        if (n.blueprint_id().empty()) {
            continue;
        }
        SubBlueprintRef ref;
        ref.id = std::string(interner.resolve(n.id));
        ref.type_name = std::string(interner.resolve(n.blueprint_id()));
        ref.blueprint_path = ref.type_name;
        root_def.sub_blueprints.push_back(std::move(ref));
    }

    std::set<std::string> loading_stack;
    TypeDefinition expanded = expand_sub_blueprint_references(root_def, registry, loading_stack);
    ctx.devices = std::move(expanded.devices);
    ctx.connections = std::move(expanded.connections);

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

    auto ctx = parse_blueprint_v3(content, registry);
    if (ctx.devices.empty()) {
        std::cerr << "Failed to parse v3 blueprint or blueprint has no devices: " << json_file << "\n";
        return 2;
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
