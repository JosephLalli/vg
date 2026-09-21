// Standalone correctness probe for SharedTranscriptPath::TranslationCache.
#include "src/shared_transcript_path.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
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

using SharedPath = vg::SharedTranscriptPath<Mapping>;
using Source = SharedPath::Source;
using Cache = SharedPath::TranslationCache;

uint64_t flip(uint64_t handle) {
    return handle ^ 1ULL;
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template<class Exception, class Function>
void require_throws(Function&& function, const std::string& message) {
    try {
        function();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error(message + ": expected exception");
}

bool equal(const Mapping& left, const Mapping& right) {
    return left.handle == right.handle && left.offset == right.offset &&
           left.length == right.length;
}

void require_equal(const std::vector<Mapping>& observed,
                   const std::vector<Mapping>& expected,
                   const std::string& label) {
    require(observed.size() == expected.size(), label + ": mapping count differs");
    for (size_t i = 0; i < expected.size(); ++i) {
        require(equal(observed[i], expected[i]),
                label + ": mapping differs at rank " + std::to_string(i));
    }
}

std::vector<Mapping> expand(const SharedPath& path) {
    std::vector<Mapping> mappings;
    path.for_each_mapping(flip, [&](const Mapping& mapping, uint64_t rank) {
        require(rank == mappings.size(), "translated ranks are not contiguous");
        mappings.push_back(mapping);
    });
    return mappings;
}

// Independent split specification used by both the direct oracle and the
// mapper callback. Handles 30, 50, and 70 represent deleted/unusable nodes.
std::vector<Mapping> children(const Mapping& original) {
    switch (original.handle) {
        case 10: return {{100, 0, 3}, {102, 0, 4}, {104, 0, 3}};
        case 20: return {{200, 0, 5}, {202, 0, 3}};
        case 40: return {{400, 0, 2}, {402, 0, 4}};
        case 30:
        case 50:
        case 70:
            throw std::runtime_error("mapper was called for an unused source mapping");
        default:
            throw std::runtime_error("missing test split specification");
    }
}

// Expand and clip directly from the old source plus the split specification.
// This deliberately does not use cache internals or translated slice indices.
std::vector<Mapping> translation_oracle(const SharedPath& path) {
    std::vector<Mapping> expected;
    for (const auto& slice : path.slices()) {
        for (uint64_t i = slice.begin; i < slice.end; ++i) {
            const Mapping& original = (*slice.source)[i];
            const uint32_t wanted_begin = i == slice.begin ? slice.first_offset : 0;
            const uint32_t wanted_end = i + 1 == slice.end
                ? slice.last_end : original.length;
            uint32_t cursor = 0;
            bool begin_aligned = wanted_begin == 0;
            bool end_aligned = wanted_end == 0;
            for (const Mapping& child : children(original)) {
                const uint32_t child_begin = cursor;
                const uint32_t child_end = cursor + child.length;
                begin_aligned = begin_aligned || wanted_begin == child_begin ||
                                wanted_begin == child_end;
                end_aligned = end_aligned || wanted_end == child_begin ||
                              wanted_end == child_end;
                if (child_begin >= wanted_begin && child_end <= wanted_end) {
                    expected.push_back(child);
                } else {
                    require(child_end <= wanted_begin || child_begin >= wanted_end,
                            "oracle clipping crosses a child mapping");
                }
                cursor = child_end;
            }
            require(cursor == original.length, "oracle split length differs");
            require(begin_aligned && end_aligned, "oracle boundary is not aligned");
        }
    }
    if (path.is_reverse()) {
        std::reverse(expected.begin(), expected.end());
        for (Mapping& mapping : expected) {
            mapping.handle = flip(mapping.handle);
            mapping.offset = 0;
        }
    }
    return expected;
}

void check_successful_batch() {
    // Used records are 1, 2, 4, and 5. The prefix, middle, and suffix poison
    // records prove that cache translation is restricted to registered ranges.
    auto source = std::make_shared<Source>(std::vector<Mapping>{
        {30, 0, 7}, {10, 0, 10}, {20, 0, 8}, {70, 0, 11},
        {40, 0, 6}, {10, 0, 10}, {50, 0, 9}
    });
    std::weak_ptr<const Source> old_source = source;

    std::vector<SharedPath> paths(5);
    // Multiple split points and clipped ends across more than one old node.
    paths[0].append(source, 1, 3, 3, 5);
    // A reverse path uses a disjoint interval after the unused middle record.
    paths[1].append(source, 4, 6, 2, 7);
    paths[1].reverse_complement();
    // A clipped single-node slice selects only the second child.
    paths[2].append(source, 2, 3, 5, 8);
    // Adjacent old ranges remain distinct because the second skips a prefix.
    paths[3].append(source, 1, 2, 0, 10);
    paths[3].append(source, 2, 3, 5, 8);
    // Repeated old handle 10 at a different source position.
    paths[4].append(source, 5, 6, 0, 7);

    std::vector<std::vector<Mapping>> expected;
    uint64_t remaining = 0;
    Cache cache;
    for (const SharedPath& path : paths) {
        expected.push_back(translation_oracle(path));
        cache.register_path(path);
        remaining += path.slices().size();
    }
    require(cache.remaining() == remaining && !cache.empty(),
            "registration slice count is wrong");

    // Registration itself adds no strong source ownership.
    source.reset();
    require(!old_source.expired(), "registered paths lost their old source");

    uint64_t mapper_calls = 0;
    auto mapper = [&](const Mapping& original, const auto& emit) {
        ++mapper_calls;
        for (const Mapping& child : children(original)) {
            emit(child);
        }
    };

    std::vector<SharedPath> translated;
    translated.reserve(paths.size());
    for (size_t i = 0; i < paths.size(); ++i) {
        const uint64_t consumed = paths[i].slices().size();
        translated.push_back(cache.translate(paths[i], mapper));
        require(translated.back().is_reverse() == paths[i].is_reverse(),
                "translation changed path orientation");
        require_equal(expand(translated.back()), expected[i],
                      "translated path " + std::to_string(i));
        paths[i] = SharedPath();
        remaining -= consumed;
        require(cache.remaining() == remaining,
                "translation consumed the wrong slice count");
    }
    require(mapper_calls == 4,
            "a covered source mapping was translated more or less than once");
    require(cache.empty() && cache.remaining() == 0,
            "cache did not evict its final source entry");
    require(old_source.expired(),
            "cache or translated paths retained the old source");
    require_throws<std::logic_error>(
        [&] { cache.register_path(translated.front()); },
        "registration remained open after translation");
}

void check_invalid_mappers_and_transactionality() {
    auto run_invalid = [](const auto& mapper, const std::string& label) {
        auto source = std::make_shared<Source>(
            std::vector<Mapping>{{80, 0, 10}});
        SharedPath path;
        path.append(source, 0, 1, 0, 10);
        Cache cache;
        cache.register_path(path);
        require_throws<std::invalid_argument>(
            [&] { (void)cache.translate(path, mapper); }, label);
        require(cache.remaining() == 1 && !cache.empty(),
                label + ": failed translation consumed the registration");
    };

    run_invalid([](const Mapping&, const auto& emit) {
        emit(Mapping{800, 1, 10});
    }, "nonzero output offset");
    run_invalid([](const Mapping&, const auto& emit) {
        emit(Mapping{800, 0, 0});
    }, "zero output length");
    run_invalid([](const Mapping&, const auto& emit) {
        emit(Mapping{800, 0, 9});
    }, "short output partition");
    run_invalid([](const Mapping&, const auto& emit) {
        emit(Mapping{800, 0, 6});
        emit(Mapping{802, 0, 5});
    }, "long output partition");

    auto source = std::make_shared<Source>(std::vector<Mapping>{{90, 0, 10}});
    SharedPath clipped;
    clipped.append(source, 0, 1, 3, 10);
    Cache cache;
    cache.register_path(clipped);
    require_throws<std::invalid_argument>([&] {
        (void)cache.translate(clipped, [](const Mapping&, const auto& emit) {
            emit(Mapping{900, 0, 4});
            emit(Mapping{902, 0, 6});
        });
    }, "unaligned clipped boundary");
    require(cache.remaining() == 1,
            "unaligned boundary consumed a registered slice");
    require_throws<std::logic_error>(
        [&] { cache.register_path(clipped); },
        "failed translation did not close registration");

    // The failed source was not cached partially, so a valid retry succeeds.
    SharedPath retried = cache.translate(
        clipped, [](const Mapping&, const auto& emit) {
            emit(Mapping{900, 0, 3});
            emit(Mapping{902, 0, 7});
        });
    require_equal(expand(retried), {{902, 0, 7}},
                  "retry after unaligned boundary");
    require(cache.empty(), "successful retry did not evict the source");
}

void check_owner_identity_and_unregistered_paths() {
    Source source(std::vector<Mapping>{{10, 0, 10}});
    std::shared_ptr<const Source> first(&source, [](const Source*) {});
    std::shared_ptr<const Source> second(&source, [](const Source*) {});
    SharedPath first_path;
    SharedPath second_path;
    first_path.append(first, 0, 1, 0, 10);
    second_path.append(second, 0, 1, 0, 10);
    Cache cache;
    cache.register_path(first_path);
    require_throws<std::logic_error>(
        [&] { cache.register_path(second_path); },
        "different owners at the same raw address were accepted");

    auto other_source = std::make_shared<Source>(
        std::vector<Mapping>{{20, 0, 8}});
    SharedPath unregistered;
    unregistered.append(other_source, 0, 1, 0, 8);
    require_throws<std::logic_error>([&] {
        (void)cache.translate(unregistered,
            [](const Mapping& mapping, const auto& emit) { emit(mapping); });
    }, "unregistered source was translated");
    require(cache.remaining() == 1,
            "unregistered translation changed registered counts");
}

uint64_t mix(uint64_t state, const Mapping& mapping) {
    state ^= mapping.handle + 0x9e3779b97f4a7c15ULL +
             (state << 6) + (state >> 2);
    state ^= (static_cast<uint64_t>(mapping.offset) << 32) | mapping.length;
    return state * 0xbf58476d1ce4e5b9ULL;
}

std::pair<Mapping, Mapping> benchmark_children(const Mapping& original) {
    require(original.offset == 0 && original.length == 20,
            "benchmark source mapping is malformed");
    return {{original.handle * 4, 0, 10},
            {original.handle * 4 + 2, 0, 10}};
}

void print_benchmark(const std::string& mode, uint64_t paths,
                     uint64_t mappings, uint64_t representation_bytes,
                     uint64_t mapper_calls, double construction_seconds,
                     double scan_seconds, uint64_t checksum) {
    std::cout << std::fixed << std::setprecision(6)
              << "{\"mode\":\"" << mode << "\",\"paths\":" << paths
              << ",\"mappings\":" << mappings
              << ",\"representation_owned_bytes\":" << representation_bytes
              << ",\"mapper_calls\":" << mapper_calls
              << ",\"construction_seconds\":" << construction_seconds
              << ",\"scan_seconds\":" << scan_seconds
              << ",\"checksum\":" << checksum << "}\n";
}

void run_benchmark(const std::string& mode) {
    constexpr uint64_t path_count = 10000;
    constexpr uint64_t source_steps = 512;
    constexpr uint64_t translated_steps = 2 * source_steps;
    const auto started = std::chrono::steady_clock::now();

    std::vector<Mapping> source_mappings;
    source_mappings.reserve(source_steps);
    for (uint64_t i = 0; i < source_steps; ++i) {
        source_mappings.push_back({1000 + 2 * i, 0, 20});
    }

    uint64_t representation_bytes = 0;
    uint64_t mapper_calls = 0;
    uint64_t mapping_count = 0;
    uint64_t checksum = 0x123456789abcdef0ULL;

    if (mode == "benchmark-expanded") {
        std::vector<std::vector<Mapping>> paths;
        paths.reserve(path_count);
        for (uint64_t p = 0; p < path_count; ++p) {
            std::vector<Mapping> path;
            path.reserve(translated_steps);
            for (const Mapping& original : source_mappings) {
                auto split = benchmark_children(original);
                path.push_back(split.first);
                path.push_back(split.second);
            }
            if (p % 2 == 1) {
                std::reverse(path.begin(), path.end());
                for (Mapping& mapping : path) {
                    mapping.handle = flip(mapping.handle);
                }
            }
            representation_bytes += path.capacity() * sizeof(Mapping);
            paths.push_back(std::move(path));
        }
        representation_bytes += paths.capacity() * sizeof(std::vector<Mapping>);
        const auto constructed = std::chrono::steady_clock::now();
        for (const auto& path : paths) {
            for (const Mapping& mapping : path) {
                checksum = mix(checksum, mapping);
                ++mapping_count;
            }
        }
        const auto finished = std::chrono::steady_clock::now();
        print_benchmark(mode, path_count, mapping_count, representation_bytes,
                        mapper_calls,
                        std::chrono::duration<double>(constructed - started).count(),
                        std::chrono::duration<double>(finished - constructed).count(),
                        checksum);
        return;
    }

    require(mode == "benchmark-shared", "unknown benchmark mode");
    auto source = std::make_shared<Source>(std::move(source_mappings));
    std::vector<SharedPath> inputs(path_count);
    Cache cache;
    for (uint64_t p = 0; p < path_count; ++p) {
        inputs[p].append(source, 0, source_steps, 0, 20);
        if (p % 2 == 1) {
            inputs[p].reverse_complement();
        }
        cache.register_path(inputs[p]);
    }

    std::vector<SharedPath> paths;
    paths.reserve(path_count);
    auto mapper = [&](const Mapping& original, const auto& emit) {
        ++mapper_calls;
        auto split = benchmark_children(original);
        emit(split.first);
        emit(split.second);
    };
    for (uint64_t p = 0; p < path_count; ++p) {
        paths.push_back(cache.translate(inputs[p], mapper));
        inputs[p] = SharedPath();
    }
    require(cache.empty(), "benchmark cache did not drain");
    require(mapper_calls == source_steps,
            "benchmark source was not translated exactly once");
    source.reset();
    std::vector<SharedPath>().swap(inputs);

    representation_bytes += paths.capacity() * sizeof(SharedPath);
    for (const SharedPath& path : paths) {
        representation_bytes += path.slices().capacity() *
                                sizeof(SharedPath::Slice);
    }
    require(!paths.empty() && !paths.front().slices().empty(),
            "benchmark translated paths are empty");
    representation_bytes += paths.front().slices().front().source->capacity_bytes();

    const auto constructed = std::chrono::steady_clock::now();
    for (const SharedPath& path : paths) {
        path.for_each_mapping(flip, [&](const Mapping& mapping, uint64_t rank) {
            require(rank < translated_steps, "benchmark rank is out of range");
            checksum = mix(checksum, mapping);
            ++mapping_count;
        });
    }
    const auto finished = std::chrono::steady_clock::now();
    require(mapping_count == path_count * translated_steps,
            "benchmark translated mapping count differs");
    print_benchmark(mode, path_count, mapping_count, representation_bytes,
                    mapper_calls,
                    std::chrono::duration<double>(constructed - started).count(),
                    std::chrono::duration<double>(finished - constructed).count(),
                    checksum);
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2) {
            run_benchmark(argv[1]);
            return 0;
        }
        require(argc == 1, "usage: shared_translation_probe [benchmark-expanded|benchmark-shared]");
        check_successful_batch();
        check_invalid_mappers_and_transactionality();
        check_owner_identity_and_unregistered_paths();
        std::cout << "shared translation probe: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "shared translation probe: FAIL: " << error.what() << '\n';
        return 1;
    }
}
