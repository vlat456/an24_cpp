#include "jit_solver_internal.h"
#include "../common/signal_key.h"

#include <algorithm>
#include <queue>
#include <spdlog/spdlog.h>

namespace jit_solver_impl {

void validate_source_writer_conflicts(
    const BuildResult& result,
    const std::vector<ResolvedDevice>& devices)
{
    std::unordered_map<uint32_t, std::vector<std::string>> writers_by_signal;

    for (const auto& dev : devices) {
        if (dev.visual_only) {
            continue;
        }

        const auto sw_ports = active_source_writer_ports_for(dev.classname);
        for (const auto& port_name : sw_ports) {
            const std::string full_port = signal_key::make_node_port_key(dev.name, port_name);
            auto it = result.port_to_signal.find(full_port);
            if (it == result.port_to_signal.end()) {
                continue;
            }
            writers_by_signal[it->second].push_back(full_port);
        }
    }

    for (const auto& [signal_idx, writers] : writers_by_signal) {
        if (writers.size() > 1) {
            std::string detail;
            for (size_t i = 0; i < writers.size(); ++i) {
                if (i > 0) detail += ", ";
                detail += writers[i];
            }
            throw std::runtime_error(
                "Source conflict on signal " + std::to_string(signal_idx) +
                ": multiple source-writer ports drive the same wire: " + detail);
        }
    }
}

void validate_consumer_guardrails(
    const BuildResult& result,
    const std::vector<std::string>& consumer_device_names,
    const std::vector<ResolvedDevice>& devices)
{
    (void)result;

    for (const auto& name : consumer_device_names) {
        auto it_dev = std::find_if(devices.begin(), devices.end(),
            [&name](const ResolvedDevice& d) { return d.name == name; });
        if (it_dev != devices.end()) {
            if (it_dev->solver_owned_electrical) {
                throw std::runtime_error(
                    std::string("Guardrail violation: solver-owned electrical propagator '") +
                    it_dev->classname + "' (device '" + it_dev->name +
                    "') was incorrectly added to push scheduler consumer list. "
                    "These components must only run via the electrical solver.");
            }
        }
    }
}

void topological_sort_consumers(
    BuildResult& result,
    std::vector<std::string>& consumer_device_names,
    const std::vector<ResolvedDevice>& devices)
{
    if (consumer_device_names.empty()) {
        return;
    }

    std::unordered_map<std::string, const ResolvedDevice*> device_by_name;
    device_by_name.reserve(devices.size());
    for (const auto& dev : devices) {
        device_by_name[dev.name] = &dev;
    }

    struct IOSet {
        std::unordered_set<uint32_t> reads;
        std::unordered_set<uint32_t> writes;
    };
    std::unordered_map<std::string, IOSet> io_by_consumer;
    io_by_consumer.reserve(consumer_device_names.size());

    for (const auto& name : consumer_device_names) {
        auto it_dev = device_by_name.find(name);
        if (it_dev == device_by_name.end() || it_dev->second == nullptr) {
            continue;
        }

        const ResolvedDevice& dev = *it_dev->second;
        const auto output_ports = output_ports_for_class(dev.classname);
        auto& io = io_by_consumer[name];

        for (const auto& [port_name, port] : dev.ports) {
            (void)port;
            const std::string full_port = signal_key::make_node_port_key(dev.name, port_name);
            auto it_sig = result.port_to_signal.find(full_port);
            if (it_sig == result.port_to_signal.end()) {
                continue;
            }

            if (output_ports.find(port_name) != output_ports.end()) {
                io.writes.insert(it_sig->second);
            }
            else {
                io.reads.insert(it_sig->second);
            }
        }
    }

    std::unordered_map<std::string, std::vector<std::string>> adj;
    std::unordered_map<std::string, uint32_t> indegree;
    adj.reserve(consumer_device_names.size());
    indegree.reserve(consumer_device_names.size());

    for (const auto& name : consumer_device_names) {
        indegree[name] = 0;
    }

    std::unordered_map<uint32_t, std::vector<std::string>> writers_by_signal;
    writers_by_signal.reserve(result.signal_count);
    for (const auto& name : consumer_device_names) {
        auto it_io = io_by_consumer.find(name);
        if (it_io == io_by_consumer.end()) {
            continue;
        }
        for (uint32_t sig : it_io->second.writes) {
            writers_by_signal[sig].push_back(name);
        }
    }

    std::unordered_set<std::string> seen_edges;
    for (const auto& reader_name : consumer_device_names) {
        auto it_io = io_by_consumer.find(reader_name);
        if (it_io == io_by_consumer.end()) {
            continue;
        }
        for (uint32_t sig : it_io->second.reads) {
            auto it_writers = writers_by_signal.find(sig);
            if (it_writers == writers_by_signal.end()) {
                continue;
            }
            for (const auto& writer_name : it_writers->second) {
                if (writer_name == reader_name) {
                    continue;
                }
                const std::string edge = writer_name + "->" + reader_name;
                if (seen_edges.insert(edge).second) {
                    adj[writer_name].push_back(reader_name);
                    indegree[reader_name]++;
                }
            }
        }
    }

    std::queue<std::string> ready;
    for (const auto& name : consumer_device_names) {
        if (indegree[name] == 0) {
            ready.push(name);
        }
    }

    std::vector<std::string> sorted;
    sorted.reserve(consumer_device_names.size());
    while (!ready.empty()) {
        const std::string current = ready.front();
        ready.pop();
        sorted.push_back(current);

        auto it_adj = adj.find(current);
        if (it_adj == adj.end()) {
            continue;
        }
        for (const auto& next : it_adj->second) {
            if (--indegree[next] == 0) {
                ready.push(next);
            }
        }
    }

    if (sorted.size() < consumer_device_names.size()) {
        spdlog::warn("[build] Consumer dependency cycle detected; falling back to one-frame delay ordering for cycle edges");
        std::unordered_set<std::string> in_sorted(sorted.begin(), sorted.end());
        for (const auto& name : consumer_device_names) {
            if (in_sorted.find(name) == in_sorted.end()) {
                sorted.push_back(name);
            }
        }
    }

    result.scheduler.clear_consumers();
    for (const auto& name : sorted) {
        ComponentVariant* variant = result.devices.find_mutable(name);
        if (variant == nullptr) {
            continue;
        }
        std::visit([&](auto& comp) {
            result.scheduler.add_consumer(&comp);
        }, *variant);
    }
}

} // namespace jit_solver_impl
