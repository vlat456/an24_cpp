#pragma once

/// v2 ↔ v1 conversion functions for library type definitions.
///
/// type_definition_to_v2()  — TypeDefinition → BlueprintV2  (for serialization)
/// v2_to_type_definition()  — BlueprintV2 → TypeDefinition  (for existing consumers)

#include "blueprint_v2.h"
#include "json_parser.h"

namespace v2 {

/// Convert a v1 TypeDefinition to a v2 BlueprintV2.
/// Handles both C++ components (cpp_class=true) and composites (cpp_class=false).
BlueprintV2 type_definition_to_v2(const TypeDefinition& td);

/// Convert a v2 BlueprintV2 back to a v1 TypeDefinition.
/// Used by load_type_registry() so existing consumers (codegen, simulation,
/// expand_sub_blueprint_references) can work unchanged.
TypeDefinition v2_to_type_definition(const BlueprintV2& bp);

} // namespace v2
