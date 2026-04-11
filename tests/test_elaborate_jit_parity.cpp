/// Parity test: elaborate_for_jit() vs legacy to_simulation_export() → parse_json() → UnionFind.
///
/// Validates that the new canonical BP2 → JIT path produces structurally
/// equivalent signal mappings and identical simulation output as the old
/// JSON-based path.  This test is the safety net for deleting the legacy path.

#include <gtest/gtest.h>

#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/elaboration/sim_export.h"
#include "blueprint_v2/flattener/flattener.h"
#include "blueprint_v2/library/blueprint_library.h"
#include "blueprint_v2/library/type_def_to_blueprint.h"
#include "blueprint_v2/path/path.h"
#include "core/solvers/jit/jit_solver.h"
#include "core/solvers/jit/simulator.h"
#include "json_parser/json_parser.h"
#include "ui/core/interned_id.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;

// ============================================================================
// Helpers
// ============================================================================

namespace {

static std::string read_file_or_skip(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

/// Find the canonical v1 blueprint for parity testing.
/// Uses closed_circuit.blueprint (strict v1 format) at the project root.
static std::string find_fixture() {
    for (const auto& p : {
        "../../closed_circuit.blueprint",     // ctest from build/tests/
        "../closed_circuit.blueprint",        // run from build/
        "closed_circuit.blueprint",           // run from project root
    }) {
        std::ifstream f(p);
        if (f.is_open()) return p;
    }
    return {};
}

/// Find the library directory.
static std::string find_library_dir() {
    for (const auto& p : {
        "../../library/",
        "../library/",
        "library/",
    }) {
        std::ifstream f(std::string(p) + "library_index.json");
        if (f.is_open()) return p;
    }
    return {};
}

/// Build the blueprint library from the TypeRegistry (composite → bp2::Blueprint).
bp2::BlueprintLibrary build_library(
    const TypeRegistry& registry,
    ui::StringInterner& interner)
{
    bp2::BlueprintLibrary library;
    for (const auto& [classname, def] : registry.types) {
        if (def.cpp_class) continue;
        try {
            auto loaded = bp2::blueprint_from_type_definition(def, interner, registry);
            library.add(interner.intern(classname), std::move(loaded));
        } catch (...) {
            // Skip blueprints that fail to convert (non-critical)
        }
    }
    return library;
}

/// Extract the topology (signal equivalence classes) from a port_to_signal map.
/// Returns a set of sets — each inner set contains all port keys that share
/// a signal.  This is independent of actual signal index values.
std::set<std::set<std::string>> extract_topology(const PortToSignal& p2s) {
    // Group ports by signal index
    std::map<uint32_t, std::set<std::string>> groups;
    for (const auto& [port, sig] : p2s) {
        groups[sig].insert(port);
    }
    // Convert to set of sets (drop the signal index — we only care about grouping)
    std::set<std::set<std::string>> topo;
    for (auto& [_, ports] : groups) {
        if (ports.size() > 1) {
            topo.insert(std::move(ports));
        }
    }
    return topo;
}

/// Check that two topologies are equivalent: every equivalence class in
/// `expected` must appear in `actual`, and vice versa.
/// Returns empty string on success, or a diagnostic message on failure.
std::string compare_topologies(
    const std::set<std::set<std::string>>& expected,
    const std::set<std::set<std::string>>& actual)
{
    std::string msg;
    for (const auto& group : expected) {
        if (actual.find(group) == actual.end()) {
            msg += "Legacy group not found in new path: {";
            for (const auto& p : group) msg += " " + p;
            msg += " }\n";
        }
    }
    for (const auto& group : actual) {
        if (expected.find(group) == expected.end()) {
            msg += "New-path group not found in legacy: {";
            for (const auto& p : group) msg += " " + p;
            msg += " }\n";
        }
    }
    return msg;
}

} // namespace

// ============================================================================
// Tests
// ============================================================================

class ElaborateJitParityTest : public ::testing::Test {
protected:
    void SetUp() override {
        fixture_path_ = find_fixture();
        library_dir_ = find_library_dir();
        if (fixture_path_.empty() || library_dir_.empty()) {
            GTEST_SKIP() << "Cannot find regression fixture or library directory";
        }

        raw_json_ = read_file_or_skip(fixture_path_);
        ASSERT_FALSE(raw_json_.empty()) << "Fixture file is empty";

        registry_ = load_type_registry(library_dir_);
    }

    std::string fixture_path_;
    std::string library_dir_;
    std::string raw_json_;
    TypeRegistry registry_;
};

/// The core parity test: signal topology must be equivalent between paths.
TEST_F(ElaborateJitParityTest, SignalTopologyParity) {
    // --- New path: BP2 codec → Flattener → elaborate_for_jit() ---
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::BlueprintLibrary library = build_library(registry_, interner);

    bp2::DecodeError err;
    auto bp = bp2::BlueprintCodec::decode(raw_json_, interner, arena, registry_, &err);
    ASSERT_TRUE(bp.has_value()) << "Decode failed: " << err.message;

    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(*bp, arena);

    JitBuildInput new_input = bp2::elaboration::elaborate_for_jit(
        netlist, arena, interner, &registry_);

    // --- Old path: to_simulation_export() → JSON → parse_json() → UnionFind ---
    auto exported = bp2::elaboration::to_simulation_export(
        netlist, arena, interner, &registry_);

    json sim_json = json::object();
    sim_json["templates"] = json::object();
    sim_json["devices"] = std::move(exported.devices);
    sim_json["connections"] = std::move(exported.connections);

    auto ctx = parse_json(sim_json.dump());
    std::vector<std::pair<std::string, std::string>> conn_pairs;
    conn_pairs.reserve(ctx.connections.size());
    for (const auto& c : ctx.connections) {
        conn_pairs.push_back({c.from, c.to});
    }
    BuildResult old_result = build_systems_dev(ctx.devices, conn_pairs);

    // --- Compare topologies ---
    // Find the set of port keys that exist in BOTH maps
    std::set<std::string> common_keys;
    for (const auto& [key, _] : new_input.port_to_signal) {
        if (old_result.port_to_signal.count(key)) {
            common_keys.insert(key);
        }
    }

    // Both maps must share a substantial number of keys
    EXPECT_GT(common_keys.size(), 0u) << "No common port keys between paths!";
    // Allow the new path to have extra keys (exposed bridge keys) but all
    // old keys should be present in new path
    for (const auto& [key, _] : old_result.port_to_signal) {
        EXPECT_TRUE(new_input.port_to_signal.count(key))
            << "Legacy key missing in new path: " << key;
    }

    // For common keys, verify that ports sharing a signal in the old map
    // also share a signal in the new map (and vice versa).
    // Build restricted port_to_signal maps containing only common keys.
    PortToSignal old_common, new_common;
    for (const auto& key : common_keys) {
        old_common[key] = old_result.port_to_signal.at(key);
        new_common[key] = new_input.port_to_signal.at(key);
    }

    auto old_topo = extract_topology(old_common);
    auto new_topo = extract_topology(new_common);

    std::string diff = compare_topologies(old_topo, new_topo);
    EXPECT_TRUE(diff.empty())
        << "Signal topology mismatch between paths:\n" << diff
        << "\nOld groups: " << old_topo.size()
        << ", New groups: " << new_topo.size();
}

/// Both paths must produce the same device list (same names and classnames).
TEST_F(ElaborateJitParityTest, DeviceListParity) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::BlueprintLibrary library = build_library(registry_, interner);

    bp2::DecodeError err;
    auto bp = bp2::BlueprintCodec::decode(raw_json_, interner, arena, registry_, &err);
    ASSERT_TRUE(bp.has_value()) << "Decode failed: " << err.message;

    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(*bp, arena);

    JitBuildInput new_input = bp2::elaboration::elaborate_for_jit(
        netlist, arena, interner, &registry_);

    auto exported = bp2::elaboration::to_simulation_export(
        netlist, arena, interner, &registry_);
    auto ctx = parse_json([&]{
        json j;
        j["templates"] = json::object();
        j["devices"] = std::move(exported.devices);
        j["connections"] = std::move(exported.connections);
        return j.dump();
    }());

    // Compare device name → classname for both paths
    std::map<std::string, std::string> new_devices, old_devices;
    for (const auto& d : new_input.devices) {
        new_devices[d.name] = d.classname;
    }
    for (const auto& d : ctx.devices) {
        old_devices[d.name] = d.classname;
    }

    EXPECT_EQ(new_devices.size(), old_devices.size())
        << "Device count mismatch: new=" << new_devices.size()
        << " old=" << old_devices.size();

    for (const auto& [name, classname] : old_devices) {
        auto it = new_devices.find(name);
        EXPECT_NE(it, new_devices.end())
            << "Device '" << name << "' missing from new path";
        if (it != new_devices.end()) {
            EXPECT_EQ(it->second, classname)
                << "Classname mismatch for '" << name << "': new="
                << it->second << " old=" << classname;
        }
    }
}

/// Simulation output must be equivalent: run both paths for N steps,
/// then compare signal values at common ports.
TEST_F(ElaborateJitParityTest, SimulationOutputParity) {
    // --- Build both paths ---
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::BlueprintLibrary library = build_library(registry_, interner);

    bp2::DecodeError err;
    auto bp = bp2::BlueprintCodec::decode(raw_json_, interner, arena, registry_, &err);
    ASSERT_TRUE(bp.has_value()) << "Decode failed: " << err.message;

    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(*bp, arena);

    JitBuildInput new_input = bp2::elaboration::elaborate_for_jit(
        netlist, arena, interner, &registry_);

    auto exported = bp2::elaboration::to_simulation_export(
        netlist, arena, interner, &registry_);
    json sim_json = json::object();
    sim_json["templates"] = json::object();
    sim_json["devices"] = std::move(exported.devices);
    sim_json["connections"] = std::move(exported.connections);
    std::string json_str = sim_json.dump();

    // --- Run new path ---
    JIT_Simulator sim_new;
    ASSERT_NO_THROW(sim_new.start(new_input));
    ASSERT_TRUE(sim_new.is_running());

    // --- Run old path ---
    JIT_Simulator sim_old;
    ASSERT_NO_THROW(sim_old.start(build_input_from_json(json_str)));
    ASSERT_TRUE(sim_old.is_running());

    // --- Step both for 60 frames (1 second at 60Hz) ---
    const double dt = 1.0 / 60.0;
    const int steps = 60;
    for (int i = 0; i < steps; ++i) {
        sim_new.step(dt);
        sim_old.step(dt);
    }

    // --- Compare output at common ports ---
    // Find common port keys
    std::set<std::string> common_keys;
    for (const auto& [key, _] : new_input.port_to_signal) {
        // old path uses build_result_.port_to_signal internally;
        // we can only read values via get_wire_voltage which also
        // does port_to_signal lookup.  Check by probing.
        float v = sim_old.get_wire_voltage(key);
        // A valid signal returns 0 or non-zero; the function returns 0
        // for unknown keys too, so check if key exists in old path
        // by checking the old build result (we have it from above).
        common_keys.insert(key);
    }

    // Build the old port_to_signal from the already-built result for lookup
    auto old_ctx = parse_json(json_str);
    std::vector<std::pair<std::string, std::string>> conn_pairs;
    for (const auto& c : old_ctx.connections) conn_pairs.push_back({c.from, c.to});
    BuildResult old_build = build_systems_dev(old_ctx.devices, conn_pairs);

    int mismatches = 0;
    int checked = 0;
    for (const auto& [key, _] : new_input.port_to_signal) {
        if (old_build.port_to_signal.count(key) == 0) continue;
        float v_new = sim_new.get_wire_voltage(key);
        float v_old = sim_old.get_wire_voltage(key);
        checked++;
        if (std::abs(v_new - v_old) > 0.01f) {
            mismatches++;
            if (mismatches <= 10) {
                ADD_FAILURE() << "Value mismatch at '" << key << "': "
                    << "new=" << v_new << " old=" << v_old
                    << " delta=" << std::abs(v_new - v_old);
            }
        }
    }

    EXPECT_GT(checked, 0) << "No common ports checked — test is vacuous";
    EXPECT_EQ(mismatches, 0) << mismatches << " / " << checked
        << " ports had different values after " << steps << " steps";
}

/// Signal count must be reasonable: new path should produce similar count
/// (may differ by +/- a few due to bridge exposed keys, but not wildly).
TEST_F(ElaborateJitParityTest, SignalCountSanity) {
    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    bp2::BlueprintLibrary library = build_library(registry_, interner);

    bp2::DecodeError err;
    auto bp = bp2::BlueprintCodec::decode(raw_json_, interner, arena, registry_, &err);
    ASSERT_TRUE(bp.has_value()) << "Decode failed: " << err.message;

    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(*bp, arena);

    JitBuildInput new_input = bp2::elaboration::elaborate_for_jit(
        netlist, arena, interner, &registry_);

    auto exported = bp2::elaboration::to_simulation_export(
        netlist, arena, interner, &registry_);
    json sim_json = json::object();
    sim_json["templates"] = json::object();
    sim_json["devices"] = std::move(exported.devices);
    sim_json["connections"] = std::move(exported.connections);

    auto ctx = parse_json(sim_json.dump());
    std::vector<std::pair<std::string, std::string>> conn_pairs;
    for (const auto& c : ctx.connections) conn_pairs.push_back({c.from, c.to});
    BuildResult old_result = build_systems_dev(ctx.devices, conn_pairs);

    // Signal counts should be in the same ballpark.
    // The new path may have more signals (exposed bridge keys add entries to
    // port_to_signal that map to existing signals, but signal_count should be
    // very close because both are compact ranges starting from 0).
    EXPECT_GT(new_input.signal_count, 0u);
    EXPECT_GT(old_result.signal_count, 0u);

    // Allow up to 20% difference — both derive from the same netlist
    uint32_t max_sc = std::max(new_input.signal_count, old_result.signal_count);
    uint32_t min_sc = std::min(new_input.signal_count, old_result.signal_count);
    float ratio = static_cast<float>(max_sc) / static_cast<float>(min_sc);
    EXPECT_LT(ratio, 1.2f)
        << "Signal count divergence too large: new=" << new_input.signal_count
        << " old=" << old_result.signal_count;
}
