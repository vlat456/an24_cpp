#include "blueprint_codec.h"
#include "blueprint_codec_internal.h"
#include "blueprint_v2/validation/invariant_checker.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <unordered_set>

namespace bp2 {

std::string BlueprintCodec::encode(Blueprint const& bp,
                                   core::StringInterner const& interner,
                                   PathArena const& arena,
                                   const ::ComponentRegistry* parser_registry) {
    nlohmann::json j;
    const ComponentSpec* type_def = nullptr;
    if (parser_registry && !bp.id().empty()) {
        type_def = parser_registry->get(std::string(interner.resolve(bp.id())));
    }

    j["format"] = "blueprint";
    j["version"] = 1;
    j["blueprint_id"] = std::string(interner.resolve(bp.id()));
    j["name"] = bp.name();
    j["interface"] = codec_detail::encode_interface(bp.iface(), interner, type_def);
    j["nodes"] = codec_detail::encode_nodes(bp.nodes(), interner, arena, parser_registry);
    j["wires"] = codec_detail::encode_wires(bp.wires(), interner);

    return j.dump(2);
}

std::optional<Blueprint> BlueprintCodec::decode(
    std::string_view json_str,
    core::StringInterner& interner,
    PathArena& arena,
    const ::ComponentRegistry& parser_registry,
    DecodeError* error_out) {
    try {
        auto j = nlohmann::json::parse(json_str);
        if (!j.contains("format") || !j["format"].is_string()
            || j["format"].get<std::string>() != "blueprint") {
            if (error_out) {
                error_out->message = "Unsupported blueprint format (expected \"blueprint\")";
            }
            return std::nullopt;
        }
        if (!j.contains("version") || !j["version"].is_number_integer()
            || j["version"].get<int>() != 1) {
            if (error_out) {
                error_out->message = "Unsupported blueprint version (expected 1)";
            }
            return std::nullopt;
        }

        if (!j.contains("blueprint_id") || !j["blueprint_id"].is_string()) {
            if (error_out) {
                error_out->message = "Missing required string field: blueprint_id";
            }
            return std::nullopt;
        }
        {
            const std::string bp_id = j["blueprint_id"].get<std::string>();
            if (bp_id.empty()) {
                if (error_out) {
                    error_out->message = "blueprint_id must not be empty";
                }
                return std::nullopt;
            }
            for (char c : bp_id) {
                if (c <= 0x20 || c > 0x7E) {
                    if (error_out) {
                        error_out->message = "blueprint_id must contain only printable ASCII with no whitespace";
                    }
                    return std::nullopt;
                }
            }
        }
        if (!j.contains("name") || !j["name"].is_string()) {
            if (error_out) {
                error_out->message = "Missing required string field: name";
            }
            return std::nullopt;
        }
        if (j["name"].get<std::string>().empty()) {
            if (error_out) {
                error_out->message = "name must not be empty";
            }
            return std::nullopt;
        }
        if (!j.contains("interface") || !j["interface"].is_array()) {
            if (error_out) {
                error_out->message = "Missing required array field: interface";
            }
            return std::nullopt;
        }
        if (!j.contains("nodes") || !j["nodes"].is_array()) {
            if (error_out) {
                error_out->message = "Missing required array field: nodes";
            }
            return std::nullopt;
        }
        if (!j.contains("wires") || !j["wires"].is_array()) {
            if (error_out) {
                error_out->message = "Missing required array field: wires";
            }
            return std::nullopt;
        }

        static const std::unordered_set<std::string> allowed_top_level = {
            "format", "version", "blueprint_id", "name", "interface", "nodes", "wires"
        };
        codec_detail::check_allowed_fields(j, allowed_top_level, "top-level");

        Blueprint bp;
        bp = bp.with_id(interner.intern(j["blueprint_id"].get<std::string>()));
        bp = bp.with_name(j["name"].get<std::string>());

        bp = bp.with_interface(codec_detail::decode_interface(j["interface"], interner));
        bp = codec_detail::decode_nodes(std::move(bp), j["nodes"], interner, parser_registry);
        bp = codec_detail::decode_wires(std::move(bp), j["wires"], interner);
        bp = codec_detail::resolve_wire_domains(std::move(bp), parser_registry, interner);

        auto inv = InvariantChecker::validate(bp, arena, parser_registry, interner);
        if (!inv.valid) {
            if (error_out) {
                error_out->message = inv.error;
            }
            return std::nullopt;
        }
        return bp;
    } catch (std::exception const& e) {
        if (error_out) {
            error_out->message = e.what();
        }
        return std::nullopt;
    }
}

} // namespace bp2
