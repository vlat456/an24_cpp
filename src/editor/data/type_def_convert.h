#pragma once

/// Flat ↔ v1 conversion functions for library type definitions.
///
/// type_definition_to_flat()  — TypeDefinition → FlatBlueprint  (for serialization)
/// flat_to_type_definition()  — FlatBlueprint → TypeDefinition  (for existing consumers)

#include "editor/data/flat_blueprint.h"
#include "json_parser.h"

/// Convert a v1 TypeDefinition to a FlatBlueprint.
/// Handles both C++ components (cpp_class=true) and composites (cpp_class=false).
FlatBlueprint type_definition_to_flat(const TypeDefinition& td);

/// Convert a FlatBlueprint back to a v1 TypeDefinition.
/// Used by load_type_registry() so existing consumers (codegen, simulation,
/// expand_sub_blueprint_references) can work unchanged.
TypeDefinition flat_to_type_definition(const FlatBlueprint& bp);
