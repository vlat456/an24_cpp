#include "blueprint_codec.h"
#include "blueprint_codec_internal.h"
#include "blueprint_v2/validation/invariant_checker.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <unordered_set>

namespace bp2 {

std::string BlueprintCodec::encode(Blueprint const& bp,
                                   ui::StringInterner const& interner,
                                   PathArena const& arena,
                                   const ::TypeRegistry* parser_registry) {
    nlohmann::json j;
    const TypeDefinition* type_def = nullptr;
    if (parser_registry && !bp.id().empty()) {
        type_def = parser_registry->get(std::string(interner.resolve(bp.id())));
    }

    j["version"] = "3.0";
    j["id"] = std::string(interner.resolve(bp.id()));
    j["display_name"] = bp.display_name();
    j["interface"] = codec_detail::encode_interface(bp.iface(), interner, type_def);
    j["nodes"] = codec_detail::encode_nodes(bp.nodes(), interner, parser_registry);
    j["wires"] = codec_detail::encode_wires(bp.wires(), interner, arena);
    j["nested"] = codec_detail::encode_nested(bp.nested(), interner, arena, parser_registry);

    if (type_def) {
        j["cpp_class"] = type_def->cpp_class;
        j["description"] = type_def->description;
        j["scheduler_source"] = type_def->scheduler_source;

        nlohmann::json domains = nlohmann::json::array();
        for (Domain d : type_def->domains.value_or(std::vector<Domain>{})) {
            domains.push_back(codec_detail::domain_to_string(d));
        }
        j["domains"] = std::move(domains);

        if (!type_def->params.empty()) {
            nlohmann::json params = nlohmann::json::object();
            for (const auto& [k, v] : type_def->params) {
                params[k] = v;
            }
            j["param_defaults"] = std::move(params);
        }
    }

    j["pan_x"] = bp.pan_x();
    j["pan_y"] = bp.pan_y();
    j["zoom"] = bp.zoom();
    j["grid_step"] = bp.grid_step();
    if (!bp.name().empty()) {
        j["name"] = bp.name();
    }
    return j.dump(2);
}

std::optional<Blueprint> BlueprintCodec::decode(
    std::string_view json_str,
    ui::StringInterner& interner,
    PathArena& arena,
    const ::TypeRegistry& parser_registry,
    DecodeError* error_out) {
    try {
        auto j = nlohmann::json::parse(json_str);
        if (!j.contains("version") || !j["version"].is_string()
            || j["version"].get<std::string>() != "3.0") {
            if (error_out) {
                error_out->message = "Unsupported blueprint version (expected \"3.0\")";
            }
            return std::nullopt;
        }

        if (!j.contains("id") || !j["id"].is_string()) {
            if (error_out) {
                error_out->message = "Missing required string field: id";
            }
            return std::nullopt;
        }
        if (!j.contains("display_name") || !j["display_name"].is_string()) {
            if (error_out) {
                error_out->message = "Missing required string field: display_name";
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
        if (!j.contains("nested") || !j["nested"].is_array()) {
            if (error_out) {
                error_out->message = "Missing required array field: nested";
            }
            return std::nullopt;
        }

        static const std::unordered_set<std::string> allowed_top_level = {
            "version", "id", "display_name", "name", "interface", "nodes",
            "wires", "nested", "pan_x", "pan_y", "zoom", "grid_step",
            "cpp_class", "description", "domains", "scheduler_source",
            "param_defaults", "param_schema", "solver_role", "priority", "critical"
        };
        codec_detail::check_allowed_fields(j, allowed_top_level, "top-level");

        Blueprint bp;
        bp = bp.with_id(interner.intern(j["id"].get<std::string>()));
        bp = bp.with_display_name(j["display_name"].get<std::string>());
        if (j.contains("name") && !j["name"].is_string()) {
            if (error_out) {
                error_out->message = "invalid top-level field type: name";
            }
            return std::nullopt;
        }
        if (j.contains("name")) {
            bp = bp.with_name(j["name"].get<std::string>());
        }

        bp = bp.with_interface(codec_detail::decode_interface(j["interface"], interner));
        bp = codec_detail::decode_nodes(std::move(bp), j["nodes"], interner, parser_registry);
        bp = codec_detail::decode_wires(std::move(bp), j["wires"], interner, arena);
        bp = codec_detail::decode_nested(std::move(bp), j["nested"], interner, parser_registry, arena);

        auto inv = InvariantChecker::validate(bp, arena, parser_registry, interner);
        if (!inv.valid) {
            if (error_out) {
                error_out->message = inv.error;
            }
            return std::nullopt;
        }

        auto viewport_or_default = [&](const char* key, float default_value) -> float {
            if (!j.contains(key)) {
                return default_value;
            }
            if (!j[key].is_number()) {
                throw std::runtime_error(std::string("invalid viewport field type: ") + key);
            }
            return j[key].get<float>();
        };

        const float pan_x = viewport_or_default("pan_x", 0.0f);
        const float pan_y = viewport_or_default("pan_y", 0.0f);
        const float zoom = viewport_or_default("zoom", 1.0f);
        const float grid_step = viewport_or_default("grid_step", 16.0f);
        if (!std::isfinite(pan_x) || !std::isfinite(pan_y)
            || !std::isfinite(zoom) || !std::isfinite(grid_step)) {
            if (error_out) {
                error_out->message = "invalid non-finite viewport value";
            }
            return std::nullopt;
        }
        if (zoom <= 0.0f) {
            if (error_out) {
                error_out->message = "invalid viewport zoom: must be > 0";
            }
            return std::nullopt;
        }
        if (zoom > 1000.0f) {
            if (error_out) {
                error_out->message = "invalid viewport zoom: exceeds maximum";
            }
            return std::nullopt;
        }
        if (grid_step <= 0.0f) {
            if (error_out) {
                error_out->message = "invalid viewport grid_step: must be > 0";
            }
            return std::nullopt;
        }
        if (grid_step > 10000.0f) {
            if (error_out) {
                error_out->message = "invalid viewport grid_step: exceeds maximum";
            }
            return std::nullopt;
        }
        bp = bp.with_viewport(pan_x, pan_y, zoom, grid_step);
        return bp;
    } catch (std::exception const& e) {
        if (error_out) {
            error_out->message = e.what();
        }
        return std::nullopt;
    }
}

} // namespace bp2
