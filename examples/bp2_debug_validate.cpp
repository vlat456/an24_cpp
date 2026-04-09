#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/codec/blueprint_codec_internal.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "blueprint_v2/interface/type_definition_interface.h"
#include "blueprint_v2/path/path.h"
#include "blueprint_v2/validation/wire_validator.h"
#include "blueprint_v2/validation/invariant_checker.h"
#include "blueprint_v2/validation/path_resolver.h"
#include "json_parser/json_parser.h"
#include "ui/core/interned_id.h"

#include <fstream>
#include <iostream>
#include <sstream>

namespace {

std::string port_type_name(PortType type) {
    switch (type) {
        case PortType::V: return "V";
        case PortType::I: return "I";
        case PortType::Bool: return "Bool";
        case PortType::RPM: return "RPM";
        case PortType::Temperature: return "Temperature";
        case PortType::Pressure: return "Pressure";
        case PortType::Position: return "Position";
        case PortType::Any: return "Any";
    }
    return "Any";
}

nlohmann::json encode_ports_from_iface(const bp2::Interface& iface) {
    nlohmann::json ports = nlohmann::json::object();
    for (const auto& p : iface.ports()) {
        nlohmann::json entry;
        entry["direction"] = (p.direction == bp2::Direction::Input)
            ? "In"
            : (p.direction == bp2::Direction::Output ? "Out" : "InOut");
        entry["type"] = static_cast<int>(p.port_type);
        ports[std::string(p.name.raw() ? "" : "")] = entry;
    }
    return ports;
}

void overwrite_ports_from_iface(nlohmann::json& node,
                                const bp2::Interface& iface,
                                ui::StringInterner& interner) {
    nlohmann::json ports = nlohmann::json::object();
    for (const auto& p : iface.ports()) {
        nlohmann::json entry;
        entry["direction"] = (p.direction == bp2::Direction::Input)
            ? "In"
            : (p.direction == bp2::Direction::Output ? "Out" : "InOut");
        entry["type"] = static_cast<int>(p.port_type);
        ports[std::string(interner.resolve(p.name))] = std::move(entry);
    }
    node["ports"] = std::move(ports);
}

std::optional<PortType> port_type_from_exposed_string(const std::string& s) {
    if (s == "V") return PortType::V;
    if (s == "I") return PortType::I;
    if (s == "Bool") return PortType::Bool;
    if (s == "RPM") return PortType::RPM;
    if (s == "Temperature") return PortType::Temperature;
    if (s == "Pressure") return PortType::Pressure;
    if (s == "Position") return PortType::Position;
    if (s == "Any") return PortType::Any;
    if (s == "Signal") return PortType::Any;
    return std::nullopt;
}

bp2::Interface bridge_iface_for_json_node(const nlohmann::json& node, ui::StringInterner& interner) {
    const std::string type = node.at("type").get<std::string>();
    const bool is_input = (type == "BlueprintInput");
    const auto& sparams = node.value("string_params", nlohmann::json::object());
    const std::string exposed_type = sparams.value("exposed_type", "Any");
    auto pt = port_type_from_exposed_string(exposed_type).value_or(PortType::Any);
    const Domain domain = ::domain_for_port_type(pt);

    bp2::PortDescriptor ext_pd;
    ext_pd.name = interner.intern("ext");
    ext_pd.domain = domain;
    ext_pd.direction = is_input ? bp2::Direction::Input : bp2::Direction::Output;
    ext_pd.port_type = pt;

    bp2::PortDescriptor port_pd;
    port_pd.name = interner.intern("port");
    port_pd.domain = domain;
    port_pd.direction = is_input ? bp2::Direction::Output : bp2::Direction::Input;
    port_pd.port_type = pt;

    return bp2::Interface({ext_pd, port_pd});
}

void normalize_definition_interfaces(nlohmann::json& root, const TypeRegistry& reg, ui::StringInterner& interner) {
    if (!root.contains("nested") || !root["nested"].is_array()) {
        return;
    }
    for (auto& nested : root["nested"]) {
        if (!nested.is_object() || !nested.value("embedded", false) || !nested.contains("definition")) {
            continue;
        }
        auto& def = nested["definition"];
        if (!def.is_object() || !def.contains("nodes") || !def["nodes"].is_array()) {
            continue;
        }

        std::vector<bp2::PortDescriptor> ports;
        for (const auto& node : def["nodes"]) {
            if (!node.is_object() || !node.contains("type") || !node.contains("name")) {
                continue;
            }
            const std::string node_type = node["type"].get<std::string>();
            if (node_type != "BlueprintInput" && node_type != "BlueprintOutput") {
                continue;
            }

            const auto& sparams = node.value("string_params", nlohmann::json::object());
            const std::string exposed_type = sparams.value("exposed_type", "Any");
            auto pt = port_type_from_exposed_string(exposed_type).value_or(PortType::Any);

            bp2::PortDescriptor pd;
            pd.name = interner.intern(node["name"].get<std::string>());
            pd.domain = ::domain_for_port_type(pt);
            pd.direction = (node_type == "BlueprintInput") ? bp2::Direction::Input : bp2::Direction::Output;
            pd.port_type = pt;
            ports.push_back(std::move(pd));
        }

        bp2::Interface iface(std::move(ports));
        def["interface"] = bp2::codec_detail::encode_interface(iface, interner, nullptr);
    }
}

void normalize_node_ports_recursive(nlohmann::json& root, const TypeRegistry& reg, ui::StringInterner& interner) {
    if (!root.contains("nodes") || !root["nodes"].is_array()) {
        return;
    }

    std::unordered_map<std::string, bp2::Interface> nested_ifaces;
    if (root.contains("nested") && root["nested"].is_array()) {
        for (auto& nested : root["nested"]) {
            if (!nested.is_object() || !nested.contains("id") || !nested.contains("blueprint")) {
                continue;
            }
            const std::string nested_id = nested["id"].get<std::string>();
            if (nested.value("embedded", false) && nested.contains("definition") && nested["definition"].contains("interface")) {
                nested_ifaces[nested_id] = bp2::codec_detail::decode_interface(nested["definition"]["interface"], interner);
            } else {
                const auto* def = reg.get(nested["blueprint"].get<std::string>());
                if (def) {
                    nested_ifaces[nested_id] = bp2::interface_from_type_definition(*def, interner);
                }
            }
        }
    }

    for (auto& node : root["nodes"]) {
        if (!node.is_object() || !node.contains("id") || !node.contains("type")) {
            continue;
        }

        const std::string node_id = node["id"].get<std::string>();
        const std::string node_type = node["type"].get<std::string>();

        if (node_type == "BlueprintInput" || node_type == "BlueprintOutput") {
            overwrite_ports_from_iface(node, bridge_iface_for_json_node(node, interner), interner);
            continue;
        }

        auto nested_it = nested_ifaces.find(node_id);
        if (nested_it != nested_ifaces.end()) {
            overwrite_ports_from_iface(node, nested_it->second, interner);
            continue;
        }

        const auto* def = reg.get(node_type);
        if (def) {
            overwrite_ports_from_iface(node, bp2::interface_from_type_definition(*def, interner), interner);
        }
    }

    if (root.contains("nested") && root["nested"].is_array()) {
        for (auto& nested : root["nested"]) {
            if (!nested.is_object() || !nested.value("embedded", false) || !nested.contains("definition")) {
                continue;
            }
            normalize_node_ports_recursive(nested["definition"], reg, interner);
        }
    }
}

bool canonicalize_wire_orientation(nlohmann::json& root,
                                   ui::StringInterner& interner,
                                   const TypeRegistry& reg) {
    if (!root.contains("wires") || !root["wires"].is_array()) {
        return false;
    }

    bp2::PathArena arena(interner);
    bp2::Blueprint partial;
    partial = partial.with_id(interner.intern(root.value("id", std::string{})));
    if (root.contains("display_name") && root["display_name"].is_string()) {
        partial = partial.with_display_name(root["display_name"].get<std::string>());
    }
    if (root.contains("name") && root["name"].is_string()) {
        partial = partial.with_name(root["name"].get<std::string>());
    }
    if (root.contains("interface") && root["interface"].is_array()) {
        partial = partial.with_interface(bp2::codec_detail::decode_interface(root["interface"], interner));
    }
    if (root.contains("nodes") && root["nodes"].is_array()) {
        partial = bp2::codec_detail::decode_nodes(std::move(partial), root["nodes"], interner, reg);
    }
    if (root.contains("wires") && root["wires"].is_array()) {
        partial = bp2::codec_detail::decode_wires(std::move(partial), root["wires"], interner, arena);
    }
    if (root.contains("nested") && root["nested"].is_array()) {
        partial = bp2::codec_detail::decode_nested(std::move(partial), root["nested"], interner, reg, arena);
    }
    partial = bp2::canonicalize_composite_host_ifaces(std::move(partial));

    bp2::PathResolver resolver;
    bool changed = false;
    for (auto& wire_json : root["wires"]) {
        const ui::InternedId wire_id = interner.lookup(wire_json.at("id").get<std::string>());
        const auto* wire = partial.find_wire(wire_id);
        if (!wire) {
            continue;
        }
        if (resolver.can_connect(wire->source, wire->target, partial, arena, reg, interner)) {
            continue;
        }
        if (resolver.can_connect(wire->target, wire->source, partial, arena, reg, interner)) {
            const auto old_source = wire_json["source"];
            wire_json["source"] = wire_json["target"];
            wire_json["target"] = old_source;
            changed = true;
        }
    }

    if (root.contains("nested") && root["nested"].is_array()) {
        for (auto& nested : root["nested"]) {
            if (!nested.is_object() || !nested.value("embedded", false) || !nested.contains("definition")) {
                continue;
            }
            changed = canonicalize_wire_orientation(nested["definition"], interner, reg) || changed;
        }
    }

    return changed;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) {
        std::cerr << "usage: bp2_debug_validate <file.blueprint> [--rewrite]\n";
        return 2;
    }

    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry reg = load_type_registry("library/");

    std::ifstream file(argv[1]);
    if (!file.is_open()) {
        std::cerr << "failed to open file\n";
        return 2;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();

    const bool rewrite = (argc >= 3 && std::string(argv[2]) == "--rewrite");

    bp2::DecodeError err;
    auto bp = bp2::BlueprintCodec::decode(buffer.str(), interner, arena, reg, &err);
    if (bp) {
        std::cout << "decode ok\n";
        return 0;
    }

    std::cout << "decode failed: " << err.message << "\n";

    if (rewrite) {
        auto j = nlohmann::json::parse(buffer.str());
        normalize_definition_interfaces(j, reg, interner);
        normalize_node_ports_recursive(j, reg, interner);

        // Repeatedly apply wire orientation canonicalization until no changes or success
        bool changed = true;
        while (changed) {
            changed = canonicalize_wire_orientation(j, interner, reg);
            
            // Check if strict decode succeeds after canonicalization pass
            ui::StringInterner check_interner;
            bp2::PathArena check_arena(check_interner);
            bp2::DecodeError check_err;
            auto rewritten = bp2::BlueprintCodec::decode(j.dump(), check_interner, check_arena, reg, &check_err);
            if (rewritten) {
                std::ofstream out(argv[1]);
                out << j.dump(2) << "\n";
                out.close();
                std::cout << "rewrite ok\n";
                return 0;
            }
            
            if (!changed) break;  // No more changes, exit loop
        }

        // Rewrite still fails - print diagnostic using same path as non-rewrite mode
        std::ofstream out(argv[1]);
        out << j.dump(2) << "\n";
        out.close();

        try {
            ui::StringInterner diag_interner;
            bp2::PathArena diag_arena(diag_interner);
            bp2::Blueprint partial;
            partial = partial.with_id(diag_interner.intern(j.value("id", std::string{})));
            if (j.contains("display_name") && j["display_name"].is_string()) {
                partial = partial.with_display_name(j["display_name"].get<std::string>());
            }
            if (j.contains("name") && j["name"].is_string()) {
                partial = partial.with_name(j["name"].get<std::string>());
            }
            if (j.contains("interface") && j["interface"].is_array()) {
                partial = partial.with_interface(bp2::codec_detail::decode_interface(j["interface"], diag_interner));
            }
            if (j.contains("nodes") && j["nodes"].is_array()) {
                partial = bp2::codec_detail::decode_nodes(std::move(partial), j["nodes"], diag_interner, reg);
            }
            if (j.contains("wires") && j["wires"].is_array()) {
                partial = bp2::codec_detail::decode_wires(std::move(partial), j["wires"], diag_interner, diag_arena);
            }
            if (j.contains("nested") && j["nested"].is_array()) {
                partial = bp2::codec_detail::decode_nested(std::move(partial), j["nested"], diag_interner, reg, diag_arena);
            }
            partial = bp2::canonicalize_composite_host_ifaces(std::move(partial));

            bp2::PathResolver resolver;
            for (const auto& wire : partial.wires()) {
                auto src = resolver.resolve(wire.source, partial, diag_arena, reg, diag_interner);
                auto tgt = resolver.resolve(wire.target, partial, diag_arena, reg, diag_interner);

                if (!src || !tgt) {
                    std::cout << "first invalid wire: " << diag_interner.resolve(wire.id) << " (unresolved)\n";
                    if (!src) {
                        std::cout << "  source: " << diag_arena.to_string(wire.source) << " (unresolved)\n";
                    }
                    if (!tgt) {
                        std::cout << "  target: " << diag_arena.to_string(wire.target) << " (unresolved)\n";
                    }
                    return 1;
                }

                if (src->port.domain != tgt->port.domain) {
                    std::cout << "first invalid wire: " << diag_interner.resolve(wire.id) << " (domain mismatch)\n";
                    std::cout << "  source: " << diag_arena.to_string(wire.source)
                              << " domain=" << static_cast<int>(src->port.domain)
                              << " port=" << diag_interner.resolve(src->port.name) << "\n";
                    std::cout << "  target: " << diag_arena.to_string(wire.target)
                              << " domain=" << static_cast<int>(tgt->port.domain)
                              << " port=" << diag_interner.resolve(tgt->port.name) << "\n";
                    return 1;
                }

                if (!resolver.can_connect(wire.source, wire.target, partial, diag_arena, reg, diag_interner)) {
                    std::cout << "first invalid wire: " << diag_interner.resolve(wire.id) << " (direction/orientation invalid)\n";
                    std::cout << "  source: " << diag_arena.to_string(wire.source)
                              << " domain=" << static_cast<int>(src->port.domain)
                              << " direction=" << static_cast<int>(src->port.direction)
                              << " port=" << diag_interner.resolve(src->port.name) << "\n";
                    std::cout << "  target: " << diag_arena.to_string(wire.target)
                              << " domain=" << static_cast<int>(tgt->port.domain)
                              << " direction=" << static_cast<int>(tgt->port.direction)
                              << " port=" << diag_interner.resolve(tgt->port.name) << "\n";
                    std::cout << "  reverse_can_connect="
                              << resolver.can_connect(wire.target, wire.source, partial, diag_arena, reg, diag_interner)
                              << "\n";
                    return 1;
                }

                auto wr = bp2::WireValidator::validate(wire, partial, diag_arena, reg, diag_interner);
                if (!wr.valid) {
                    std::cout << "first invalid wire: " << diag_interner.resolve(wire.id)
                              << " error=" << wr.error << "\n";
                    std::cout << "  source: " << diag_arena.to_string(wire.source)
                              << " domain=" << static_cast<int>(src->port.domain)
                              << " direction=" << static_cast<int>(src->port.direction)
                              << " port=" << diag_interner.resolve(src->port.name) << "\n";
                    std::cout << "  target: " << diag_arena.to_string(wire.target)
                              << " domain=" << static_cast<int>(tgt->port.domain)
                              << " direction=" << static_cast<int>(tgt->port.direction)
                              << " port=" << diag_interner.resolve(tgt->port.name) << "\n";
                    std::cout << "  reverse_can_connect="
                              << resolver.can_connect(wire.target, wire.source, partial, diag_arena, reg, diag_interner)
                              << "\n";
                    return 1;
                }
            }
        } catch (const std::exception& ex) {
            std::cout << "partial decode failed: " << ex.what() << "\n";
        }

        std::cout << "rewrite still fails\n";
        return 1;
    }

    auto j = nlohmann::json::parse(buffer.str());
    try {
        bp2::Blueprint partial;
        partial = partial.with_id(interner.intern(j.value("id", std::string{})));
        if (j.contains("display_name") && j["display_name"].is_string()) {
            partial = partial.with_display_name(j["display_name"].get<std::string>());
        }
        if (j.contains("name") && j["name"].is_string()) {
            partial = partial.with_name(j["name"].get<std::string>());
        }
        if (j.contains("interface") && j["interface"].is_array()) {
            partial = partial.with_interface(bp2::codec_detail::decode_interface(j["interface"], interner));
        }
        if (j.contains("nodes") && j["nodes"].is_array()) {
            partial = bp2::codec_detail::decode_nodes(std::move(partial), j["nodes"], interner, reg);
        }
        if (j.contains("wires") && j["wires"].is_array()) {
            partial = bp2::codec_detail::decode_wires(std::move(partial), j["wires"], interner, arena);
        }
        if (j.contains("nested") && j["nested"].is_array()) {
            partial = bp2::codec_detail::decode_nested(std::move(partial), j["nested"], interner, reg, arena);
        }
        partial = bp2::canonicalize_composite_host_ifaces(std::move(partial));

        bp2::PathResolver resolver;
        for (const auto& wire : partial.wires()) {
            auto src = resolver.resolve(wire.source, partial, arena, reg, interner);
            auto tgt = resolver.resolve(wire.target, partial, arena, reg, interner);
            
            // Check for unresolved paths
            if (!src || !tgt) {
                std::cout << "first invalid wire: " << interner.resolve(wire.id) << " (unresolved)\n";
                if (!src) {
                    std::cout << "  source: " << arena.to_string(wire.source) << " (unresolved)\n";
                }
                if (!tgt) {
                    std::cout << "  target: " << arena.to_string(wire.target) << " (unresolved)\n";
                }
                return 1;
            }
            
            // Check for domain mismatch
            if (src->port.domain != tgt->port.domain) {
                std::cout << "first invalid wire: " << interner.resolve(wire.id) << " (domain mismatch)\n";
                std::cout << "  source: " << arena.to_string(wire.source)
                          << " domain=" << static_cast<int>(src->port.domain)
                          << " port=" << interner.resolve(src->port.name) << "\n";
                std::cout << "  target: " << arena.to_string(wire.target)
                          << " domain=" << static_cast<int>(tgt->port.domain)
                          << " port=" << interner.resolve(tgt->port.name) << "\n";
                return 1;
            }
            
            // Check for direction/orientation issues
            if (!resolver.can_connect(wire.source, wire.target, partial, arena, reg, interner)) {
                std::cout << "first invalid wire: " << interner.resolve(wire.id) << " (direction/orientation invalid)\n";
                std::cout << "  source: " << arena.to_string(wire.source)
                          << " domain=" << static_cast<int>(src->port.domain)
                          << " direction=" << static_cast<int>(src->port.direction)
                          << " port=" << interner.resolve(src->port.name) << "\n";
                std::cout << "  target: " << arena.to_string(wire.target)
                          << " domain=" << static_cast<int>(tgt->port.domain)
                          << " direction=" << static_cast<int>(tgt->port.direction)
                          << " port=" << interner.resolve(tgt->port.name) << "\n";
                std::cout << "  reverse_can_connect="
                          << resolver.can_connect(wire.target, wire.source, partial, arena, reg, interner)
                          << "\n";
                return 1;
            }
            
            // Check for WireValidator issues
            auto wr = bp2::WireValidator::validate(wire, partial, arena, reg, interner);
            if (!wr.valid) {
                std::cout << "first invalid wire: " << interner.resolve(wire.id)
                          << " error=" << wr.error << "\n";
                std::cout << "  source: " << arena.to_string(wire.source)
                          << " domain=" << static_cast<int>(src->port.domain)
                          << " direction=" << static_cast<int>(src->port.direction)
                          << " port=" << interner.resolve(src->port.name) << "\n";
                std::cout << "  target: " << arena.to_string(wire.target)
                          << " domain=" << static_cast<int>(tgt->port.domain)
                          << " direction=" << static_cast<int>(tgt->port.direction)
                          << " port=" << interner.resolve(tgt->port.name) << "\n";
                std::cout << "  reverse_can_connect="
                          << resolver.can_connect(wire.target, wire.source, partial, arena, reg, interner)
                          << "\n";
                return 1;
            }
        }

        auto inv = bp2::InvariantChecker::validate(partial, arena, reg, interner);
        std::cout << "partial decode ok, invariant valid=" << inv.valid << " error=" << inv.error << "\n";
    } catch (const std::exception& ex) {
        std::cout << "partial decode failed: " << ex.what() << "\n";
    }

    return 1;
}
