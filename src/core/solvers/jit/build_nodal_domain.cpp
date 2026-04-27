/// Generic nodal domain build pipeline template.
///
/// Parameterized by DomainConfig which provides domain-specific behavior:
/// - domain filter, extractor table, handle assignment, step op traits.
///
/// Three instantiations: electrical, hydraulic, pneumatic.
/// Adding a new nodal domain requires only a new DomainConfig specialization.

#include "jit_solver_internal.h"
#include "build_common.h"
#include "../common/signal_key.h"
#include "core/solvers/common/provider.h"
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
using Extractor = build_common::ElementExtractor<RawElement>;

// =====================================================================
// Domain-specific extractor tables
// =====================================================================

// Electrical domain: custom extractors for TheveninSource, ConductanceBranch,
// KnobSwitchBranches (multi-handle), FixedVoltageNode.

static void extract_fixed_voltage_node(
    const SolverDevice& dev, const SolverRole& role,
    const PortToSignal& pts, const core::StringInterner& intern,
    bool bind_handle, std::vector<RawElement>& out, size_t& element_idx)
{
    float value = build_common::read_role_param_required(dev, role, "voltage");
    uint32_t node_a = build_common::resolve_role_port(dev, role, "node", pts, intern);
    out.push_back({NodalElementKind::FixedNode,
        node_a, UINT32_MAX, value, 0.0f,
        element_idx++, bind_handle ? dev.name : std::string{}});
}

static void extract_thevenin_source(
    const SolverDevice& dev, const SolverRole& role,
    const PortToSignal& pts, const core::StringInterner& intern,
    bool bind_handle, std::vector<RawElement>& out, size_t& element_idx)
{
    float voltage = build_common::read_role_param_required(dev, role, "voltage");
    float resistance = build_common::read_role_param_required(dev, role, "resistance");
    uint32_t node_pos = build_common::resolve_role_port(dev, role, "pos", pts, intern);
    uint32_t node_neg = build_common::resolve_role_port(dev, role, "neg", pts, intern);
    out.push_back({NodalElementKind::Source,
        node_pos, node_neg, voltage, resistance,
        element_idx++, bind_handle ? dev.name : std::string{}});
}

static void extract_conductance_branch(
    const SolverDevice& dev, const SolverRole& role,
    const PortToSignal& pts, const core::StringInterner& intern,
    bool bind_handle, std::vector<RawElement>& out, size_t& element_idx)
{
    float conductance = build_common::read_role_param_required(dev, role, "g");
    uint32_t node_a = build_common::resolve_role_port(dev, role, "a", pts, intern);
    uint32_t node_b = build_common::resolve_role_port(dev, role, "b", pts, intern);
    out.push_back({NodalElementKind::Branch,
        node_a, node_b, conductance, 0.0f,
        element_idx++, bind_handle ? dev.name : std::string{}});
}

static void extract_knob_switch_branches(
    const SolverDevice& dev, const SolverRole& role,
    const PortToSignal& pts, const core::StringInterner& intern,
    bool bind_handle, std::vector<RawElement>& out, size_t& element_idx)
{
    int positions = static_cast<int>(build_common::read_role_param_required(dev, role, "positions"));
    positions = std::clamp(positions, 2, KnobSwitch<JitProvider>::MAX_POSITIONS);
    int initial_pos = static_cast<int>(build_common::read_role_param_required(dev, role, "initial_position"));
    initial_pos = std::clamp(initial_pos, 0, positions - 1);
    float g_open_val = build_common::read_role_param_required(dev, role, "g_open");
    float g_closed_val = build_common::read_role_param_required(dev, role, "g_closed");
    uint32_t node_wiper = build_common::resolve_role_port(dev, role, "wiper", pts, intern);

    static_assert(KnobSwitch<JitProvider>::MAX_POSITIONS <= 5,
                  "KnobSwitch terminal list supports up to 5 throws");
    static const char* terminal_names[] = {"throw1", "throw2", "throw3", "throw4", "throw5"};

    for (int i = 0; i < positions; ++i) {
        uint32_t node_t = build_common::resolve_role_port(dev, role, terminal_names[i], pts, intern);
        float initial_g = (i == initial_pos) ? g_closed_val : g_open_val;
        out.push_back({NodalElementKind::Branch,
            node_wiper, node_t, initial_g, 0.0f,
            element_idx++, bind_handle ? dev.name : std::string{}});
    }
}

static const Extractor k_electrical_extractors[] = {
    {SolverRoleKind::FixedVoltageNode,    &extract_fixed_voltage_node},
    {SolverRoleKind::TheveninSource,      &extract_thevenin_source},
    {SolverRoleKind::ConductanceBranch,   &extract_conductance_branch},
    {SolverRoleKind::KnobSwitchBranches,  &extract_knob_switch_branches},
};

// Pressure domains (hydraulic + pneumatic): shared extractors.

static const Extractor k_pressure_extractors[] = {
    {SolverRoleKind::FixedPressureNode, &build_common::extract_fixed_pressure_node<RawElement>},
    {SolverRoleKind::PressureSource,    &build_common::extract_pressure_source<RawElement>},
    {SolverRoleKind::FlowBranch,        &build_common::extract_flow_branch<RawElement>},
};

// =====================================================================
// DomainConfig — per-domain behavior traits
// =====================================================================

struct DomainConfig {
    Domain domain;
    const char* label;
    const Extractor* extractors;
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

    // Build device_name → element count (for multi-handle indexing)
    std::unordered_map<std::string, int> device_element_count;

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
    k_electrical_extractors,
    std::size(k_electrical_extractors),
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
    k_pressure_extractors,
    std::size(k_pressure_extractors),
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
    k_pressure_extractors,
    std::size(k_pressure_extractors),
    &assign_pneumatic_handles,
    &get_pneumatic_artifacts,
    &has_pneumatic_handle,
};

// =====================================================================
// Generic domain build functions
// =====================================================================

/// Extract raw elements for a domain using its config's extractor table.
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

        const auto* extractor = build_common::find_extractor(
            config.extractors, config.extractor_count, role.kind);
        if (!extractor) {
            throw std::runtime_error("Unsupported " + std::string(config.label) +
                " solver_role kind '" + std::string(solver_role_kind_name(role.kind)) +
                "' for component '" + dev.name + "' (classname: " + dev.classname + ")");
        }

        extractor->extract(dev, role, port_to_signal, signal_key_interner,
                           build_common::should_bind_handle(role), raw_elements, element_idx);
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
