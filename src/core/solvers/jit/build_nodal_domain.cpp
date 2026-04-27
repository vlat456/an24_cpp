/// Generic nodal domain build pipeline template.
///
/// Parameterized by DomainConfig which provides domain-specific behavior:
/// - domain filter, extractor table, handle assignment, step op traits.
///
/// Three instantiations: electrical, hydraulic, pneumatic.
/// Adding a new nodal domain requires only a new DomainConfig specialization.
///
/// Extraction is delegated to shared templates in element_extraction.h,
/// parameterized by JitExtractionAdapter (strict: throws on missing data).

#include "jit_solver_internal.h"
#include "build_common.h"
#include "../common/signal_key.h"
#include "core/solvers/common/provider.h"
#include "core/solvers/common/element_extraction.h"
// All component headers for step ops (structural typing visits all).
#include "components/azs.h"
#include "components/controlled_voltage_source.h"
#include "components/electrical_conductance.h"
#include "components/electrical_source.h"
#include "components/fuel_tank.h"
#include "components/generator.h"
#include "components/hold_button.h"
#include "components/knob_switch.h"
#include "components/pneumatic_compressor.h"
#include "components/pneumatic_ref.h"
#include "components/pneumatic_valve.h"
#include "components/relay.h"
#include "components/resistor.h"
#include "components/solenoid_valve.h"
#include "components/variable_conductance.h"
#include "../../../parse_number.h"
#include <algorithm>
#include <concepts>
#include <map>
#include <set>
#include <unordered_set>

namespace jit_solver_impl {

using RawElement = build_common::GenericRawElement<NodalElementKind>;

// =====================================================================
// JitExtractionAdapter — satisfies ExtractionAdapter concept.
// Strict: throws on missing params/ports (JIT ports are always allocated).
// Uses InternedId port lookup + per-element bind_handle control.
// =====================================================================

struct JitExtractionAdapter {
    const SolverDevice& dev;
    const PortToSignal& port_to_signal;
    const core::StringInterner& interner;
    bool bind_handle;                     // captured from role.value_map before extraction
    std::vector<RawElement>& out;
    size_t& element_idx;

    float read_param(const SolverRole& role, const char* key, float /*default_val*/) {
        // JIT is strict: ignore default_val, throw on missing.
        return build_common::read_role_param_required(dev, role, key);
    }

    std::optional<uint32_t> resolve_port(const SolverRole& role, const char* key) {
        // JIT is strict: throws on missing port key AND missing signal.
        // Returns optional to satisfy concept — always has_value() or throws.
        return build_common::resolve_role_port(dev, role, key, port_to_signal, interner);
    }

    void emit(NodalElementKind kind, uint32_t node_a, uint32_t node_b,
              float value_a, float value_b) {
        out.push_back({kind, node_a, node_b, value_a, value_b,
            element_idx++, bind_handle ? dev.name : std::string{}});
    }
};

static_assert(build_algo::ExtractionAdapter<JitExtractionAdapter>,
    "JitExtractionAdapter must satisfy ExtractionAdapter concept");

// =====================================================================
// DomainConfig — per-domain behavior traits
// =====================================================================

using JitExtractorEntry = build_algo::ExtractorEntry<JitExtractionAdapter>;

struct DomainConfig {
    Domain domain;
    const char* label;
    const JitExtractorEntry* extractors;
    size_t extractor_count;

    /// Assign handles from raw elements to component variants.
    void (*assign_handles)(
        const std::vector<RawElement>& raw_elements,
        const std::vector<NodalIslandPlan>& islands,
        BuildDeviceStore& devices);

    /// Get this domain's artifacts from BuildResult.
    NodalArtifacts& (*get_artifacts)(BuildResult& result);

    /// Check if a component variant has a handle in this domain.
    bool (*has_handle)(const ComponentVariant& variant);
};

// ---- Electrical config ----

static void assign_electrical_handles(
    const std::vector<RawElement>& raw_elements,
    const std::vector<NodalIslandPlan>& islands,
    BuildDeviceStore& devices)
{
    // Build element_id → device_name lookup
    std::unordered_map<uint32_t, std::string> element_id_to_device;
    element_id_to_device.reserve(raw_elements.size());
    for (const auto& raw_elem : raw_elements) {
        if (!raw_elem.device_name.empty()) {
            element_id_to_device[static_cast<uint32_t>(raw_elem.element_id)] = raw_elem.device_name;
        }
    }

    for (size_t island_idx = 0; island_idx < islands.size(); ++island_idx) {
        const auto& island = islands[island_idx];
        for (size_t elem_idx = 0; elem_idx < island.elements.size(); ++elem_idx) {
            const auto& elem = island.elements[elem_idx];
            auto it_name = element_id_to_device.find(elem.element_id);
            if (it_name == element_id_to_device.end()) continue;

            const std::string& device_name = it_name->second;
            ComponentVariant* variant = devices.find_mutable(device_name);
            if (variant == nullptr) {
                throw std::runtime_error("Electrical handle assignment failed: device '" +
                    device_name + "' not found in result.devices");
            }

            NodalPrimitiveHandle handle;
            handle.island_index = static_cast<uint32_t>(island_idx);
            handle.element_index = static_cast<uint32_t>(elem_idx);
            handle.element_id = elem.element_id;

            std::visit([&](auto& comp) {
                using T = std::decay_t<decltype(comp)>;
                if constexpr (requires { comp.electrical_handles; comp.num_handles; }) {
                    if (comp.num_handles < T::MAX_POSITIONS) {
                        comp.electrical_handles[comp.num_handles++] = handle;
                    }
                }
                else if constexpr (requires { comp.electrical_handle; }) {
                    comp.electrical_handle = handle;
                }
            }, *variant);
        }
    }
}

static NodalArtifacts& get_electrical_artifacts(BuildResult& r) { return r.electrical; }

static bool has_electrical_handle(const ComponentVariant& v) {
    return std::visit([](auto& c) -> bool {
        using T = std::decay_t<decltype(c)>;
        if constexpr (requires { c.electrical_handle; }) return true;
        else if constexpr (requires { c.electrical_handles; }) return true;
        else return false;
    }, v);
}

static const DomainConfig k_electrical_config = {
    Domain::Electrical,
    "Electrical",
    build_algo::k_electrical_extractors<JitExtractionAdapter>,
    std::size(build_algo::k_electrical_extractors<JitExtractionAdapter>),
    &assign_electrical_handles,
    &get_electrical_artifacts,
    &has_electrical_handle,
};

// ---- Hydraulic config ----

static void assign_hydraulic_handles(
    const std::vector<RawElement>& raw_elements,
    const std::vector<NodalIslandPlan>& islands,
    BuildDeviceStore& devices)
{
    build_common::assign_single_handles(
        raw_elements, islands, devices,
        [](NodalPrimitiveHandle handle, ComponentVariant& variant) {
            std::visit([&](auto& comp) {
                using T = std::decay_t<decltype(comp)>;
                if constexpr (requires { comp.hydraulic_handle; }) {
                    comp.hydraulic_handle = handle;
                }
            }, variant);
        }, "Hydraulic");
}

static NodalArtifacts& get_hydraulic_artifacts(BuildResult& r) { return r.hydraulic; }

static bool has_hydraulic_handle(const ComponentVariant& v) {
    return std::visit([](auto& c) -> bool {
        using T = std::decay_t<decltype(c)>;
        return requires { c.hydraulic_handle; };
    }, v);
}

static const DomainConfig k_hydraulic_config = {
    Domain::Hydraulic,
    "Hydraulic",
    build_algo::k_pressure_extractors<JitExtractionAdapter>,
    std::size(build_algo::k_pressure_extractors<JitExtractionAdapter>),
    &assign_hydraulic_handles,
    &get_hydraulic_artifacts,
    &has_hydraulic_handle,
};

// ---- Pneumatic config ----

static void assign_pneumatic_handles(
    const std::vector<RawElement>& raw_elements,
    const std::vector<NodalIslandPlan>& islands,
    BuildDeviceStore& devices)
{
    build_common::assign_single_handles(
        raw_elements, islands, devices,
        [](NodalPrimitiveHandle handle, ComponentVariant& variant) {
            std::visit([&](auto& comp) {
                using T = std::decay_t<decltype(comp)>;
                if constexpr (requires { comp.pneumatic_handle; }) {
                    comp.pneumatic_handle = handle;
                }
            }, variant);
        }, "Pneumatic");
}

static NodalArtifacts& get_pneumatic_artifacts(BuildResult& r) { return r.pneumatic; }

static bool has_pneumatic_handle(const ComponentVariant& v) {
    return std::visit([](auto& c) -> bool {
        using T = std::decay_t<decltype(c)>;
        return requires { c.pneumatic_handle; };
    }, v);
}

static const DomainConfig k_pneumatic_config = {
    Domain::Pneumatic,
    "Pneumatic",
    build_algo::k_pressure_extractors<JitExtractionAdapter>,
    std::size(build_algo::k_pressure_extractors<JitExtractionAdapter>),
    &assign_pneumatic_handles,
    &get_pneumatic_artifacts,
    &has_pneumatic_handle,
};

// =====================================================================
// Generic domain build functions
// =====================================================================

/// Extract raw elements for a domain using shared extraction templates.
static std::vector<RawElement> extract_domain_raw_elements(
    const DomainConfig& config,
    const std::vector<SolverDevice>& devices,
    const PortToSignal& port_to_signal,
    const core::StringInterner& signal_key_interner)
{
    std::vector<RawElement> raw_elements;
    raw_elements.reserve(devices.size());

    size_t element_idx = 0;
    for (const auto& dev : devices) {
        if (!dev.solver_role.has_value()) continue;
        const auto& role = *dev.solver_role;
        if (role.domain != config.domain) continue;

        JitExtractionAdapter adapter{dev, port_to_signal, signal_key_interner,
            build_common::should_bind_handle(role), raw_elements, element_idx};
        if (!build_algo::extract_with_table(
                adapter,
                config.extractors,
                config.extractor_count,
                role)) {
            throw std::runtime_error("Unsupported " + std::string(config.label) +
                " solver_role kind '" + std::string(solver_role_kind_name(role.kind)) +
                "' for component '" + dev.name + "' (classname: " + dev.classname + ")");
        }
    }

    return raw_elements;
}

/// Build islands, assign handles, and generate patch ops for a domain.
static void build_domain_islands(
    const DomainConfig& config,
    BuildResult& result,
    const std::vector<SolverDevice>& devices)
{
    auto raw_elements = extract_domain_raw_elements(
        config, devices, result.port_to_signal, result.signal_key_interner);
    auto& artifacts = config.get_artifacts(result);

    build_common::group_into_islands<RawElement, NodalIslandPlan>(
        raw_elements, artifacts.plan);

    config.assign_handles(raw_elements, artifacts.plan.islands, result.devices);

    // Data-driven patch ops from solver_role.patch_op metadata.
    auto element_id_map = build_common::build_element_id_map(raw_elements);
    build_common::build_patch_ops_from_metadata(
        artifacts.patch_ops, devices, element_id_map,
        result.port_to_signal, result.signal_key_interner);
}

/// Build step ops (execute + commit) for a domain using structural handle typing.
static void build_domain_step_ops(
    const DomainConfig& config,
    BuildResult& result)
{
    auto& artifacts = config.get_artifacts(result);
    artifacts.execute_ops.clear();
    artifacts.commit_ops.clear();

    result.devices.for_each_mutable([&](const std::string&, ComponentVariant& variant) {
        if (!config.has_handle(variant)) return;
        std::visit([&](auto& comp) {
            using T = std::decay_t<decltype(comp)>;
            artifacts.execute_ops.push_back({&comp, &execute_component_adapter<T>});
            artifacts.commit_ops.push_back({&comp, &commit_component_adapter<T>});
        }, variant);
    });
}

// =====================================================================
// Public API — thin wrappers over generic functions
// =====================================================================

void build_electrical_islands(BuildResult& result, const std::vector<SolverDevice>& devices) {
    build_domain_islands(k_electrical_config, result, devices);
}

void build_hydraulic_islands(BuildResult& result, const std::vector<SolverDevice>& devices) {
    build_domain_islands(k_hydraulic_config, result, devices);
}

void build_pneumatic_islands(BuildResult& result, const std::vector<SolverDevice>& devices) {
    build_domain_islands(k_pneumatic_config, result, devices);
}

void build_solver_step_ops(BuildResult& result) {
    build_domain_step_ops(k_electrical_config, result);
}

void build_hydraulic_step_ops(BuildResult& result) {
    build_domain_step_ops(k_hydraulic_config, result);
}

void build_pneumatic_step_ops(BuildResult& result) {
    build_domain_step_ops(k_pneumatic_config, result);
}

}  // namespace jit_solver_impl