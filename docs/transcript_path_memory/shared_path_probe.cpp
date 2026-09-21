// Standalone correctness and representation probe for SharedTranscriptPath.
#include "src/shared_transcript_path.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Mapping {
    uint64_t handle;
    uint32_t offset;
    uint32_t length;
};

static_assert(sizeof(Mapping) == 16, "probe Mapping must match EditedMapping size");

using SharedPath = vg::SharedTranscriptPath<Mapping>;
using Source = SharedPath::Source;

struct OracleSlice {
    std::shared_ptr<const Source> source;
    uint64_t begin;
    uint64_t end;
    uint32_t first_offset;
    uint32_t last_end;
};

struct OracleExpansion {
    std::vector<Mapping> mappings;
    std::vector<uint32_t> original_lengths;
};

uint64_t flip(uint64_t handle) {
    return handle ^ 1ULL;
}

OracleExpansion expand_oracle(const std::vector<OracleSlice>& slices, bool reverse) {
    OracleExpansion result;
    for (const auto& slice : slices) {
        for (uint64_t i = slice.begin; i < slice.end; ++i) {
            Mapping mapping = (*slice.source)[i];
            const uint32_t original_length = mapping.length;
            mapping.offset = i == slice.begin ? slice.first_offset : 0;
            const uint32_t end = i + 1 == slice.end ? slice.last_end : original_length;
            mapping.length = end - mapping.offset;
            result.mappings.push_back(mapping);
            result.original_lengths.push_back(original_length);
        }
    }
    if (reverse) {
        std::reverse(result.mappings.begin(), result.mappings.end());
        std::reverse(result.original_lengths.begin(), result.original_lengths.end());
        for (size_t i = 0; i < result.mappings.size(); ++i) {
            Mapping& mapping = result.mappings[i];
            mapping.handle = flip(mapping.handle);
            mapping.offset = result.original_lengths[i] - mapping.offset - mapping.length;
        }
    }
    return result;
}

std::vector<Mapping> expand_shared(const SharedPath& path) {
    std::vector<Mapping> result;
    path.for_each_mapping(flip, [&](const Mapping& mapping, uint64_t rank) {
        if (rank != result.size()) {
            throw std::runtime_error("non-contiguous shared-path rank");
        }
        result.push_back(mapping);
    });
    return result;
}

bool equal(const Mapping& left, const Mapping& right) {
    return left.handle == right.handle && left.offset == right.offset && left.length == right.length;
}

void require_equal(const std::vector<Mapping>& observed, const std::vector<Mapping>& expected,
                   const std::string& label) {
    if (observed.size() != expected.size()) {
        throw std::runtime_error(label + ": mapping count differs");
    }
    for (size_t i = 0; i < expected.size(); ++i) {
        if (!equal(observed[i], expected[i])) {
            throw std::runtime_error(label + ": mapping differs at rank " + std::to_string(i));
        }
    }
}

template<class Fn>
void require_throws(Fn&& fn, const std::string& label) {
    try {
        fn();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error(label + ": expected invalid_argument");
}

void check_boundaries(const SharedPath& path, const std::vector<std::pair<uint64_t, Mapping>>& expected,
                      const std::string& label) {
    std::vector<std::pair<uint64_t, Mapping>> observed;
    path.for_each_boundary(flip, [&](const Mapping& mapping, uint64_t rank) {
        observed.emplace_back(rank, mapping);
    });
    if (observed.size() != expected.size()) {
        throw std::runtime_error(label + ": boundary count differs");
    }
    for (size_t i = 0; i < expected.size(); ++i) {
        if (observed[i].first != expected[i].first || !equal(observed[i].second, expected[i].second)) {
            throw std::runtime_error(label + ": boundary differs at index " + std::to_string(i));
        }
    }
}

void check_partial_boundaries_against_oracle(const SharedPath& path, const OracleExpansion& oracle,
                                             const std::string& label) {
    std::vector<std::pair<uint64_t, Mapping>> expected;
    for (size_t i = 0; i < oracle.mappings.size(); ++i) {
        const Mapping& mapping = oracle.mappings[i];
        if (mapping.offset > 0 || mapping.offset + mapping.length != oracle.original_lengths[i]) {
            expected.emplace_back(i, mapping);
        }
    }
    std::vector<std::pair<uint64_t, Mapping>> observed;
    path.for_each_boundary(flip, [&](const Mapping& mapping, uint64_t rank) {
        if (rank >= oracle.mappings.size() || !equal(mapping, oracle.mappings[rank])) {
            throw std::runtime_error(label + ": callback mapping differs from expanded oracle");
        }
        if (mapping.offset > 0 || mapping.offset + mapping.length != oracle.original_lengths[rank]) {
            observed.emplace_back(rank, mapping);
        }
    });
    if (observed.size() != expected.size()) {
        throw std::runtime_error(label + ": partial boundary count differs");
    }
    for (size_t i = 0; i < expected.size(); ++i) {
        if (observed[i].first != expected[i].first || !equal(observed[i].second, expected[i].second)) {
            throw std::runtime_error(label + ": partial boundary differs at index " + std::to_string(i));
        }
    }
}

void run_checks() {
    auto source_a = std::make_shared<Source>(std::vector<Mapping>{{10, 0, 10}, {13, 0, 12}, {10, 0, 10}});
    auto source_b = std::make_shared<Source>(std::vector<Mapping>{{20, 0, 4}, {23, 0, 7}});
    const std::vector<OracleSlice> slices{{source_a, 0, 2, 2, 9}, {source_b, 0, 1, 1, 4},
                                          {source_a, 2, 3, 0, 10}};
    SharedPath path;
    for (const auto& slice : slices) {
        path.append(slice.source, slice.begin, slice.end, slice.first_offset, slice.last_end);
    }
    require_equal(expand_shared(path), expand_oracle(slices, false).mappings, "forward multi-source slices");
    if (path.size() != 4) {
        throw std::runtime_error("forward step count");
    }
    check_boundaries(path, {{0, {10, 2, 8}}, {1, {13, 0, 9}}, {2, {20, 1, 3}}, {3, {10, 0, 10}}},
                     "forward boundary ranks");

    path.reverse_complement();
    require_equal(expand_shared(path), expand_oracle(slices, true).mappings, "reverse multi-source slices");
    check_boundaries(path, {{0, {11, 0, 10}}, {1, {21, 0, 3}}, {2, {12, 3, 9}}, {3, {11, 0, 8}}},
                     "reverse boundary ranks");

    SharedPath adjacent;
    adjacent.append(source_a, 0, 1, 0, 10);
    adjacent.append(source_a, 1, 3, 0, 10);
    if (adjacent.slices().size() != 1 || adjacent.size() != 3) {
        throw std::runtime_error("whole-node adjacency was not coalesced");
    }
    SharedPath partial_adjacent;
    partial_adjacent.append(source_a, 0, 1, 0, 9);
    partial_adjacent.append(source_a, 1, 2, 0, 12);
    if (partial_adjacent.slices().size() != 2) {
        throw std::runtime_error("partial adjacency was incorrectly coalesced");
    }
    SharedPath single_partial;
    single_partial.append(source_b, 1, 2, 2, 6);
    require_equal(expand_shared(single_partial), {{23, 2, 4}}, "single partial node");

    // The Source retains pre-split lengths; the path must remain readable after external ownership drops.
    auto lifetime_source = std::make_shared<Source>(std::vector<Mapping>{{30, 0, 8}});
    std::weak_ptr<const Source> weak_source = lifetime_source;
    SharedPath retained;
    retained.append(lifetime_source, 0, 1, 1, 7);
    lifetime_source.reset();
    if (weak_source.expired()) {
        throw std::runtime_error("slice did not retain source lifetime");
    }
    require_equal(expand_shared(retained), {{30, 1, 6}}, "retained original source length");
    SharedPath copied = retained;
    SharedPath moved = std::move(copied);
    if (!copied.empty()) {
        throw std::runtime_error("moved-from path did not reset its step count");
    }
    require_equal(expand_shared(moved), {{30, 1, 6}}, "copied and moved source ownership");
    SharedPath move_assigned;
    move_assigned = std::move(moved);
    if (!moved.empty()) {
        throw std::runtime_error("move-assigned path did not reset its step count");
    }
    require_equal(expand_shared(move_assigned), {{30, 1, 6}}, "move-assigned source ownership");
    retained = SharedPath();
    move_assigned = SharedPath();
    if (!weak_source.expired()) {
        throw std::runtime_error("source remained after its final path release");
    }

    path.reverse_complement();
    require_equal(expand_shared(path), expand_oracle(slices, false).mappings, "reverse twice restores forward walk");

    require_throws([] { (void)Source(std::vector<Mapping>{{1, 1, 4}}); }, "partial source mapping");
    require_throws([] { (void)Source(std::vector<Mapping>{{1, 0, 0}}); }, "zero-length source mapping");
    require_throws([&] { SharedPath bad; bad.append(nullptr, 0, 1, 0, 1); }, "null source");
    require_throws([&] { SharedPath bad; bad.append(source_a, 1, 1, 0, 1); }, "empty range");
    require_throws([&] { SharedPath bad; bad.append(source_a, 2, 1, 0, 1); }, "wrong-order range");
    require_throws([&] { SharedPath bad; bad.append(source_a, 0, 4, 0, 1); }, "range past source");
    require_throws([&] { SharedPath bad; bad.append(source_a, 0, 1, 10, 10); }, "first offset outside node");
    require_throws([&] { SharedPath bad; bad.append(source_a, 0, 1, 0, 11); }, "last end outside node");
    require_throws([&] { SharedPath bad; bad.append(source_a, 0, 1, 8, 7); }, "crossing single-node boundaries");

    std::mt19937_64 generator(0x5EED5EEDULL);
    for (size_t case_number = 0; case_number < 256; ++case_number) {
        std::vector<std::shared_ptr<const Source>> sources;
        for (size_t source_number = 0; source_number < 3; ++source_number) {
            std::vector<Mapping> mappings;
            for (size_t i = 0; i < 17; ++i) {
                mappings.push_back({1000 + source_number * 100 + i, 0,
                                    static_cast<uint32_t>(1 + generator() % 31)});
            }
            sources.push_back(std::make_shared<Source>(std::move(mappings)));
        }
        SharedPath random_path;
        std::vector<OracleSlice> random_slices;
        for (size_t i = 0; i < 7; ++i) {
            const auto source = sources[generator() % sources.size()];
            const uint64_t begin = generator() % source->size();
            const uint64_t end = begin + 1 + generator() % (source->size() - begin);
            const uint32_t first_offset = static_cast<uint32_t>(generator() % (*source)[begin].length);
            const uint32_t last_end = static_cast<uint32_t>(1 + generator() % (*source)[end - 1].length);
            if (end == begin + 1 && first_offset >= last_end) {
                continue;
            }
            random_path.append(source, begin, end, first_offset, last_end);
            random_slices.push_back({source, begin, end, first_offset, last_end});
        }
        const OracleExpansion forward = expand_oracle(random_slices, false);
        require_equal(expand_shared(random_path), forward.mappings, "random forward");
        check_partial_boundaries_against_oracle(random_path, forward, "random forward boundaries");
        random_path.reverse_complement();
        const OracleExpansion reverse = expand_oracle(random_slices, true);
        require_equal(expand_shared(random_path), reverse.mappings, "random reverse");
        check_partial_boundaries_against_oracle(random_path, reverse, "random reverse boundaries");
    }
}

uint64_t mix(uint64_t state, const Mapping& mapping) {
    state ^= mapping.handle + 0x9e3779b97f4a7c15ULL + (state << 6) + (state >> 2);
    state ^= (static_cast<uint64_t>(mapping.offset) << 32) | mapping.length;
    return state * 0xbf58476d1ce4e5b9ULL;
}

struct BenchmarkSlice {
    uint64_t begin;
    uint64_t end;
    uint32_t first_offset;
    uint32_t last_end;
};

std::vector<BenchmarkSlice> benchmark_slices(const Source& source) {
    std::vector<BenchmarkSlice> slices;
    for (uint64_t i = 0; i < source.size(); i += 8) {
        const uint64_t end = i + 8;
        slices.push_back({i, end, static_cast<uint32_t>((i / 8) % 3),
                          static_cast<uint32_t>(source[end - 1].length - ((i / 8) % 2))});
    }
    return slices;
}

void print_json(const std::string& mode, uint64_t paths, uint64_t mappings, uint64_t source_bytes,
                uint64_t representation_capacity_bytes, double construction_seconds, double scan_seconds,
                uint64_t checksum) {
    std::cout << std::fixed << std::setprecision(6)
              << "{\"mode\":\"" << mode << "\",\"paths\":" << paths
              << ",\"mappings\":" << mappings << ",\"source_bytes\":" << source_bytes
              << ",\"representation_capacity_bytes\":" << representation_capacity_bytes
              << ",\"construction_seconds\":" << construction_seconds
              << ",\"scan_seconds\":" << scan_seconds << ",\"checksum\":" << checksum << "}\n";
}

void run_benchmark(const std::string& mode) {
    constexpr uint64_t path_count = 10000;
    constexpr uint64_t source_steps = 512;
    auto source = std::make_shared<Source>([&] {
        std::vector<Mapping> mappings;
        mappings.reserve(source_steps);
        for (uint64_t i = 0; i < source_steps; ++i) {
            mappings.push_back({2 * i + 100, 0, static_cast<uint32_t>(17 + (i % 29))});
        }
        return mappings;
    }());
    const auto slices = benchmark_slices(*source);
    const auto started = std::chrono::steady_clock::now();
    uint64_t representation_capacity_bytes = 0;
    uint64_t checksum = 0x123456789abcdef0ULL;
    uint64_t mappings = 0;

    if (mode == "expanded") {
        std::vector<std::vector<Mapping>> paths;
        paths.reserve(path_count);
        for (uint64_t p = 0; p < path_count; ++p) {
            std::vector<Mapping> path;
            std::vector<uint32_t> original_lengths;
            path.reserve(source_steps);
            original_lengths.reserve(source_steps);
            for (const auto& slice : slices) {
                for (uint64_t i = slice.begin; i < slice.end; ++i) {
                    Mapping mapping = (*source)[i];
                    const uint32_t original_length = mapping.length;
                    mapping.offset = i == slice.begin ? slice.first_offset : 0;
                    const uint32_t end = i + 1 == slice.end ? slice.last_end : mapping.length;
                    mapping.length = end - mapping.offset;
                    path.push_back(mapping);
                    original_lengths.push_back(original_length);
                }
            }
            if (p % 2 == 1) {
                std::reverse(path.begin(), path.end());
                std::reverse(original_lengths.begin(), original_lengths.end());
                for (size_t i = 0; i < path.size(); ++i) {
                    path[i].handle = flip(path[i].handle);
                    path[i].offset = original_lengths[i] - path[i].offset - path[i].length;
                }
            }
            mappings += path.size();
            representation_capacity_bytes += path.capacity() * sizeof(Mapping);
            paths.push_back(std::move(path));
        }
        representation_capacity_bytes += paths.capacity() * sizeof(std::vector<Mapping>);
        const auto constructed = std::chrono::steady_clock::now();
        for (unsigned pass = 0; pass < 2; ++pass) {
            for (const auto& path : paths) {
                for (const auto& mapping : path) {
                    checksum = mix(checksum, mapping);
                }
            }
        }
        const auto finished = std::chrono::steady_clock::now();
        print_json(mode, path_count, mappings, source->capacity_bytes(), representation_capacity_bytes,
                   std::chrono::duration<double>(constructed - started).count(),
                   std::chrono::duration<double>(finished - constructed).count(), checksum);
        return;
    }

    std::vector<SharedPath> paths;
    paths.reserve(path_count);
    for (uint64_t p = 0; p < path_count; ++p) {
        SharedPath path;
        for (const auto& slice : slices) {
            path.append(source, slice.begin, slice.end, slice.first_offset, slice.last_end);
        }
        if (p % 2 == 1) {
            path.reverse_complement();
        }
        mappings += path.size();
        representation_capacity_bytes += path.slices().capacity() * sizeof(SharedPath::Slice);
        paths.push_back(std::move(path));
    }
    representation_capacity_bytes += paths.capacity() * sizeof(SharedPath);
    const auto constructed = std::chrono::steady_clock::now();
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (const auto& path : paths) {
            path.for_each_mapping(flip, [&](const Mapping& mapping, uint64_t) {
                checksum = mix(checksum, mapping);
            });
        }
    }
    const auto finished = std::chrono::steady_clock::now();
    print_json(mode, path_count, mappings, source->capacity_bytes(), representation_capacity_bytes,
               std::chrono::duration<double>(constructed - started).count(),
               std::chrono::duration<double>(finished - constructed).count(), checksum);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " {check|expanded|shared}\n";
        return EXIT_FAILURE;
    }
    try {
        const std::string mode(argv[1]);
        if (mode == "check") {
            run_checks();
            std::cout << "{\"mode\":\"check\",\"status\":\"ok\"}\n";
        } else if (mode == "expanded" || mode == "shared") {
            run_benchmark(mode);
        } else {
            std::cerr << "unknown mode: " << mode << "\n";
            return EXIT_FAILURE;
        }
    } catch (const std::exception& error) {
        std::cerr << "shared_path_probe: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
