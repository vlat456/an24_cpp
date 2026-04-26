#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/path/path.h"
#include <string>
#include <vector>

struct ComponentRegistry;

namespace bp2::diagnostics {

struct IntegrityIssue {
    enum class Kind {
        UnknownNodeType,
        UnknownNestedBlueprint,
        EmbeddedNestedMissingDefinition,
        DuplicateNodeId,
        DuplicateWireId,
        DuplicateNestedId,
        InvalidWireEndpoint,
    };

    Kind kind;
    std::string message;
};

struct RepairReport {
    std::vector<IntegrityIssue> issues;
    size_t removed_wires = 0;
    bool changed = false;
};

RepairReport diagnose_and_repair(Blueprint& bp,
                                 PathArena& arena,
                                 const ::ComponentRegistry& parser_registry,
                                 core::StringInterner& interner);

} // namespace bp2::diagnostics
