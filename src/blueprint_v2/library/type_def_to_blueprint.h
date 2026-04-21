#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/interface/type_definition_interface.h"
#include "core/model/component_registry.h"
#include "core/model/component_spec.h"
#include "ui/core/interned_id.h"

#include <stdexcept>
#include <string>

namespace bp2 {

/// Build a bp2::Blueprint from a composite TypeDefinition (cpp_class == false).
///
/// Maps TypeDefinition.devices → Node (Kind::Component) and
/// TypeDefinition.connections → Wire, using the existing
/// interface_from_type_definition() helper for the blueprint interface.
///
/// Node interfaces are resolved from the ComponentRegistry so that flattening
/// and wire validation can see each node's ports.
///
/// This is the canonical path for materialising library composite blueprints
/// into the bp2 model.  Library .blueprint files are v3 type-definition assets
/// and must NOT be loaded through the strict v1 blueprint codec.
///
/// Throws std::runtime_error if:
///   - spec is not a CompositeSpec (primitive)
///   - a connection string cannot be parsed as "node.port"
Blueprint blueprint_from_type_definition(const ComponentSpec& spec,
                                         ui::StringInterner& interner,
                                         const ComponentRegistry& registry);

} // namespace bp2
