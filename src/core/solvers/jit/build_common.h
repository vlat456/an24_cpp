#pragma once

/// JIT-specific build helpers for nodal domain pipelines.
///
/// Extends the pure-algorithm layer in build_algorithms.h with:
/// - Port/param resolution (InternedId-based, strict: throws on missing)
/// - Handle assignment (single + electrical multi-handle)
/// - Patch op context adapter (JitPatchOpContext)
///
/// Extraction is now in element_extraction.h — parameterized by ExtractionAdapter.
/// JIT uses JitExtractionAdapter (in build_nodal_domain.cpp).

#include "jit_solver.h"
#include "core/solvers/common/signal_key.h"
#include "core/solvers/common/build_algorithms.h"
#include "core/solvers/common/nodal_patch_convert.h"
#include "../../../parse_number.h"
#include <algorithm>
#include <map>
#include <set>
#include <unordered_set>

namespace jit_solver_impl {
namespace build_common {

// =====================================================================
// Re-export shared algorithms into build_common namespace.
// Zero-cost — existing callers see the same names.
// =====================================================================
using build_algo::GenericRawElement;
using build_algo::group_into_islands;
using build_algo::init_element_values_from_plan;
using build_algo::build_element_id_map;

// =====================================================================
// Port and param resolution — strict (throws on missing).
// Used by JitExtractionAdapter and patch op context.
// =====================================================================

inline uint32_t resolve_port(
    const SolverDevice& dev,
    const std::string& port_name,
    const PortToSignal& port_to_signal,
    const core::StringInterner& signal_key_interner)
{
    const std::string full_port = signal_key::make_node_port_key(dev.name, port_name);
    const core::InternedId key = signal_key_interner.lookup(full_port);
    if (key.empty()) {
        throw std::runtime_error("Port '" + full_port +
            "' not interned for component '" + dev.name + "' (classname: " + dev.classname +
            ") — signal key was never registered during allocation");
    }
    auto it = port_to_signal.find(key);
    if (it == port_to_signal.end()) {
        throw std::runtime_error("Interned port '" + full_port +
            "' has no signal mapping for component '" + dev.name + "' (classname: " + dev.classname +
            ") — interner/port_to_signal desync");
    }
    return it->second;
}

inline float read_param_float_required(
    const SolverDevice& dev,
    const std::string& param_key,
    const std::string& role_key)
{
    auto it = dev.params.find(param_key);
    if (it == dev.params.end()) {
        throw std::runtime_error(
            "solver_role references missing param '" + param_key + "' via key '" + role_key +
            "' for component '" + dev.name + "' (classname: " + dev.classname + ")");
    }
    return locale_safe::parse_float_or(it->second, 0.0f);
}

inline uint32_t resolve_role_port(
    const SolverDevice& dev,
    const SolverRole& role,
    const std::string& role_key,
    const PortToSignal& port_to_signal,
    const core::StringInterner& signal_key_interner)
{
    auto it = role.port_map.find(role_key);
    if (it == role.port_map.end()) {
        throw std::runtime_error("solver_role missing required port key '" + role_key +
            "' for component '" + dev.name + "' (classname: " + dev.classname + "'");
    }
    return resolve_port(dev, it->second, port_to_signal, signal_key_interner);
}

inline float read_role_param_required(
    const SolverDevice& dev,
    const SolverRole& role,
    const std::string& role_key)
{
    auto it = role.param_map.find(role_key);
    if (it != role.param_map.end()) {
        return read_param_float_required(dev, it->second, role_key);
    }
    auto it_val = role.value_map.find(role_key);
    if (it_val != role.value_map.end()) {
        return it_val->second;
    }
    throw std::runtime_error("solver_role missing required param key '" + role_key +
        "' for component '" + dev.name + "' (classname: " + dev.classname + "'");
}

inline bool should_bind_handle(const SolverRole& role) {
    auto it = role.value_map.find("bind_handle");
    return it != role.value_map.end() && it->second > 0.5f;
}

// =====================================================================
// Element ID → device name lookup — shared by all handle assignment.
// =====================================================================

/// Build O(1) lookup: element_id → device_name (skips empty device names).
inline std::unordered_map<uint32_t, std::string> build_element_id_to_device(
    const std::vector<GenericRawElement<NodalElementKind>>& raw_elements)
{
    std::unordered_map<uint32_t, std::string> map;
    map.reserve(raw_elements.size());
    for (const auto& raw_elem : raw_elements) {
        if (!raw_elem.device_name.empty()) {
            map[static_cast<uint32_t>(raw_elem.element_id)] = raw_elem.device_name;
        }
    }
    return map;
}

// =====================================================================
// Generic single-handle assignment — used by hydraulic and pneumatic.
//
// Walks island elements, matches those with bound device names back to
// the component variant, and sets the domain-specific handle member via
// a visitor callback.
// =====================================================================

/// Assign handles from island elements to component variants.
/// `handle_setter` is a callable: void(NodalPrimitiveHandle, ComponentVariant&)
/// `domain_label` is used in error messages (e.g., "Hydraulic").
template<typename HandleSetter>
void assign_single_handles(
    const std::vector<GenericRawElement<NodalElementKind>>& raw_elements,
    const std::vector<NodalIslandPlan>& islands,
    BuildDeviceStore& devices,
    HandleSetter&& handle_setter,
    const char* domain_label)
{
    const auto element_id_to_device = build_element_id_to_device(raw_elements);

    for (size_t island_idx = 0; island_idx < islands.size(); ++island_idx) {
        const auto& island = islands[island_idx];
        for (size_t elem_idx = 0; elem_idx < island.elements.size(); ++elem_idx) {
            const auto& elem = island.elements[elem_idx];
            auto it_name = element_id_to_device.find(elem.element_id);
            if (it_name == element_id_to_device.end()) continue;

            const std::string& device_name = it_name->second;
            ComponentVariant* variant = devices.find_mutable(device_name);
            if (variant == nullptr) {
                throw std::runtime_error(std::string(domain_label) +
                    " handle assignment failed: device '" + device_name +
                    "' not found in result.devices");
            }

            NodalPrimitiveHandle handle;
            handle.island_index = static_cast<uint32_t>(island_idx);
            handle.element_index = static_cast<uint32_t>(elem_idx);
            handle.element_id = elem.element_id;

            handle_setter(handle, *variant);
        }
    }
}

} // namespace build_common

// =====================================================================
// JIT patch op context — adapts JIT types for the generic builder.
// =====================================================================

namespace build_common {

/// Resolve a signal index from device name + port name.
/// Returns UINT32_MAX if not found (caller decides whether to skip).
inline uint32_t resolve_port_signal(
    const std::string& device_name,
    const std::string& port_name,
    const PortToSignal& port_to_signal,
    const core::StringInterner& interner)
{
    const std::string full_key = signal_key::make_node_port_key(device_name, port_name);
    const core::InternedId id = interner.lookup(full_key);
    if (id.empty()) return UINT32_MAX;
    auto it = port_to_signal.find(id);
    return (it != port_to_signal.end()) ? it->second : UINT32_MAX;
}

/// Resolve a float param from a SolverDevice. Returns default_val if not found.
inline float resolve_param_float(
    const SolverDevice& dev,
    const std::string& param_name,
    float default_val)
{
    auto it = dev.params.find(param_name);
    if (it == dev.params.end()) return default_val;
    return locale_safe::parse_float_or(it->second, default_val);
}

/// Fill a NodalPatchOp from PatchOpDecl metadata.
/// Resolves signal ports from port_to_signal and constant values from device params.
inline void fill_patch_op_from_decl(
    NodalPatchOp& op,
    const PatchOpDecl& decl,
    const SolverDevice& dev,
    const PortToSignal& port_to_signal,
    const core::StringInterner& interner)
{
    // Resolve signal ports → s0..s4
    const size_t n_signals = std::min(decl.signal_ports.size(), size_t(5));
    uint32_t* targets[] = { &op.s0, &op.s1, &op.s2, &op.s3, &op.s4 };
    for (size_t i = 0; i < n_signals; ++i) {
        *targets[i] = resolve_port_signal(
            dev.name, decl.signal_ports[i], port_to_signal, interner);
    }

    // Resolve constant output values from device params.
    // For BoolSwitch/IndexSwitch: true_value_param and false_value_param.
    if (!decl.true_value_param.empty()) {
        op.state_true_value = resolve_param_float(dev, decl.true_value_param, 0.0f);
    }
    if (!decl.false_value_param.empty()) {
        op.state_false_value = resolve_param_float(dev, decl.false_value_param, 0.0f);
    }
}

/// JIT-specific context adapter for the generic patch op builder.
struct JitPatchOpContext {
    const std::vector<SolverDevice>& devices;
    const std::unordered_map<std::string, uint32_t>& element_id_map;
    const PortToSignal& port_to_signal;
    const core::StringInterner& interner;

    size_t device_count() const { return devices.size(); }

    bool has_patch_op(size_t i) const {
        return devices[i].solver_role.has_value() &&
               devices[i].solver_role->patch_op.has_value();
    }

    const PatchOpDecl& patch_op_decl(size_t i) const {
        return *devices[i].solver_role->patch_op;
    }

    std::string device_element_key(size_t i, int handle_index = -1) const {
        const std::string& name = devices[i].name;
        return handle_index >= 0
            ? name + "_" + std::to_string(handle_index)
            : name;
    }

    uint32_t lookup_element_id(const std::string& key) const {
        auto it = element_id_map.find(key);
        return (it != element_id_map.end()) ? it->second : UINT32_MAX;
    }

    void fill_signal_ports(NodalPatchOp& op, const PatchOpDecl& decl, size_t i) const {
        fill_patch_op_from_decl(op, decl, devices[i], port_to_signal, interner);
    }
};

/// Build patch ops from solver_role metadata — zero component visitation.
/// Delegates to the generic builder via JitPatchOpContext.
inline void build_patch_ops_from_metadata(
    std::vector<NodalPatchOp>& patch_ops,
    const std::vector<SolverDevice>& devices,
    const std::unordered_map<std::string, uint32_t>& element_id_map,
    const PortToSignal& port_to_signal,
    const core::StringInterner& interner)
{
    JitPatchOpContext ctx{devices, element_id_map, port_to_signal, interner};
    build_algo::build_patch_ops_generic(patch_ops, ctx);
}

} // namespace build_common
} // namespace jit_solver_impl