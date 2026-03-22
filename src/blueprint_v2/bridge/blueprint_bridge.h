#pragma once
#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/path/path.h"
#include "ui/core/interned_id.h"

// Forward declarations for old types
class FlatBlueprint;
class TypeDefinition;

namespace bp2 {

class BlueprintBridge {
public:
    /// Convert old FlatBlueprint to new Blueprint
    static Blueprint from_flat(FlatBlueprint const& flat,
                               ui::StringInterner& interner);

    /// Convert new Blueprint to old FlatBlueprint (for gradual rollout).
    /// arena must be the PathArena that was used to create the wire paths in bp.
    static FlatBlueprint to_flat(Blueprint const& bp,
                                 ui::StringInterner& interner,
                                 PathArena& arena);

    /// Convert old TypeDefinition to new Blueprint
    static Blueprint from_type_def(TypeDefinition const& td,
                                   ui::StringInterner& interner,
                                   class TypeRegistry& registry);

    /// Convert new Blueprint to old TypeDefinition
    static TypeDefinition to_type_def(Blueprint const& bp,
                                      ui::StringInterner& interner);
};

} // namespace bp2

