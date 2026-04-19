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
#include <iostream>
#include <fstream>
#include <chrono>
#include <stdexcept>
#include <vector>


namespace {

bp2::BlueprintLibrary build_library(const TypeRegistry& registry, ui::StringInterner& interner) {
    bp2::BlueprintLibrary library;
    for (const auto& [classname, spec] : registry.types) {
        try {
            auto loaded = bp2::blueprint_from_type_definition(spec, interner, registry);
            library.add(interner.intern(classname), std::move(loaded));
        } catch (...) {
        }
    }
    return library;
}

JitBuildInput build_input_from_blueprint_file(const std::string& blueprint_file) {
    std::ifstream file(blueprint_file);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open blueprint file: " + blueprint_file);
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    ui::StringInterner interner;
    bp2::PathArena arena(interner);
    TypeRegistry registry = load_type_registry("library/");
    bp2::DecodeError err;
    auto bp = bp2::BlueprintCodec::decode(content, interner, arena, registry, &err);
    if (!bp) {
        throw std::runtime_error("Failed to decode blueprint: " + err.message);
    }

    bp2::BlueprintLibrary library = build_library(registry, interner);
    bp2::Flattener flattener(library);
    bp2::FlatNetlist netlist = flattener.flatten(*bp, arena);
    return bp2::elaboration::elaborate_for_jit(netlist, arena, interner, &registry);
}

} // namespace



// Benchmark results
struct BenchmarkResult {
    std::string name;
    double setup_ms;
    double simulation_ms;
    double total_ms;
    uint64_t steps;
};

BenchmarkResult benchmark_jit(const std::string& blueprint_file, uint64_t iterations) {
    BenchmarkResult result;
    result.name = "JIT (ComponentVariant + JitProvider)";
    result.steps = iterations;

    auto start = std::chrono::high_resolution_clock::now();

    JitBuildInput input = build_input_from_blueprint_file(blueprint_file);

    JIT_Simulator sim;
    sim.start(input);

    auto setup_done = std::chrono::high_resolution_clock::now();
    result.setup_ms = std::chrono::duration<double, std::milli>(setup_done - start).count();

    // Run simulation
    auto sim_start = std::chrono::high_resolution_clock::now();

    const double dt = 1.0 / 60.0;
    for (uint64_t step = 0; step < iterations; ++step) {
        (void)step;
        sim.step(dt);
    }

    auto sim_done = std::chrono::high_resolution_clock::now();
    result.simulation_ms = std::chrono::duration<double, std::milli>(sim_done - sim_start).count();
    result.total_ms = result.setup_ms + result.simulation_ms;

    return result;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <blueprint_file> [iterations]\n";
        return 1;
    }

    std::string blueprint_file = argv[1];
    uint64_t iterations = argc > 2 ? std::stoull(argv[2]) : 10000;

    std::cout << "=============================================================================\n";
    std::cout << "AN-24 Simulation Benchmark: JIT vs AOT\n";
    std::cout << "=============================================================================\n";
    std::cout << "Blueprint file: " << blueprint_file << "\n";
    std::cout << "Iterations: " << iterations << " steps\n\n";

    // Warmup
    std::cout << "Warming up JIT...\n";
    benchmark_jit(blueprint_file, 1000);

    // Actual benchmarks
    std::cout << "\nRunning benchmarks...\n\n";

    auto jit_result = benchmark_jit(blueprint_file, iterations);

    // Print results
    std::cout << "=============================================================================\n";
    std::cout << "RESULTS:\n";
    std::cout << "=============================================================================\n\n";

    std::cout << jit_result.name << ":\n";
    std::cout << "  Setup:       " << jit_result.setup_ms << " ms\n";
    std::cout << "  Simulation:  " << jit_result.simulation_ms << " ms\n";
    std::cout << "  Total:       " << jit_result.total_ms << " ms\n";
    std::cout << "  Time/step:  " << (jit_result.simulation_ms / jit_result.steps * 1000.0) << " µs\n";
    std::cout << "  Steps/sec:   " << (jit_result.steps / jit_result.simulation_ms * 1000.0) << " steps/s\n";
    std::cout << "  Target:      60000 steps/s (60 Hz)\n";
    std::cout << "  Efficiency:  " << ((jit_result.steps / jit_result.simulation_ms * 1000.0) / 60000.0 * 100.0) << "%\n";

    std::cout << "\nNOTE: To benchmark AOT, run:\n";
    std::cout << "  cd /tmp/aot_test\n";
    std::cout << "  ./aot_blueprint  # This will run simulation\n";
    std::cout << "  # For profiling: perf record ./aot_blueprint\n";

    return 0;
}
