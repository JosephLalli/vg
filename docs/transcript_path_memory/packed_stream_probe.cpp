// Compare ordinary PackedGraph path construction with replayed streamed output.

#include <bdsg/packed_graph.hpp>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <vector>

using namespace bdsg;
using namespace handlegraph;

namespace {

constexpr size_t STEPS_PER_PATH = 4096;
constexpr size_t PATH_COUNT = 12208;

double seconds(const timeval& value) {
    return static_cast<double>(value.tv_sec) + static_cast<double>(value.tv_usec) / 1e6;
}

struct Metrics {
    double wall_seconds;
    double user_seconds;
    double system_seconds;
    long max_rss_kib;
};

PackedGraph make_graph() {
    PackedGraph graph;
    const std::vector<nid_t> ids = {1003, 7, 65537, 3, 511, 41, 9001, 19,
                                    307, 73, 2003, 5, 4099, 29, 10007, 11};
    std::vector<handle_t> handles;
    handles.reserve(ids.size());
    for (const auto id : ids) {
        handles.push_back(graph.create_handle("ACGT", id));
    }
    for (size_t i = 0; i < handles.size(); ++i) {
        graph.create_edge(handles[i], handles[(i + 1) % handles.size()]);
    }
    return graph;
}

std::vector<handle_t> make_shared_walk(const PackedGraph& graph) {
    const std::vector<nid_t> ids = {1003, 7, 65537, 3, 511, 41, 9001, 19,
                                    307, 73, 2003, 5, 4099, 29, 10007, 11};
    std::vector<handle_t> walk;
    walk.reserve(STEPS_PER_PATH);
    for (size_t i = 0; i < STEPS_PER_PATH; ++i) {
        const bool reverse = (i % 5 == 0) || (i % 17 == 0);
        walk.push_back(graph.get_handle(ids[(i * 11 + i / 7) % ids.size()], reverse));
    }
    return walk;
}

std::string path_name(size_t index) {
    return "dense-path-" + std::to_string(index);
}

template<class Function>
Metrics measure(Function&& function) {
    rusage before{}, after{};
    getrusage(RUSAGE_SELF, &before);
    const auto start = std::chrono::steady_clock::now();
    function();
    const auto finish = std::chrono::steady_clock::now();
    getrusage(RUSAGE_SELF, &after);
    return {std::chrono::duration<double>(finish - start).count(),
            seconds(after.ru_utime) - seconds(before.ru_utime),
            seconds(after.ru_stime) - seconds(before.ru_stime), after.ru_maxrss};
}

void ordinary(PackedGraph& graph, const std::vector<handle_t>& walk, std::ostream& output) {
    for (size_t i = 0; i < PATH_COUNT; ++i) {
        const auto path = graph.create_path_handle(path_name(i), false);
        for (const auto& handle : walk) {
            graph.append_step(path, handle);
        }
    }
    graph.serialize(output);
}

void generated(const PackedGraph& graph, const std::vector<handle_t>& walk, std::ostream& output) {
    graph.serialize_with_paths(output, PATH_COUNT,
        [](size_t index) { return path_name(index); },
        [](size_t) { return STEPS_PER_PATH; },
        [&](size_t, const auto& emit) {
            for (const auto& handle : walk) {
                emit(handle);
            }
        });
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        require(argc == 3, "usage: packed_stream_probe ordinary|generated OUTPUT.pg");
        const std::string mode(argv[1]);
        require(mode == "ordinary" || mode == "generated", "unknown mode");
        PackedGraph graph = make_graph();
        const std::vector<handle_t> walk = make_shared_walk(graph);
        std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
        require(static_cast<bool>(output), "cannot open output");
        Metrics metrics = measure([&] {
            if (mode == "ordinary") {
                ordinary(graph, walk, output);
            } else {
                generated(graph, walk, output);
            }
        });
        output.close();
        require(static_cast<bool>(output), "output write failed");
        std::ifstream input(argv[2], std::ios::binary | std::ios::ate);
        require(static_cast<bool>(input), "cannot stat output");
        std::cout << "{\"mode\":\"" << mode << "\",\"paths\":" << PATH_COUNT
                  << ",\"steps_per_path\":" << STEPS_PER_PATH
                  << ",\"total_steps\":" << PATH_COUNT * STEPS_PER_PATH
                  << ",\"writer_wall_seconds\":" << metrics.wall_seconds
                  << ",\"writer_user_seconds\":" << metrics.user_seconds
                  << ",\"writer_system_seconds\":" << metrics.system_seconds
                  << ",\"max_rss_kib\":" << metrics.max_rss_kib
                  << ",\"output_bytes\":" << input.tellg() << "}\n";
    } catch (const std::exception& error) {
        std::cerr << "packed_stream_probe: " << error.what() << '\n';
        return 1;
    }
}
