#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "shared_transcript_path.hpp"

struct Mapping {
    uint64_t handle;
    int64_t offset;
    int32_t length;
};

using SharedPath = vg::SharedTranscriptPath<Mapping>;

static bool width_boundary_passes() {
    const int64_t offset = static_cast<int64_t>(std::numeric_limits<uint32_t>::max()) + 1;
    const uint64_t end = static_cast<uint64_t>(offset) + 17;
    const uint64_t node_length = end + 101;
    auto source = std::make_shared<SharedPath::Source>(
        std::vector<SharedPath::SourceStep>{{42, node_length}});

    SharedPath path;
    path.append(source, 0, 1, offset, end);
    Mapping forward{};
    path.for_each_mapping([](uint64_t handle) { return handle ^ 1; },
                          [&](const Mapping& mapping, uint64_t) { forward = mapping; });
    if (forward.handle != 42 || forward.offset != offset || forward.length != 17) {
        return false;
    }

    path.reverse_complement();
    Mapping reverse{};
    path.for_each_mapping([](uint64_t handle) { return handle ^ 1; },
                          [&](const Mapping& mapping, uint64_t) { reverse = mapping; });
    if (reverse.handle != 43 || reverse.offset != static_cast<int64_t>(node_length - end) ||
        reverse.length != 17) {
        return false;
    }

    try {
        SharedPath invalid;
        invalid.append(source, 0, 1, 0, node_length);
        return false;
    } catch (const std::invalid_argument&) {
    }

    return true;
}

int main(int argc, char** argv) {
    if (!width_boundary_passes()) {
        std::cerr << "width boundary failed\n";
        return 1;
    }

    const bool shared = argc == 2 && std::string(argv[1]) == "shared";
    constexpr size_t path_count = 10000;
    constexpr size_t step_count = 1000;
    std::vector<Mapping> input;
    input.reserve(step_count);
    std::vector<SharedPath::SourceStep> source_steps;
    source_steps.reserve(step_count);
    for (size_t i = 0; i < step_count; ++i) {
        input.push_back({i * 2, 0, 10});
        source_steps.push_back({i * 2, 10});
    }
    auto source = std::make_shared<SharedPath::Source>(std::move(source_steps));
    std::vector<std::vector<Mapping>> expanded;
    std::vector<SharedPath> sliced;
    const auto start = std::chrono::steady_clock::now();
    if (shared) {
        for (size_t i = 0; i < path_count; ++i) {
            sliced.emplace_back();
            sliced.back().append(source, 0, step_count, 0, 10);
        }
    } else {
        for (size_t i = 0; i < path_count; ++i) {
            expanded.push_back(input);
        }
    }
    const auto built = std::chrono::steady_clock::now();
    uint64_t digest = 0;
    if (shared) {
        for (const auto& path : sliced) {
            path.for_each_mapping([](uint64_t handle) { return handle ^ 1; },
                                  [&](const Mapping& mapping, uint64_t rank) {
                digest += mapping.handle + rank + mapping.length;
            });
        }
    } else {
        for (const auto& path : expanded) {
            for (size_t i = 0; i < path.size(); ++i) {
                digest += path[i].handle + i + path[i].length;
            }
        }
    }
    const auto end = std::chrono::steady_clock::now();
    std::cout << std::chrono::duration<double>(built - start).count() << " "
              << std::chrono::duration<double>(end - start).count() << " " << digest << "\n";
}
