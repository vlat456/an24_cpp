#pragma once

#include "core/model/component_registry.h"
#include "core/model/component_spec.h"
#include "core/model/resolved_device.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

/// Patch operation kind for AOT codegen.
/// Mirrors NodalPatchKind from jit_solver.h — kept separate to avoid
/// pulling the full JIT dependency chain into the codegen tool.
enum class PatchKindCodegen : uint8_t {
    AffineClamp = 0,
    LerpClamped01 = 1,
    BoolSwitch = 2,
    IndexSwitch = 3,
    CopySignal = 4
};

/// Patch operation for AOT codegen.
/// Mirrors NodalPatchOp from jit_solver.h — signal-driven, fully resolved
/// at code generation time. Emitted as constexpr NodalPatchOp in generated code.
struct PatchOpCodegen {
    PatchKindCodegen kind = PatchKindCodegen::BoolSwitch;
    uint32_t element_id = UINT32_MAX;
    uint32_t s0 = UINT32_MAX;
    uint32_t s1 = UINT32_MAX;
    uint32_t s2 = UINT32_MAX;
    uint32_t s3 = UINT32_MAX;
    uint32_t s4 = UINT32_MAX;
    int index_value = 0;
    float state_true_value = 0.0f;
    float state_false_value = 0.0f;
};

/// Result of composite code generation
struct CompositeCodegenResult {
    std::string header;
    std::string source;
    std::string class_name;
};

// == Electrical plan for AOT codegen ==
// Codegen-phase plan types. Element kinds map 1:1 to runtime NodalElementKind,
// but keep domain-specific names for codegen clarity. The generated C++ output
// uses unified runtime types (NodalElementKind, NodalElement, NodalIslandPlan).
// Used to emit constexpr electrical island data in generated code.

enum class ElectricalElementKindCodegen : uint8_t {
    FixedVoltageNode = 0,
    TheveninSource = 1,
    ConductanceBranch = 2
};

struct ElectricalElementCodegen {
    ElectricalElementKindCodegen kind;
    uint32_t node_a;
    uint32_t node_b;
    float value_a;
    float value_b;
    uint32_t element_id;
};

struct ElectricalIslandPlanCodegen {
    std::vector<uint32_t> signal_indices;
    std::vector<ElectricalElementCodegen> elements;
};

struct ElectricalPlanCodegen {
    struct ComponentDebug {
        uint32_t element_id;
        uint32_t island_index;
        uint32_t element_index;
        std::string device_name;
        std::string device_classname;
        std::string role;
        uint32_t node_a;
        uint32_t node_b;
    };

    std::vector<ElectricalIslandPlanCodegen> islands;
    struct DeviceBinding {
        std::string device_field_name;
        uint32_t island_index;
        uint32_t element_index;
        uint32_t element_id;
    };
    std::vector<DeviceBinding> device_bindings;
    std::vector<PatchOpCodegen> patch_ops;
    std::vector<ComponentDebug> component_debug;
};

struct ElectricalExtractOptions {
    bool strict_port_resolution = false;
    bool warn_on_missing_ports = true;
};

/// Extract electrical island plan from devices and port_to_signal mapping.
/// Mirrors the island extraction logic in build_systems_dev().
ElectricalPlanCodegen extract_electrical_plan(
    const std::vector<ResolvedDevice>& devices,
    const std::unordered_map<std::string, uint32_t>& port_to_signal,
    const ElectricalExtractOptions& options = {}
);

/// Code generator - produces C++ source files from device configuration
class CodeGen {
public:
    /// Generate C++ header file with Systems struct
    static std::string generate_header(
        const std::string& source_file,
        const std::vector<ResolvedDevice>& devices,
        const std::unordered_map<std::string, uint32_t>& port_to_signal,
        uint32_t signal_count,
        const std::string& class_name = "Systems",
        const ElectricalPlanCodegen& electrical_plan = {}
    );

    /// Generate C++ source file with implementations
    static std::string generate_source(
        const std::string& header_name,
        const std::vector<ResolvedDevice>& devices,
        const std::unordered_map<std::string, uint32_t>& port_to_signal,
        uint32_t signal_count,
        const std::string& class_name = "Systems",
        const ElectricalPlanCodegen& electrical_plan = {}
    );

    /// Write generated files to directory
    static void write_files(
        const std::string& out_dir,
        const std::string& source_file,
        const std::vector<ResolvedDevice>& devices,
        const std::unordered_map<std::string, uint32_t>& port_to_signal,
        uint32_t signal_count,
        const ElectricalPlanCodegen& electrical_plan = {}
    );

    /// Generate port registry header from ComponentRegistry
    static void generate_port_registry(const ComponentRegistry& registry, const std::string& output_path);

    /// Generate build_factory.cpp from ComponentRegistry.
    /// Replaces hand-written build_components*.cpp switch files.
    static void generate_build_factory(const ComponentRegistry& registry, const std::string& output_path);

    /// Generate component_kind.h from ComponentRegistry.
    /// Emits enum, parse_component_kind(), component_kind_classname(), and family predicates.
    /// Eliminates the last manual sync point when adding new components.
    static void generate_component_kind(const ComponentRegistry& registry, const std::string& output_path);

    /// Generate Systems class for a composite blueprint.
    /// Expands sub-blueprint references into flat devices + connections,
    /// runs union-find signal allocation, then delegates to generate_header/source.
    /// Produces fully branchless ECS-like code identical to flat codegen.
    /// Throws on cycles or missing types.
    static CompositeCodegenResult generate_composite_systems(
        const CompositeSpec& td,
        const ComponentRegistry& registry);

    /// Generate all composites in topological order (leaves first).
    /// Returns map: classname → {header, source}.
    static std::map<std::string, CompositeCodegenResult> generate_all_composites(
        const ComponentRegistry& registry);
};
