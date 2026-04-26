/// Golden regression test for the canonical BP2 -> JIT path.
///
/// Validates the elaborated topology, runtime-visible device list, signal count,
/// and representative simulation outputs against a checked-in golden snapshot.

#include <gtest/gtest.h>

#include "blueprint_v2/codec/blueprint_codec.h"
#include "blueprint_v2/elaboration/sim_export.h"
#include "blueprint_v2/flattener/flattener.h"
#include "blueprint_v2/library/blueprint_library.h"
#include "blueprint_v2/library/type_def_to_blueprint.h"
#include "blueprint_v2/path/path.h"
#include "core/solvers/jit/simulator.h"
#include "io/json/component_registry_json_loader.h"
#include "core/strings/interned_id.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {

static std::string read_file_or_skip(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

static std::string find_fixture() {
    for (const auto& p : {
        "../../closed_circuit.blueprint",
        "../closed_circuit.blueprint",
        "closed_circuit.blueprint",
    }) {
        std::ifstream f(p);
        if (f.is_open()) return p;
    }
    return {};
}

static std::string find_golden_fixture() {
    for (const auto& p : {
        "../tests/fixtures/elaborate_jit_golden.json",
        "../../tests/fixtures/elaborate_jit_golden.json",
        "tests/fixtures/elaborate_jit_golden.json",
    }) {
        std::ifstream f(p);
        if (f.is_open()) return p;
    }
    return {};
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

bp2::BlueprintLibrary build_library(const ComponentRegistry& registry, core::StringInterner& interner) {
    bp2::BlueprintLibrary library;
    for (const auto& [classname, spec] : registry.all_types()) {
        if (is_primitive(spec)) continue;
        try {
            auto loaded = bp2::blueprint_from_type_definition(spec, interner, registry);
            library.add(interner.intern(classname), std::move(loaded));
        } catch (...) {
        }
    }
    return library;
}

std::set<std::set<std::string>> extract_topology(const PortToSignal& p2s,
                                                const core::StringInterner& interner) {
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

std::set<std::set<std::string>> parse_topology(const json& topology_json) {
    std::set<std::set<std::string>> topo;
    for (const auto& group_json : topology_json) {
        std::set<std::string> group;
        for (const auto& port_json : group_json) {
            group.insert(port_json.get<std::string>());
        }
        topo.insert(std::move(group));
    }
    return topo;
}

json make_topology_json(const PortToSignal& p2s, const core::StringInterner& interner) {
    std::map<uint32_t, std::vector<std::string>> groups;
    for (const auto& [port_id, sig] : p2s) {
        groups[sig].push_back(std::string(interner.resolve(port_id)));
    }

    json topology = json::array();
    for (auto& [_, ports] : groups) {
        if (ports.size() <= 1) {
            continue;
        }
        std::sort(ports.begin(), ports.end());
        topology.push_back(ports);
    }
    return topology;
}

json make_values_json(const std::map<std::string, float>& values) {
    json out = json::object();
    for (const auto& [port, value] : values) {
        out[port] = value;
    }
    return out;
}

void maybe_regenerate_golden(const std::string& golden_path, const JitBuildInput& input) {
    const char* update = std::getenv("AN24_UPDATE_ELABORATE_JIT_GOLDEN");
    if (!update || std::string_view(update) != "1") {
        return;
    }

    json golden = json::object();
    golden["signal_count"] = input.signal_count;

    std::map<std::string, std::string> devices;
    for (const auto& d : input.devices) {
        devices[d.name] = d.classname;
    }
    golden["devices"] = devices;
    golden["topology"] = make_topology_json(input.port_to_signal, input.signal_key_interner);

    JIT_Simulator sim;
    sim.start(input);
    const double dt = 1.0 / 60.0;
    for (int i = 0; i < 60; ++i) {
        sim.step(dt);
    }

    std::map<std::string, float> values;
    for (const auto& [port_id, _sig] : input.port_to_signal) {
        std::string port_str(input.signal_key_interner.resolve(port_id));
        values[port_str] = sim.get_signal_value(sim.signal_key_interner().lookup(port_str));
    }
    golden["values"] = make_values_json(values);

    std::ofstream out(golden_path);
    out << golden.dump(2) << '\n';
}

} // namespace

class ElaborateJitParityTest : public ::testing::Test {
protected:
    void SetUp() override {
        fixture_path_ = find_fixture();
        golden_path_ = find_golden_fixture();
        library_dir_ = find_library_dir();
        if (fixture_path_.empty() || golden_path_.empty() || library_dir_.empty()) {
            GTEST_SKIP() << "Cannot find regression fixture, golden snapshot, or library directory";
        }

        raw_json_ = read_file_or_skip(fixture_path_);
        ASSERT_FALSE(raw_json_.empty()) << "Fixture file is empty";

        registry_ = load_component_registry(library_dir_);

        const char* update = std::getenv("AN24_UPDATE_ELABORATE_JIT_GOLDEN");
        if (update && std::string_view(update) == "1") {
            (void)build_input();
        }

        std::string golden_raw = read_file_or_skip(golden_path_);
        ASSERT_FALSE(golden_raw.empty()) << "Golden fixture file is empty";
        golden_ = json::parse(golden_raw);
    }

    JitBuildInput build_input() const {
        core::StringInterner interner;
        bp2::PathArena arena(interner);
        bp2::BlueprintLibrary library = build_library(registry_, interner);

        bp2::DecodeError err;
        auto bp = bp2::BlueprintCodec::decode(raw_json_, interner, arena, registry_, &err);
        EXPECT_TRUE(bp.has_value()) << "Decode failed: " << err.message;
        if (!bp.has_value()) {
            return {};
        }

        bp2::Flattener flattener(library);
        bp2::FlatNetlist netlist = flattener.flatten(*bp, arena);
        JitBuildInput input = bp2::elaboration::elaborate_for_jit(netlist, arena, interner, registry_);
        maybe_regenerate_golden(golden_path_, input);
        return input;
    }

    std::string fixture_path_;
    std::string golden_path_;
    std::string library_dir_;
    std::string raw_json_;
    ComponentRegistry registry_;
    json golden_;
};

TEST_F(ElaborateJitParityTest, SignalTopologyMatchesGolden) {
    JitBuildInput input = build_input();
    auto actual_topology = extract_topology(input.port_to_signal, input.signal_key_interner);
    auto expected_topology = parse_topology(golden_["topology"]);

    EXPECT_EQ(actual_topology, expected_topology)
        << "Canonical elaboration topology diverged from the golden snapshot";
}

TEST_F(ElaborateJitParityTest, DeviceListMatchesGolden) {
    JitBuildInput input = build_input();

    std::map<std::string, std::string> actual_devices;
    for (const auto& d : input.devices) {
        actual_devices[d.name] = d.classname;
    }

    std::map<std::string, std::string> expected_devices;
    for (const auto& [name, classname] : golden_["devices"].items()) {
        expected_devices[name] = classname.get<std::string>();
    }

    EXPECT_EQ(actual_devices, expected_devices)
        << "Canonical elaboration device list diverged from the golden snapshot";
}

TEST_F(ElaborateJitParityTest, RepresentativeSimulationOutputsMatchGolden) {
    JitBuildInput input = build_input();

    JIT_Simulator sim;
    ASSERT_NO_THROW(sim.start(input));
    ASSERT_TRUE(sim.is_running());

    const double dt = 1.0 / 60.0;
    for (int i = 0; i < 60; ++i) {
        sim.step(dt);
    }

    for (const auto& [port, expected_json] : golden_["values"].items()) {
        float expected = expected_json.get<float>();
        float actual = sim.get_signal_value(sim.signal_key_interner().lookup(port));
        EXPECT_NEAR(actual, expected, 1e-4f)
            << "Representative value mismatch at '" << port << "'";
    }
}

TEST_F(ElaborateJitParityTest, SignalCountMatchesGolden) {
    JitBuildInput input = build_input();
    EXPECT_EQ(input.signal_count, golden_["signal_count"].get<uint32_t>())
        << "Canonical elaboration signal count diverged from the golden snapshot";
}
