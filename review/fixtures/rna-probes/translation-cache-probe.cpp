#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

#include "shared_transcript_path.hpp"

struct Mapping {
    uint64_t handle;
    int64_t offset;
    int32_t length;

    bool operator==(const Mapping& other) const {
        return handle == other.handle && offset == other.offset && length == other.length;
    }
};

int main() {
    using Path = vg::SharedTranscriptPath<Mapping>;
    const int64_t offset = static_cast<int64_t>(std::numeric_limits<uint32_t>::max()) + 9;
    const uint64_t end = static_cast<uint64_t>(offset) + 23;
    const uint64_t node_length = end + 47;
    auto source = std::make_shared<Path::Source>(
        std::vector<Path::SourceStep>{{10, node_length}});

    Path forward;
    forward.append(source, 0, 1, offset, end);
    Path reverse = forward;
    reverse.reverse_complement();
    Path::TranslationCache cache;
    cache.register_path(forward);
    cache.register_path(reverse);
    source.reset();

    size_t mapper_calls = 0;
    auto mapper = [&](const Path::SourceStep& original, const auto& emit) {
        ++mapper_calls;
        if (original.handle != 10 || original.length != node_length) {
            throw std::runtime_error("unexpected source step");
        }
        emit(Path::SourceStep{20, static_cast<uint64_t>(offset)});
        emit(Path::SourceStep{21, end - static_cast<uint64_t>(offset)});
        emit(Path::SourceStep{22, node_length - end});
    };

    const Path translated_forward = cache.translate(forward, mapper);
    if (cache.empty()) {
        return 1;
    }
    const Path translated_reverse = cache.translate(reverse, mapper);
    if (!cache.empty() || mapper_calls != 1) {
        return 2;
    }

    std::vector<Mapping> forward_mappings;
    translated_forward.for_each_mapping(
        [](uint64_t handle) { return handle ^ 1; },
        [&](const Mapping& mapping, uint64_t) { forward_mappings.push_back(mapping); });
    if (forward_mappings != std::vector<Mapping>{{21, 0, 23}}) {
        return 3;
    }

    std::vector<Mapping> reverse_mappings;
    translated_reverse.for_each_mapping(
        [](uint64_t handle) { return handle ^ 1; },
        [&](const Mapping& mapping, uint64_t) { reverse_mappings.push_back(mapping); });
    if (reverse_mappings != std::vector<Mapping>{{20, 0, 23}}) {
        return 4;
    }

    size_t boundaries = 0;
    translated_forward.for_each_boundary(
        [](uint64_t handle) { return handle ^ 1; },
        [&](const Mapping& mapping, uint64_t rank) {
            ++boundaries;
            if (!(mapping == forward_mappings.front()) || rank != 0) {
                throw std::runtime_error("unexpected translated boundary");
            }
        });
    if (boundaries != 1) {
        return 5;
    }

    std::cout << "translation-cache-probe passed: offset=" << offset
              << " length=23 mapper_calls=" << mapper_calls << "\n";
}
