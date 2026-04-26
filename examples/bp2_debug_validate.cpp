#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/codec/blueprint_codec_internal.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "blueprint_v2/interface/type_definition_interface.h"
#include "blueprint_v2/path/path.h"
#include "blueprint_v2/validation/wire_validator.h"
#include "blueprint_v2/validation/invariant_checker.h"
#include "blueprint_v2/validation/path_resolver.h"
#include "io/json/component_registry_json_loader.h"
#include "core/strings/interned_id.h"

#include <fstream>
#include <iostream>
#include <sstream>

namespace {

std::string ep_to_string(const bp2::WireEndpoint& ep, core::StringInterner const& interner) {
    return "/" + std::string(interner.resolve(ep.node)) + ":" + std::string(interner.resolve(ep.port));
}

std::string port_type_name(PortType type) {
    switch (type) {
        case PortType::V: return "V";
        case PortType::I: return "I";
        case PortType::Signal: return "Signal";
        case PortType::Bool: return "Bool";
        case PortType::RPM: return "RPM";
        case PortType::Temperature: return "Temperature";
        case PortType::Pressure: return "Pressure";
        case PortType::Position: return "Position";
        case PortType::Contextual: return "Contextual";
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
                                core::StringInterner& interner) {
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

bp2::Interface bridge_iface_for_json_node(const nlohmann::json& node, core::StringInterner& interner) {
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


void normalize_node_ports_recursive(nlohmann::json& root, const ComponentRegistry& reg, core::StringInterner& interner) {
     if (!root.contains("nodes") || !root["nodes"].is_array()) {
         return;
     }

     for (auto& node : root["nodes"]) {
         if (!node.is_object() || !node.contains("id") || !node.contains("type")) {
             continue;
         }

         const std::string node_type = node["type"].get<std::string>();

         if (node_type == "BlueprintInput" || node_type == "BlueprintOutput") {
             overwrite_ports_from_iface(node, bridge_iface_for_json_node(node, interner), interner);
             continue;
         }

         const auto* def = reg.get(node_type);
         if (def) {
             overwrite_ports_from_iface(node, bp2::interface_from_type_definition(*def, interner), interner);
         }
     }

     // Recursively process embedded blueprints via BlueprintInstance source
     if (root.contains("nodes") && root["nodes"].is_array()) {
         for (auto& node : root["nodes"]) {
             if (!node.is_object() || node["type"] != "BlueprintInstance" || !node.contains("source")) {
                 continue;
             }
             auto& source = node["source"];
             if (source.value("embedded", false) && source.contains("inline_def")) {
                 normalize_node_ports_recursive(source["inline_def"], reg, interner);
             }
         }
     }
 }

bool canonicalize_wire_orientation(nlohmann::json& root,
                                    core::StringInterner& interner,
                                    const ComponentRegistry& reg) {
     if (!root.contains("wires") || !root["wires"].is_array()) {
         return false;
     }

     bp2::PathArena arena(interner);
     bp2::Blueprint partial;
     partial = partial.with_id(interner.intern(root.value("id", std::string{})));
     if (root.contains("name") && root["name"].is_string()) {
         partial = partial.with_name(root["name"].get<std::string>());
     } else if (root.contains("display_name") && root["display_name"].is_string()) {
         partial = partial.with_name(root["display_name"].get<std::string>());
     }
     if (root.contains("interface") && root["interface"].is_array()) {
         partial = partial.with_interface(bp2::codec_detail::decode_interface(root["interface"], interner));
     }
     if (root.contains("nodes") && root["nodes"].is_array()) {
         partial = bp2::codec_detail::decode_nodes(std::move(partial), root["nodes"], interner, reg);
     }
     if (root.contains("wires") && root["wires"].is_array()) {
         partial = bp2::codec_detail::decode_wires(std::move(partial), root["wires"], interner);
     }

     bp2::PathResolver resolver;
     bool changed = false;
     for (auto& wire_json : root["wires"]) {
         const core::InternedId wire_id = interner.lookup(wire_json.at("id").get<std::string>());
         const auto* wire = partial.find_wire(wire_id);
         if (!wire) {
             continue;
         }
         if (resolver.can_connect(wire->source, wire->target, partial, reg, interner)) {
             continue;
         }
         if (resolver.can_connect(wire->target, wire->source, partial, reg, interner)) {
             const auto old_source = wire_json["source"];
             wire_json["source"] = wire_json["target"];
             wire_json["target"] = old_source;
             changed = true;
         }
     }

     // Recursively process embedded blueprints via BlueprintInstance source
     if (root.contains("nodes") && root["nodes"].is_array()) {
         for (auto& node : root["nodes"]) {
             if (!node.is_object() || node["type"] != "BlueprintInstance" || !node.contains("source")) {
                 continue;
             }
             auto& source = node["source"];
             if (source.value("embedded", false) && source.contains("inline_def")) {
                 changed = canonicalize_wire_orientation(source["inline_def"], interner, reg) || changed;
             }
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

    core::StringInterner interner;
    bp2::PathArena arena(interner);
    ComponentRegistry reg = load_component_registry("library/");

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
         normalize_node_ports_recursive(j, reg, interner);

         // Repeatedly apply wire orientation canonicalization until no changes or success
         bool changed = true;
         while (changed) {
             changed = canonicalize_wire_orientation(j, interner, reg);
             
             // Check if strict decode succeeds after canonicalization pass
             core::StringInterner check_interner;
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
             core::StringInterner diag_interner;
             bp2::PathArena diag_arena(diag_interner);
             bp2::Blueprint partial;
             partial = partial.with_id(diag_interner.intern(j.value("id", std::string{})));
             if (j.contains("name") && j["name"].is_string()) {
                 partial = partial.with_name(j["name"].get<std::string>());
             } else if (j.contains("display_name") && j["display_name"].is_string()) {
                 partial = partial.with_name(j["display_name"].get<std::string>());
             }
             if (j.contains("interface") && j["interface"].is_array()) {
                 partial = partial.with_interface(bp2::codec_detail::decode_interface(j["interface"], diag_interner));
             }
             if (j.contains("nodes") && j["nodes"].is_array()) {
                 partial = bp2::codec_detail::decode_nodes(std::move(partial), j["nodes"], diag_interner, reg);
             }
             if (j.contains("wires") && j["wires"].is_array()) {
                 partial = bp2::codec_detail::decode_wires(std::move(partial), j["wires"], diag_interner);
             }

             bp2::PathResolver resolver;
             for (const auto& wire : partial.wires()) {
                 auto src = resolver.resolve(wire.source, partial, reg, diag_interner);
                 auto tgt = resolver.resolve(wire.target, partial, reg, diag_interner);

                 if (!src || !tgt) {
                     std::cout << "first invalid wire: " << diag_interner.resolve(wire.id) << " (unresolved)\n";
                     if (!src) {
                         std::cout << "  source: " << ep_to_string(wire.source, diag_interner) << " (unresolved)\n";
                     }
                     if (!tgt) {
                         std::cout << "  target: " << ep_to_string(wire.target, diag_interner) << " (unresolved)\n";
                     }
                     return 1;
                 }

                 if (src->port.domain != tgt->port.domain) {
                     std::cout << "first invalid wire: " << diag_interner.resolve(wire.id) << " (domain mismatch)\n";
                     std::cout << "  source: " << ep_to_string(wire.source, diag_interner)
                               << " domain=" << static_cast<int>(src->port.domain)
                               << " port=" << diag_interner.resolve(src->port.name) << "\n";
                     std::cout << "  target: " << ep_to_string(wire.target, diag_interner)
                               << " domain=" << static_cast<int>(tgt->port.domain)
                               << " port=" << diag_interner.resolve(tgt->port.name) << "\n";
                     return 1;
                 }

                 if (!resolver.can_connect(wire.source, wire.target, partial, reg, diag_interner)) {
                     std::cout << "first invalid wire: " << diag_interner.resolve(wire.id) << " (direction/orientation invalid)\n";
                     std::cout << "  source: " << ep_to_string(wire.source, diag_interner)
                               << " domain=" << static_cast<int>(src->port.domain)
                               << " direction=" << static_cast<int>(src->port.direction)
                               << " port=" << diag_interner.resolve(src->port.name) << "\n";
                     std::cout << "  target: " << ep_to_string(wire.target, diag_interner)
                               << " domain=" << static_cast<int>(tgt->port.domain)
                               << " direction=" << static_cast<int>(tgt->port.direction)
                               << " port=" << diag_interner.resolve(tgt->port.name) << "\n";
                     std::cout << "  reverse_can_connect="
                               << resolver.can_connect(wire.target, wire.source, partial, reg, diag_interner)
                               << "\n";
                     return 1;
                 }

                 auto wr = bp2::WireValidator::validate(wire, partial, reg, diag_interner);
                 if (!wr.valid) {
                     std::cout << "first invalid wire: " << diag_interner.resolve(wire.id)
                               << " error=" << wr.error << "\n";
                     std::cout << "  source: " << ep_to_string(wire.source, diag_interner)
                               << " domain=" << static_cast<int>(src->port.domain)
                               << " direction=" << static_cast<int>(src->port.direction)
                               << " port=" << diag_interner.resolve(src->port.name) << "\n";
                     std::cout << "  target: " << ep_to_string(wire.target, diag_interner)
                               << " domain=" << static_cast<int>(tgt->port.domain)
                               << " direction=" << static_cast<int>(tgt->port.direction)
                               << " port=" << diag_interner.resolve(tgt->port.name) << "\n";
                     std::cout << "  reverse_can_connect="
                               << resolver.can_connect(wire.target, wire.source, partial, reg, diag_interner)
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
         if (j.contains("name") && j["name"].is_string()) {
             partial = partial.with_name(j["name"].get<std::string>());
         } else if (j.contains("display_name") && j["display_name"].is_string()) {
             partial = partial.with_name(j["display_name"].get<std::string>());
         }
         if (j.contains("interface") && j["interface"].is_array()) {
             partial = partial.with_interface(bp2::codec_detail::decode_interface(j["interface"], interner));
         }
         if (j.contains("nodes") && j["nodes"].is_array()) {
             partial = bp2::codec_detail::decode_nodes(std::move(partial), j["nodes"], interner, reg);
         }
         if (j.contains("wires") && j["wires"].is_array()) {
             partial = bp2::codec_detail::decode_wires(std::move(partial), j["wires"], interner);
         }

         bp2::PathResolver resolver;
         for (const auto& wire : partial.wires()) {
             auto src = resolver.resolve(wire.source, partial, reg, interner);
             auto tgt = resolver.resolve(wire.target, partial, reg, interner);
             
             // Check for unresolved paths
             if (!src || !tgt) {
                 std::cout << "first invalid wire: " << interner.resolve(wire.id) << " (unresolved)\n";
                 if (!src) {
                     std::cout << "  source: " << ep_to_string(wire.source, interner) << " (unresolved)\n";
                 }
                 if (!tgt) {
                     std::cout << "  target: " << ep_to_string(wire.target, interner) << " (unresolved)\n";
                 }
                 return 1;
             }
             
             // Check for domain mismatch
             if (src->port.domain != tgt->port.domain) {
                 std::cout << "first invalid wire: " << interner.resolve(wire.id) << " (domain mismatch)\n";
                 std::cout << "  source: " << ep_to_string(wire.source, interner)
                           << " domain=" << static_cast<int>(src->port.domain)
                           << " port=" << interner.resolve(src->port.name) << "\n";
                 std::cout << "  target: " << ep_to_string(wire.target, interner)
                           << " domain=" << static_cast<int>(tgt->port.domain)
                           << " port=" << interner.resolve(tgt->port.name) << "\n";
                 return 1;
             }
             
             // Check for direction/orientation issues
             if (!resolver.can_connect(wire.source, wire.target, partial, reg, interner)) {
                 std::cout << "first invalid wire: " << interner.resolve(wire.id) << " (direction/orientation invalid)\n";
                 std::cout << "  source: " << ep_to_string(wire.source, interner)
                           << " domain=" << static_cast<int>(src->port.domain)
                           << " direction=" << static_cast<int>(src->port.direction)
                           << " port=" << interner.resolve(src->port.name) << "\n";
                 std::cout << "  target: " << ep_to_string(wire.target, interner)
                           << " domain=" << static_cast<int>(tgt->port.domain)
                           << " direction=" << static_cast<int>(tgt->port.direction)
                           << " port=" << interner.resolve(tgt->port.name) << "\n";
                 std::cout << "  reverse_can_connect="
                           << resolver.can_connect(wire.target, wire.source, partial, reg, interner)
                           << "\n";
                 return 1;
             }
             
             // Check for WireValidator issues
             auto wr = bp2::WireValidator::validate(wire, partial, reg, interner);
             if (!wr.valid) {
                 std::cout << "first invalid wire: " << interner.resolve(wire.id)
                           << " error=" << wr.error << "\n";
                 std::cout << "  source: " << ep_to_string(wire.source, interner)
                           << " domain=" << static_cast<int>(src->port.domain)
                           << " direction=" << static_cast<int>(src->port.direction)
                           << " port=" << interner.resolve(src->port.name) << "\n";
                 std::cout << "  target: " << ep_to_string(wire.target, interner)
                           << " domain=" << static_cast<int>(tgt->port.domain)
                           << " direction=" << static_cast<int>(tgt->port.direction)
                           << " port=" << interner.resolve(tgt->port.name) << "\n";
                 std::cout << "  reverse_can_connect="
                           << resolver.can_connect(wire.target, wire.source, partial, reg, interner)
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
