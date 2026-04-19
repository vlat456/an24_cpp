#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/path/path.h"
#include "ui/core/interned_id.h"
#include <optional>
#include <string>

namespace ui { class StringInterner; }
struct ComponentRegistry;

/// Save blueprint to file using bp2 codec.
[[nodiscard]] bool save_blueprint_to_file(const bp2::Blueprint& bp,
                                           ui::StringInterner& interner,
                                           bp2::PathArena const& arena,
                                           const ComponentRegistry& parser_registry,
                                           const char* path);

/// Load blueprint from file using bp2 codec.
[[nodiscard]] std::optional<bp2::Blueprint> load_blueprint_from_file(
    const char* path,
    ui::StringInterner& interner,
    bp2::PathArena& arena,
    const ComponentRegistry& parser_registry);

/// Strict load variant: additionally validates loaded blueprint invariants.
[[nodiscard]] std::optional<bp2::Blueprint> load_blueprint_from_file_validated(
    const char* path,
    ui::StringInterner& interner,
    bp2::PathArena& arena,
    const ComponentRegistry& parser_registry);

/// Editor/runtime load variant: strict decode + validation followed by the
/// single explicit runtime node-view hydration step.
[[nodiscard]] std::optional<bp2::Blueprint> load_hydrated_blueprint_from_file(
    const char* path,
    ui::StringInterner& interner,
    bp2::PathArena& arena,
    const ComponentRegistry& parser_registry);

/// Shared integrity validation (bp2 invariants against runtime registry).
[[nodiscard]] bool validate_blueprint_integrity(
    const bp2::Blueprint& bp,
    ui::StringInterner& interner,
    const bp2::PathArena& arena,
    const ComponentRegistry& parser_registry,
    std::string* error_out = nullptr);

/// Validate a blueprint with bp2 invariants and parser type registry checks.
[[nodiscard]] bool validate_blueprint_for_persist(
    const bp2::Blueprint& bp,
    ui::StringInterner& interner,
    const bp2::PathArena& arena,
    const ComponentRegistry& parser_registry,
    std::string* error_out = nullptr);
