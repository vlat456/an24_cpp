/// Cross-path signal allocation equivalence test (Issue #227).
///
/// Proves that the Flattener path (Blueprint → flatten → elaborate_for_jit)
/// and the JIT direct signal_alloc pipeline (ResolvedDevice + Connection →
/// build_port_index_map → apply_signal_allocation_rules → finalize_signal_indices)
/// produce identical signal topology for the same logical circuit.
///
/// The Flattener assigns compact indices in first-encounter order;
/// finalize_signal_indices assigns them in sorted-root order.
/// Absolute indices may differ, but the *grouping* (which ports share a signal)
/// must be identical. We compare via set<set<string>> topology extraction.

#include <gtest/gtest.h>

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/elaboration/sim_export.h"
#include "blueprint_v2/flattener/flattener.h"
#include "blueprint_v2/flattener/flat_netlist.h"
#include "blueprint_v2/library/blueprint_library.h"
#include "blueprint_v2/library/type_def_to_blueprint.h"
#include "blueprint_v2/path/path.h"
#include "core/solvers/common/signal_allocation.h"
#include "core/solvers/jit/jit_solver.h"
#include "core/registry/component_resolution.h"
#include "io/json/component_registry_json_loader.h"
#include "ui/core/interned_id.h"
#include "bp2_test_helpers.h"
#include "jit_build_input_test_helper.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

// == Topology extraction ==

/// Extract signal topology as set<set<string>> from a string-keyed port_to_signal.
/// Only includes groups with 2+ ports (actual connections).
std::set<std::set<std::string>> extract_topology_from_string_map(
    const std::unordered_map<std::string, uint32_t>& p2s)
{
    std::map<uint32_t, std::set<std::string>> groups;
    for (const auto& [port_str, sig] : p2s) {
        groups[sig].insert(port_str);
    }

    std::set<std::set<std::string>> topo;
    for (auto& [_, ports] : groups) {
        if (ports.size() > 1) {
            topo.insert(std::move(ports));
        }
    }
    return topo;
}

/// Extract signal topology from a PortToSignal (InternedId-keyed) map.
std::set<std::set<std::string>> extract_topology(
    const PortToSignal& p2s,
    const ui::StringInterner& interner)
{
    std::map<uint32_t, std::set<std::string>> groups;
    for (const auto& [port_id, sig] : p2s) {
        groups[sig].insert(std::string(interner.resolve(port_id)));
    }

    std::set<std::set<std::string>> topo;
    for (auto& [_, ports] : groups) {
        if (ports.size() > 1) {
            topo.insert(std::move(ports));
        }
    }
    return topo;
}

// == Reconstruct JIT path input from FlatNetlist ==

/// Reconstruct connections from a signal topology.
/// For each signal group with 2+ ports, creates connections between the first
/// port and all others (star topology). The union rules don't care about
/// direction — they just unite port pairs.
std::vector<Connection> reconstruct_connections(
    const PortToSignal& p2s,
    const ui::StringInterner& interner)
{
    std::map<uint32_t, std::vector<std::string>> groups;
    for (const auto& [port_id, sig] : p2s) {
        groups[sig].push_back(std::string(interner.resolve(port_id)));
    }

    std::vector<Connection> connections;
    for (auto& [_, ports] : groups) {
        if (ports.size() < 2) continue;
        for (size_t i = 1; i < ports.size(); ++i) {
            connections.push_back(Connection{ports[0], ports[i]});
        }
    }
    return connections;
}

/// Run the signal_alloc pipeline directly (Path B: JIT UnionFind path).
/// Produces a string-keyed port_to_signal map.
std::unordered_map<std::string, uint32_t> run_signal_alloc_pipeline(
    const std::vector<ResolvedDevice>& devices,
    const std::vector<BridgePortDefinition>& bridges,
    const std::vector<Connection>& connections,
    uint32_t& out_signal_count)
{
    std::vector<std::string> all_ports;
    std::unordered_map<std::string, uint32_t> port_to_idx;
    signal_alloc::build_port_index_map(devices, bridges, all_ports, port_to_idx);

    if (all_ports.empty()) {
        out_signal_count = 1;
        return {};
    }

    signal_alloc::UnionFind uf(all_ports.size());
    signal_alloc::apply_signal_allocation_rules(uf, devices, bridges, connections, port_to_idx);

    return signal_alloc::finalize_signal_indices(uf, all_ports, port_to_idx, out_signal_count);
}

// == Shared test library ==

bp2::BlueprintLibrary make_test_library(ui::StringInterner& interner) {
    bp2::BlueprintLibrary library;

    bp2::Blueprint bat;
    bat = bat.with_interface(bp2::Interface({
        {interner.intern("v_in"), Domain::Electrical, bp2::Direction::Input},
        {interner.intern("v_out"), Domain::Electrical, bp2::Direction::Output},
    }));
    library.add(interner.intern("Battery"), bat);

    bp2::Blueprint res;
    res = res.with_interface(bp2::Interface({
        {interner.intern("in"), Domain::Electrical, bp2::Direction::Input},
        {interner.intern("out"), Domain::Electrical, bp2::Direction::Output},
    }));
    library.add(interner.intern("Resistor"), res);

    bp2::Blueprint gnd;
    gnd = gnd.with_interface(bp2::Interface({
        {interner.intern("gnd"), Domain::Electrical, bp2::Direction::InOut},
    }));
    library.add(interner.intern("Ground"), gnd);

    bp2::Blueprint led;
    led = led.with_interface(bp2::Interface({
        {interner.intern("v_in"), Domain::Electrical, bp2::Direction::Input},
        {interner.intern("ground"), Domain::Electrical, bp2::Direction::InOut},
    }));
    library.add(interner.intern("LED"), led);

    return library;
}

bp2::Blueprint::Node make_bridge_node(ui::StringInterner& I,
                                      const char* id,
                                      bool input_side,
                                      Domain domain) {
    PortType type = (domain == Domain::Electrical) ? PortType::V : PortType::Signal;
    bp2::Blueprint::Node bridge;
    bridge.semantic.id = I.intern(id);
    bridge.semantic.type = I.intern("BridgePort");
    bridge.view.name = id;
    bridge.content = bp2::Blueprint::Node::BridgePortData{
        I.intern(id),
        input_side ? bp2::BridgeDirection::Input
                   : bp2::BridgeDirection::Output,
        type,
    };
    return bridge;
}

// == Minimal component registry for elaborate_for_jit ==

ComponentRegistry make_minimal_registry() {
    ComponentRegistry registry;

    // Battery
    {
        PrimitiveSpec spec;
        spec.classname = "Battery";
        spec.ports["v_in"] = Port{bp2::Direction::Input, PortType::V, std::nullopt};
        spec.ports["v_out"] = Port{bp2::Direction::Output, PortType::V, std::nullopt};
        spec.domains = {Domain::Electrical};
        spec.solver.execution = {.electrical_passive = true};
        spec.params["v_nominal"] = ParamSpec{ParamSchemaType::Float, "28.0"};
        spec.params["capacity_ah"] = ParamSpec{ParamSchemaType::Float, "60.0"};
        spec.params["internal_r"] = ParamSpec{ParamSchemaType::Float, "0.01"};
        spec.solver.solver_owned_electrical = false;
        SolverRole role;
        role.kind = "FixedVoltageNode";
        role.port_map["node"] = "v_out";
        role.param_map["voltage"] = "v_nominal";
        role.value_map["bind_handle"] = 1.0f;
        spec.solver.solver_role = role;
        registry.register_type("Battery", spec);
    }

    // Resistor (from authoritative library spec)
    registry.register_type("Resistor", *as_primitive(*test_registry().get("Resistor")));

    // Ground (port name must match Blueprint interface "gnd")
    {
        PrimitiveSpec spec;
        spec.classname = "Ground";
        spec.ports["gnd"] = Port{bp2::Direction::InOut, PortType::V, std::nullopt};
        spec.domains = {Domain::Electrical};
        spec.solver.execution = {.electrical_passive = true};
        spec.solver.scheduler_source = true;
        spec.params["value"] = ParamSpec{ParamSchemaType::Float, "0.0"};
        SolverRole role;
        role.kind = "FixedVoltageNode";
        role.port_map["node"] = "gnd";
        role.param_map["voltage"] = "value";
        role.value_map["bind_handle"] = 1.0f;
        spec.solver.solver_role = role;
        spec.solver.solver_owned_electrical = false;
        registry.register_type("Ground", spec);
    }

    // LED (simple passive)
    {
        PrimitiveSpec spec;
        spec.classname = "LED";
        spec.ports["v_in"] = Port{bp2::Direction::Input, PortType::V, std::nullopt};
        spec.ports["ground"] = Port{bp2::Direction::InOut, PortType::V, std::nullopt};
        spec.domains = {Domain::Electrical};
        spec.solver.execution = {.electrical_passive = true};
        spec.params["conductance"] = ParamSpec{ParamSchemaType::Float, "0.002"};
        spec.solver.solver_owned_electrical = false;
        SolverRole role;
        role.kind = "ConductanceBranch";
        role.port_map["a"] = "v_in";
        role.port_map["b"] = "ground";
        role.param_map["g"] = "conductance";
        role.value_map["bind_handle"] = 1.0f;
        spec.solver.solver_role = role;
        registry.register_type("LED", spec);
    }

    return registry;
}

// == File helpers ==

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

/// Try to read a file from several candidate relative paths.
/// Returns (contents, resolved_path) on success, or ("", "") if not found.
static std::pair<std::string, std::string> try_read_file(
    const std::vector<std::string>& candidates)
{
    for (const auto& p : candidates) {
        std::string contents = read_file(p);
        if (!contents.empty()) return {std::move(contents), p};
    }
    return {};
}

static std::pair<std::string, std::string> find_and_read_blueprint(const std::string& name) {
    return try_read_file({
        "../../" + name,
        "../" + name,
        name,
    });
}

static std::string find_library_dir() {
    for (const auto& p : {
        "../../library/",
        "../library/",
        "library/",
    }) {
        std::ifstream f(std::string(p) + "electrical/Resistor.blueprint");
        if (f.is_open()) return p;
    }
    return {};
}

} // namespace

// ==================================================================
// Fixture 1: Simple chain — bat → r1 → r2 (no bridges)
// ==================================================================

TEST(CrossPathSignalEquivalence, SimpleChainThreeNodes) {
    ui::StringInterner I;
    bp2::PathArena arena(I);
    auto library = make_test_library(I);

    // Build blueprint: bat → r1 → r2
    bp2::Blueprint bp;

    bp2::Blueprint::Node bat;
    bat.semantic.id = I.intern("bat");
    bat.semantic.type = I.intern("Battery");
    bat.component().iface = library.find(I.intern("Battery"))->iface();
    bp = bp.with_node(std::move(bat));

    bp2::Blueprint::Node r1;
    r1.semantic.id = I.intern("r1");
    r1.semantic.type = I.intern("Resistor");
    r1.component().iface = library.find(I.intern("Resistor"))->iface();
    bp = bp.with_node(std::move(r1));

    bp2::Blueprint::Node r2;
    r2.semantic.id = I.intern("r2");
    r2.semantic.type = I.intern("Resistor");
    r2.component().iface = library.find(I.intern("Resistor"))->iface();
    bp = bp.with_node(std::move(r2));

    bp2::Blueprint::Wire w1;
    w1.id = I.intern("w1");
    w1.source = {I.intern("bat"), I.intern("v_out")};
    w1.target = {I.intern("r1"), I.intern("in")};
    w1.domain = Domain::Electrical;
    bp = bp.with_wire(std::move(w1));

    bp2::Blueprint::Wire w2;
    w2.id = I.intern("w2");
    w2.source = {I.intern("r1"), I.intern("out")};
    w2.target = {I.intern("r2"), I.intern("in")};
    w2.domain = Domain::Electrical;
    bp = bp.with_wire(std::move(w2));

    // Path A: Flattener → elaborate_for_jit
    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(bp, arena);

    ComponentRegistry registry = make_minimal_registry();
    JitBuildInput input_a = bp2::elaboration::elaborate_for_jit(netlist, arena, I, registry);

    auto topo_a = extract_topology(input_a.port_to_signal, input_a.signal_key_interner);

    // --- Hand-crafted expected topology (independent of either path) ---
    // bat.v_out ↔ r1.in  (wire w1)
    // r1.out ↔ r2.in     (wire w2)
    std::set<std::set<std::string>> expected;
    expected.insert({"bat.v_out", "r1.in"});
    expected.insert({"r1.out", "r2.in"});

    EXPECT_EQ(topo_a, expected)
        << "Simple chain: Path A topology must match hand-crafted expected";

    // Path B: signal_alloc pipeline directly
    auto connections = reconstruct_connections(input_a.port_to_signal, input_a.signal_key_interner);
    auto bridges = bp2::elaboration::extract_bridge_definitions(netlist, arena, I);

    uint32_t signal_count_b = 0;
    auto p2s_b = run_signal_alloc_pipeline(input_a.devices, bridges, connections, signal_count_b);

    auto topo_b = extract_topology_from_string_map(p2s_b);

    EXPECT_EQ(topo_a, topo_b)
        << "Simple chain: Flattener and JIT paths must produce identical topology";

    EXPECT_GE(topo_a.size(), 2u);
}

// ==================================================================
// Fixture 2: Nested composite with bridges
//   Root: bat → [inner].vin → [inner].vout → gnd
//   Inner: vin(bridge_in) → r1 → vout(bridge_out)
// ==================================================================

TEST(CrossPathSignalEquivalence, NestedCompositeWithBridges) {
    ui::StringInterner I;
    bp2::PathArena arena(I);
    auto library = make_test_library(I);

    // Inner blueprint: vin → r1 → vout
    bp2::Blueprint inner;
    inner = inner.with_interface(bp2::Interface({
        make_port(I, "vin", Domain::Electrical, bp2::Direction::Input, PortType::V),
        make_port(I, "vout", Domain::Electrical, bp2::Direction::Output, PortType::V),
    }));

    bp2::Blueprint::Node bridge_in = make_bridge_node(I, "vin", true, Domain::Electrical);
    inner = inner.with_node(std::move(bridge_in));

    bp2::Blueprint::Node bridge_out = make_bridge_node(I, "vout", false, Domain::Electrical);
    inner = inner.with_node(std::move(bridge_out));

    bp2::Blueprint::Node r1;
    r1.semantic.id = I.intern("r1");
    r1.semantic.type = I.intern("Resistor");
    r1.component().iface = library.find(I.intern("Resistor"))->iface();
    inner = inner.with_node(std::move(r1));

    // Inner wires: vin.port → r1.in, r1.out → vout.port
    {
        bp2::Blueprint::Wire w;
        w.id = I.intern("iw1");
        w.source = {I.intern("vin"), I.intern("port")};
        w.target = {I.intern("r1"), I.intern("in")};
        w.domain = Domain::Electrical;
        inner = inner.with_wire(std::move(w));
    }
    {
        bp2::Blueprint::Wire w;
        w.id = I.intern("iw2");
        w.source = {I.intern("r1"), I.intern("out")};
        w.target = {I.intern("vout"), I.intern("port")};
        w.domain = Domain::Electrical;
        inner = inner.with_wire(std::move(w));
    }

    // Root: bat → [inst].vin, [inst].vout → gnd
    bp2::Blueprint root;

    bp2::Blueprint::Node bat;
    bat.semantic.id = I.intern("bat");
    bat.semantic.type = I.intern("Battery");
    bat.component().iface = library.find(I.intern("Battery"))->iface();
    root = root.with_node(std::move(bat));

    bp2::Blueprint::Node inst;
    inst.semantic.id = I.intern("inst");
    inst.semantic.type = I.intern("InnerPass");
    inst.content = bp2::Blueprint::Node::BlueprintInstanceData{
        bp2::Blueprint::Node::BlueprintSource::make_embedded(
            std::make_unique<bp2::Blueprint>(inner.with_id(I.intern("InnerPass"))))
    };
    root = root.with_node(std::move(inst));

    bp2::Blueprint::Node gnd;
    gnd.semantic.id = I.intern("gnd");
    gnd.semantic.type = I.intern("Ground");
    gnd.component().iface = library.find(I.intern("Ground"))->iface();
    root = root.with_node(std::move(gnd));

    {
        bp2::Blueprint::Wire w;
        w.id = I.intern("rw1");
        w.source = {I.intern("bat"), I.intern("v_out")};
        w.target = {I.intern("inst"), I.intern("vin")};
        w.domain = Domain::Electrical;
        root = root.with_wire(std::move(w));
    }
    {
        bp2::Blueprint::Wire w;
        w.id = I.intern("rw2");
        w.source = {I.intern("inst"), I.intern("vout")};
        w.target = {I.intern("gnd"), I.intern("gnd")};
        w.domain = Domain::Electrical;
        root = root.with_wire(std::move(w));
    }

    // Path A: Flattener → elaborate_for_jit
    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(root, arena);

    ComponentRegistry registry = make_minimal_registry();
    JitBuildInput input_a = bp2::elaboration::elaborate_for_jit(netlist, arena, I, registry);

    auto topo_a = extract_topology(input_a.port_to_signal, input_a.signal_key_interner);

    // --- Hand-crafted expected topology (independent of either path) ---
    // Root wires: bat.v_out → inst.vin (bridge-in ext), inst.vout (bridge-out ext) → gnd.gnd
    // Inner wires: inst:vin.port → inst:r1.in, inst:r1.out → inst:vout.port
    // Bridge passthrough unites ext↔port, so the final groups are:
    //   {bat.v_out, inst:vin.ext, inst:vin.port, inst.vin, inst:r1.in}
    //   {inst:r1.out, inst:vout.ext, inst:vout.port, inst.vout, gnd.gnd}
    std::set<std::set<std::string>> expected;
    expected.insert({"bat.v_out", "inst:vin.ext", "inst:vin.port", "inst.vin", "inst:r1.in"});
    expected.insert({"inst:r1.out", "inst:vout.ext", "inst:vout.port", "inst.vout", "gnd.gnd"});

    EXPECT_EQ(topo_a, expected)
        << "Nested composite: Path A topology must match hand-crafted expected";

    // Path B: signal_alloc pipeline directly
    auto connections = reconstruct_connections(input_a.port_to_signal, input_a.signal_key_interner);
    auto bridges = bp2::elaboration::extract_bridge_definitions(netlist, arena, I);

    uint32_t signal_count_b = 0;
    auto p2s_b = run_signal_alloc_pipeline(input_a.devices, bridges, connections, signal_count_b);

    auto topo_b = extract_topology_from_string_map(p2s_b);

    EXPECT_EQ(topo_a, topo_b)
        << "Nested composite with bridges: Flattener and JIT paths must produce identical topology";

    EXPECT_FALSE(bridges.empty()) << "Nested composite must have bridge definitions";
}

// ==================================================================
// Fixture 3: closed_circuit.blueprint — full complex circuit
// ==================================================================

TEST(CrossPathSignalEquivalence, ClosedCircuitBlueprint) {
    std::string lib_dir = find_library_dir();
    if (lib_dir.empty()) {
        GTEST_SKIP() << "Cannot find library directory";
    }

    auto [raw_json, bp_path] = find_and_read_blueprint("closed_circuit.blueprint");
    if (raw_json.empty()) {
        GTEST_SKIP() << "Cannot find closed_circuit.blueprint";
    }

    ComponentRegistry registry = load_component_registry(lib_dir);

    ui::StringInterner I;
    bp2::PathArena arena(I);

    // Build library from registry (non-primitive composites)
    bp2::BlueprintLibrary library;
    for (const auto& [classname, spec] : registry.all_types()) {
        if (is_primitive(spec)) continue;
        try {
            auto loaded = bp2::blueprint_from_type_definition(spec, I, registry);
            library.add(I.intern(classname), std::move(loaded));
        } catch (const std::exception& e) {
            // Non-critical: composite types that fail to load are simply skipped.
            // This is expected for types without a valid blueprint definition.
            std::cerr << "Note: skipping composite '" << classname
                      << "' during library build: " << e.what() << std::endl;
        }
    }

    // Decode blueprint
    bp2::DecodeError err;
    auto bp = bp2::BlueprintCodec::decode(raw_json, I, arena, registry, &err);
    ASSERT_TRUE(bp.has_value()) << "Decode failed: " << err.message;

    // Path A: Flattener → elaborate_for_jit
    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(*bp, arena);
    JitBuildInput input_a = bp2::elaboration::elaborate_for_jit(netlist, arena, I, registry);

    // Path B: signal_alloc pipeline directly
    auto connections = reconstruct_connections(input_a.port_to_signal, input_a.signal_key_interner);
    auto bridges = bp2::elaboration::extract_bridge_definitions(netlist, arena, I);

    uint32_t signal_count_b = 0;
    auto p2s_b = run_signal_alloc_pipeline(input_a.devices, bridges, connections, signal_count_b);

    // Compare topologies
    auto topo_a = extract_topology(input_a.port_to_signal, input_a.signal_key_interner);
    auto topo_b = extract_topology_from_string_map(p2s_b);

    EXPECT_EQ(topo_a, topo_b)
        << "closed_circuit.blueprint: Flattener and JIT paths must produce identical topology";

    // Sanity: the complex blueprint should have many signal groups
    EXPECT_GT(topo_a.size(), 10u) << "closed_circuit.blueprint should have significant connectivity";

    // --- Structural sanity checks (independent of either path) ---

    // 1. Every non-bridge device's ports appear in the signal map
    for (const auto& dev : input_a.devices) {
        for (const auto& [port_name, _port] : dev.ports) {
            const std::string key = dev.name + "." + port_name;
            auto iid = input_a.signal_key_interner.lookup(key);
            EXPECT_NE(iid, ui::InternedId{})
                << "Device port '" << key << "' missing from signal map";
            if (iid != ui::InternedId{}) {
                EXPECT_NE(input_a.port_to_signal.count(iid), 0u)
                    << "Device port '" << key << "' has no signal assignment";
            }
        }
    }

    // 2. Signal count is positive and reasonable
    EXPECT_GT(input_a.signal_count, 1u) << "Signal count should include at least one signal + sentinel";

    // 3. No empty signal groups — every group has at least 2 ports
    for (const auto& group : topo_a) {
        EXPECT_GE(group.size(), 2u) << "Topology group should have at least 2 ports";
    }

    // 4. FlatNetlist component count matches device count (+ bridge components)
    size_t non_bridge_components = 0;
    for (const auto& comp : netlist.components) {
        if (comp.exposed_port_name.empty()) {
            non_bridge_components++;
        }
    }
    EXPECT_EQ(input_a.devices.size(), non_bridge_components)
        << "Every non-bridge FlatNetlist component should become a device";

    // 5. Every port key references either a known device or a known blueprint instance
    //     (exposed keys like "extract_inst_1.feedback" reference parent instances)
    std::set<std::string> device_names;
    for (const auto& dev : input_a.devices) {
        device_names.insert(dev.name);
    }
    // Collect blueprint instance names from the blueprint's top-level nodes
    std::set<std::string> instance_names;
    for (const auto& node : bp->nodes()) {
        if (node.is_blueprint_instance()) {
            instance_names.insert(std::string(I.resolve(node.semantic.id)));
        }
    }
    for (const auto& [port_id, _sig] : input_a.port_to_signal) {
        const std::string port_str(input_a.signal_key_interner.resolve(port_id));
        const size_t dot = port_str.rfind('.');
        ASSERT_NE(dot, std::string::npos) << "Port key '" << port_str << "' has no dot separator";
        const std::string dev_name = port_str.substr(0, dot);
        const bool is_device = device_names.count(dev_name);
        const bool is_instance = instance_names.count(dev_name);
        // Nested devices use colon separator (e.g., "inst:r1")
        const bool is_nested = dev_name.find(':') != std::string::npos;
        EXPECT_TRUE(is_device || is_instance || is_nested)
            << "Port key '" << port_str << "' references unknown entity '" << dev_name << "'";
    }
}

// ==================================================================
// Fixture 4: Port alias union — device with port.alias
//   Proves that apply_alias_unions correctly unites aliased ports.
//   Uses a device where port "v_out" has alias "v_bus", so both must
//   share the same signal.
// ==================================================================

TEST(CrossPathSignalEquivalence, PortAliasUnion) {
    // Proves that apply_alias_unions correctly unites aliased ports.
    // The Flattener path doesn't carry aliases through FlatNetlist
    // (PortDescriptors don't have alias fields), so this test validates
    // the signal_alloc pipeline's alias union rule directly.

    // Source: outputs voltage
    ResolvedDevice src;
    src.name = "src";
    src.classname = "Source";
    src.domains = {Domain::Electrical};
    src.ports["v"] = Port{bp2::Direction::Output, PortType::V, std::nullopt};

    // Load: has mutually-aliased ports — "v_in" aliases "v_bus" and vice versa
    ResolvedDevice load;
    load.name = "load";
    load.classname = "Load";
    load.domains = {Domain::Electrical};
    load.ports["v_in"] = Port{bp2::Direction::Input, PortType::V, "v_bus"};
    load.ports["v_bus"] = Port{bp2::Direction::Output, PortType::V, "v_in"};

    std::vector<ResolvedDevice> devices = {src, load};

    // Wire: src.v → load.v_in (which is aliased to load.v_bus)
    std::vector<Connection> connections = {
        {"src.v", "load.v_in"},
    };

    // Run signal_alloc pipeline
    uint32_t signal_count = 0;
    auto p2s = run_signal_alloc_pipeline(devices, {}, connections, signal_count);

    // Hand-crafted expected topology:
    // src.v ↔ load.v_in ↔ load.v_bus
    // (wire unites src.v and load.v_in; alias unites load.v_in and load.v_bus)
    ASSERT_TRUE(p2s.count("src.v")) << "Missing port src.v";
    ASSERT_TRUE(p2s.count("load.v_in")) << "Missing port load.v_in";
    ASSERT_TRUE(p2s.count("load.v_bus")) << "Missing port load.v_bus";

    EXPECT_EQ(p2s.at("src.v"), p2s.at("load.v_in"))
        << "Wire connection must unite src.v and load.v_in";
    EXPECT_EQ(p2s.at("load.v_in"), p2s.at("load.v_bus"))
        << "Port alias must unite load.v_in and load.v_bus";
    EXPECT_EQ(p2s.at("src.v"), p2s.at("load.v_bus"))
        << "Transitive: src.v, load.v_in, load.v_bus all on same signal";

    // Verify: only one signal group (all 3 ports united)
    auto topo = extract_topology_from_string_map(p2s);
    ASSERT_EQ(topo.size(), 1u) << "Should have exactly one signal group";
    const auto& group = *topo.begin();
    EXPECT_EQ(group.size(), 3u) << "Group should contain all 3 ports";
    EXPECT_TRUE(group.count("src.v"));
    EXPECT_TRUE(group.count("load.v_in"));
    EXPECT_TRUE(group.count("load.v_bus"));
}
