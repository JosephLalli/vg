// Tests for streaming path serialization into a fresh PackedGraph.

#include "catch.hpp"

#include <array>
#include <atomic>
#include <limits>
#include <sstream>
#include <streambuf>
#include <string>
#include <vector>

#include "bdsg/packed_graph.hpp"

namespace vg {
namespace unittest {
using namespace std;
using namespace handlegraph;

namespace {

struct PathSpec {
    string name;
    vector<handle_t> steps;
};

static bdsg::PackedGraph make_source_graph() {
    bdsg::PackedGraph graph;
    const auto h3 = graph.create_handle("A", 3);
    const auto h7 = graph.create_handle("CG", 7);
    const auto h75 = graph.create_handle("T", 75);
    const auto h501 = graph.create_handle("GGA", 501);
    const auto removed = graph.create_handle("N", 1001);
    graph.create_edge(h3, h7);
    graph.create_edge(h7, h75);
    graph.create_edge(graph.flip(h501), h3);
    graph.create_edge(removed, h3);
    graph.destroy_edge(removed, h3);
    graph.destroy_handle(removed);
    return graph;
}

static string serialized_bytes(const bdsg::PackedGraph& graph) {
    stringstream stream;
    graph.serialize(stream);
    return stream.str();
}

static bdsg::PackedGraph clone_without_paths(const bdsg::PackedGraph& graph) {
    stringstream stream(serialized_bytes(graph));
    bdsg::PackedGraph clone;
    clone.deserialize(stream);
    return clone;
}

static vector<PathSpec> make_paths(const bdsg::PackedGraph& graph) {
    const array<handle_t, 8> pattern = {
        graph.flip(graph.get_handle(501)), graph.get_handle(3), graph.get_handle(3),
        graph.flip(graph.get_handle(75)), graph.get_handle(7), graph.get_handle(501),
        graph.flip(graph.get_handle(3)), graph.get_handle(75)
    };
    const array<size_t, 11> lengths = {{0, 1, 127, 128, 129, 255, 256, 257, 1023, 1024, 1025}};
    const array<string, 11> names = {{
        "a", "aa", "aaa", "a-a", "a_a", "path/a", "path/aa", "aaaaaaa", "a1a1", "aa-final-2", "aa-final"
    }};
    vector<PathSpec> paths;
    paths.reserve(lengths.size());
    for (size_t i = 0; i < lengths.size(); ++i) {
        PathSpec path{names[i], {}};
        path.steps.reserve(lengths[i]);
        for (size_t j = 0; j < lengths[i]; ++j) {
            path.steps.push_back(pattern[(j + i) % pattern.size()]);
        }
        paths.push_back(std::move(path));
    }
    return paths;
}

static vector<PathSpec> make_named_paths(const bdsg::PackedGraph& graph,
                                         const vector<string>& names,
                                         size_t length) {
    const array<handle_t, 6> pattern = {
        graph.get_handle(3), graph.flip(graph.get_handle(501)),
        graph.get_handle(7), graph.flip(graph.get_handle(75)),
        graph.get_handle(3), graph.get_handle(501)
    };
    vector<PathSpec> paths;
    paths.reserve(names.size());
    for (size_t i = 0; i < names.size(); ++i) {
        PathSpec path{names[i], {}};
        path.steps.reserve(length);
        for (size_t rank = 0; rank < length; ++rank) {
            path.steps.push_back(pattern[(rank + i) % pattern.size()]);
        }
        paths.push_back(std::move(path));
    }
    return paths;
}

static void append_paths(bdsg::PackedGraph& graph, const vector<PathSpec>& paths) {
    for (const auto& path : paths) {
        const auto path_handle = graph.create_path_handle(path.name, false);
        for (const auto& handle : path.steps) {
            graph.append_step(path_handle, handle);
        }
    }
}

static void require_paths(const bdsg::PackedGraph& graph, const vector<PathSpec>& paths) {
    REQUIRE(graph.get_path_count() == paths.size());
    for (const auto& expected : paths) {
        const auto path = graph.get_path_handle(expected.name);
        REQUIRE_FALSE(graph.get_is_circular(path));
        REQUIRE(graph.get_step_count(path) == expected.steps.size());
        size_t rank = 0;
        graph.for_each_step_in_path(path, [&](const step_handle_t& step) {
            REQUIRE(rank < expected.steps.size());
            REQUIRE(as_integer(graph.get_handle_of_step(step)) == as_integer(expected.steps[rank]));
            ++rank;
        });
        REQUIRE(rank == expected.steps.size());
    }
}

static void stream_paths(
    bdsg::PackedGraph& graph, const vector<PathSpec>& paths, ostream& stream,
    size_t workers = 1, size_t extra_memory_budget = 0,
    const bdsg::PathSerializationStageCallback& stage = {}) {
    graph.serialize_with_paths(stream, paths.size(),
        [&](size_t index) { return paths.at(index).name; },
        [&](size_t index) { return paths.at(index).steps.size(); },
        [&](size_t index, const auto& emit) {
            for (const auto& handle : paths.at(index).steps) {
                emit(handle);
            }
        }, workers, extra_memory_budget, stage);
}

class LimitedOutputBuffer : public std::streambuf {
public:
    explicit LimitedOutputBuffer(size_t capacity) : remaining(capacity) {
    }

protected:
    std::streamsize xsputn(const char*, std::streamsize count) override {
        const size_t requested = count < 0 ? 0 : static_cast<size_t>(count);
        const size_t accepted = std::min(remaining, requested);
        remaining -= accepted;
        return static_cast<std::streamsize>(accepted);
    }

    int_type overflow(int_type character) override {
        if (traits_type::eq_int_type(character, traits_type::eof())) {
            return traits_type::not_eof(character);
        }
        if (remaining == 0) {
            return traits_type::eof();
        }
        --remaining;
        return character;
    }

private:
    size_t remaining;
};

static bdsg::PackedGraph make_source_with_deleted_paths() {
    bdsg::PackedGraph graph = make_source_graph();
    const auto reference = graph.create_path_handle("reference", false);
    graph.append_step(reference, graph.get_handle(3));
    graph.append_step(reference, graph.flip(graph.get_handle(75)));
    graph.append_step(reference, graph.get_handle(3));
    graph.append_step(reference, graph.flip(graph.get_handle(501)));
    graph.destroy_path(reference);

    const auto alternate = graph.create_path_handle("alternate", false);
    graph.append_step(alternate, graph.flip(graph.get_handle(7)));
    graph.append_step(alternate, graph.get_handle(501));
    graph.append_step(alternate, graph.flip(graph.get_handle(7)));
    graph.destroy_path(alternate);
    return graph;
}

} // namespace

TEST_CASE("PackedGraph streams fresh paths as ordinary serialization", "[packed][path][stream]") {
    bdsg::PackedGraph source = make_source_graph();
    REQUIRE(source.get_path_count() == 0);
    REQUIRE(source.can_serialize_with_paths());
    const string source_before = serialized_bytes(source);
    const vector<PathSpec> paths = make_paths(source);

    bdsg::PackedGraph expected = clone_without_paths(source);
    append_paths(expected, paths);
    const string expected_bytes = serialized_bytes(expected);

    for (size_t workers : {size_t(1), size_t(2), size_t(4), size_t(24)}) {
        stringstream actual;
        stream_paths(source, paths, actual, workers, size_t(128) << 20);
        REQUIRE(actual.str() == expected_bytes);

        actual.seekg(0);
        bdsg::PackedGraph loaded;
        loaded.deserialize(actual);
        require_paths(loaded, paths);
        REQUIRE(serialized_bytes(loaded) == expected_bytes);
    }
    REQUIRE(serialized_bytes(source) == source_before);
    REQUIRE(source.get_path_count() == 0);
}

TEST_CASE("PackedGraph streams zero paths without changing serialization", "[packed][path][stream]") {
    bdsg::PackedGraph source = make_source_graph();
    const string expected = serialized_bytes(source);
    stringstream actual;
    source.serialize_with_paths(actual, 0,
        [](size_t) { return string(); },
        [](size_t) { return size_t(0); },
        [](size_t, const auto&) {});
    REQUIRE(actual.str() == expected);
    REQUIRE(serialized_bytes(source) == expected);
}

TEST_CASE("PackedGraph streams after all source paths are deleted", "[packed][path][stream]") {
    bdsg::PackedGraph source = make_source_with_deleted_paths();
    REQUIRE(source.get_path_count() == 0);
    REQUIRE(source.can_serialize_with_paths());
    const string source_before = serialized_bytes(source);

    vector<PathSpec> paths = make_paths(source);
    // Reuse the deleted name. This exercises preserved deleted-name metadata and
    // the base path ID history while ordinary append creates a fresh live path.
    paths.front().name = "reference";

    bdsg::PackedGraph expected = clone_without_paths(source);
    append_paths(expected, paths);
    const string expected_bytes = serialized_bytes(expected);

    stringstream actual;
    stream_paths(source, paths, actual, 4, size_t(128) << 20);
    REQUIRE(actual.str() == expected_bytes);
    REQUIRE(serialized_bytes(source) == source_before);
    REQUIRE(source.get_path_count() == 0);

    actual.seekg(0);
    bdsg::PackedGraph loaded;
    loaded.deserialize(actual);
    require_paths(loaded, paths);
    REQUIRE(serialized_bytes(loaded) == expected_bytes);

    stringstream zero_paths;
    source.serialize_with_paths(zero_paths, 0,
        [](size_t) { return string(); },
        [](size_t) { return size_t(0); },
        [](size_t, const auto&) {});
    REQUIRE(zero_paths.str() == source_before);
}

TEST_CASE("PackedGraph bounded parallel streaming reports stages and falls back", "[packed][path][stream]") {
    bdsg::PackedGraph source = make_source_graph();
    vector<PathSpec> paths;
    // Exercise multiple real packing batches, including repeated nodes and
    // partial pages at their boundaries, rather than relying on tiny blocks.
    for (size_t repeat = 0; repeat < 8; ++repeat) {
        for (auto path : make_paths(source)) {
            path.name += "-batch-" + to_string(repeat);
            paths.push_back(std::move(path));
        }
    }
    bdsg::PackedGraph expected = clone_without_paths(source);
    append_paths(expected, paths);
    const string expected_bytes = serialized_bytes(expected);

    const size_t budget = size_t(128) << 20;
    vector<bdsg::PathSerializationStage> stages;
    stringstream parallel;
    stream_paths(source, paths, parallel, 4, budget,
                 [&](const bdsg::PathSerializationStage& record) {
                     stages.push_back(record);
                 });
    REQUIRE(parallel.str() == expected_bytes);
    REQUIRE_FALSE(stages.empty());
    bool saw_parallel_scan = false;
    bool saw_parallel_local = false;
    for (const auto& record : stages) {
        REQUIRE(record.wall_seconds >= 0.0);
        REQUIRE(record.cpu_seconds >= 0.0);
        REQUIRE(record.planned_extra_bytes <= budget);
        saw_parallel_scan = saw_parallel_scan ||
            (record.name == "membership-preflight" && record.workers > 1);
        saw_parallel_local = saw_parallel_local ||
            (record.name == "local-paths" && record.workers > 1);
    }
    REQUIRE(saw_parallel_scan);
    REQUIRE(saw_parallel_local);

    // One byte cannot retain even the path prefix, so dispatch must choose the
    // unchanged serial implementation before emitting a stage or output byte.
    stages.clear();
    stringstream fallback;
    stream_paths(source, paths, fallback, 24, 1,
                 [&](const bdsg::PathSerializationStage& record) {
                     stages.push_back(record);
                 });
    REQUIRE(fallback.str() == expected_bytes);
    REQUIRE(stages.empty());
}

TEST_CASE("PackedGraph parallel path callback failures join workers", "[packed][path][stream]") {
    const size_t path_count = 4;
    const size_t declared = 2048;
    const size_t budget = size_t(128) << 20;

    SECTION("preflight catches callback underrun and overrun before output") {
        for (size_t emitted : {declared - 1, declared + 1}) {
            bdsg::PackedGraph source = make_source_graph();
            stringstream output;
            REQUIRE_THROWS(source.serialize_with_paths(
                output, path_count,
                [](size_t i) { return string("parallel-count-") + to_string(i); },
                [&](size_t) { return declared; },
                [&](size_t i, const auto& emit) {
                    const size_t count = i == 2 ? emitted : declared;
                    for (size_t rank = 0; rank < count; ++rank) {
                        emit(source.get_handle(rank % 2 == 0 ? 3 : 7));
                    }
                }, 4, budget));
            REQUIRE(output.str().empty());
        }
    }

    SECTION("preflight callback exception is rethrown after joining") {
        bdsg::PackedGraph source = make_source_graph();
        stringstream output;
        REQUIRE_THROWS(source.serialize_with_paths(
            output, path_count,
            [](size_t i) { return string("parallel-throw-") + to_string(i); },
            [&](size_t) { return declared; },
            [&](size_t i, const auto& emit) {
                for (size_t rank = 0; rank < declared; ++rank) {
                    if (i == 1 && rank == 17) {
                        throw runtime_error("path callback failure");
                    }
                    emit(source.get_handle(3));
                }
            }, 4, budget));
        REQUIRE(output.str().empty());
    }

    SECTION("stable replay callback exception joins workers") {
        bdsg::PackedGraph source = make_source_graph();
        array<atomic<size_t>, path_count> calls;
        for (auto& count : calls) {
            count.store(0);
        }
        stringstream output;
        REQUIRE_THROWS(source.serialize_with_paths(
            output, path_count,
            [](size_t i) { return string("parallel-replay-") + to_string(i); },
            [&](size_t) { return declared; },
            [&](size_t i, const auto& emit) {
                const size_t invocation = calls[i].fetch_add(1);
                for (size_t rank = 0; rank < declared; ++rank) {
                    if (i == 1 && invocation == 2 && rank == 31) {
                        throw runtime_error("replay callback failure");
                    }
                    emit(source.get_handle(rank % 3 == 0 ? 501 : 3));
                }
            }, 4, budget));
    }
}

TEST_CASE("Bounded output reuses ring slots without losing order or cancellation", "[packed][path][stream]") {
    const size_t blocks = 257;
    auto payload = [](size_t block) { return string((block * 17) % 255, char('a' + block % 26)); };
    stringstream expected;
    for (size_t block = 0; block < blocks; ++block) {
        expected << block << ':' << payload(block) << ';';
    }
    for (size_t workers : {size_t(1), size_t(2), size_t(4), size_t(24)}) {
        stringstream actual;
        bdsg::internal::bounded_ordered_output(actual, blocks, workers, 31,
            [&](size_t block, ostream& out) { out << payload(block); },
            [](size_t block, ostream& out) { out << block << ':'; },
            [](size_t, ostream& out) { out << ';'; });
        REQUIRE(actual.str() == expected.str());
    }

    SECTION("producer exception after repeated slot reuse wakes waiters") {
        stringstream actual;
        REQUIRE_THROWS(bdsg::internal::bounded_ordered_output(actual, blocks, 4, 31,
            [&](size_t block, ostream& out) {
                if (block == 137) { throw runtime_error("late producer failure"); }
                out << payload(block);
            }));
    }
    SECTION("writer exception after repeated slot reuse wakes waiters") {
        LimitedOutputBuffer buffer(16384);
        ostream out(&buffer);
        REQUIRE_THROWS(bdsg::internal::bounded_ordered_output(out, blocks, 4, 31,
            [&](size_t block, ostream& output) { output << payload(block); }));
    }
}

TEST_CASE("Profiled PagedVector replay preserves append history", "[packed][path][stream]") {
    constexpr size_t page_width = 256;
    vector<uint64_t> values(3 * page_width + 19, 0);
    for (size_t i = 0; i < values.size(); ++i) {
        if (i % 11 != 0) {
            values[i] = (i * 104729) ^ (uint64_t(1) << (i % 47));
        }
    }
    bdsg::PagedVector<page_width> resident;
    for (uint64_t value : values) {
        resident.push_back(value);
    }
    stringstream expected;
    resident.serialize(expected);

    const vector<size_t> profile_offsets = {0, 257, 511, values.size()};
    stringstream actual;
    bdsg::PagedVector<page_width>::serialize_generated_profiled(
        actual, values.size(), profile_offsets, 4,
        [&](size_t block, const auto& emit) {
            for (size_t i = profile_offsets[block];
                 i < profile_offsets[block + 1]; ++i) {
                emit(values[i]);
            }
        },
        [&](size_t pages, const auto& write_page) -> size_t {
            for (size_t page = 0; page < pages; ++page) {
                const size_t begin = page * page_width;
                const size_t end = std::min(values.size(), begin + page_width);
                write_page(actual, page, [&](const auto& emit) {
                    for (size_t i = begin; i < end; ++i) {
                        emit(values[i]);
                    }
                });
            }
            return pages;
        });
    REQUIRE(actual.str() == expected.str());

    SECTION("page underrun is rejected") {
        stringstream output;
        REQUIRE_THROWS(bdsg::PagedVector<page_width>::serialize_generated_profiled(
            output, values.size(), profile_offsets, 4,
            [&](size_t block, const auto& emit) {
                for (size_t i = profile_offsets[block];
                     i < profile_offsets[block + 1]; ++i) {
                    emit(values[i]);
                }
            },
            [&](size_t, const auto& write_page) -> size_t {
                write_page(output, 0, [&](const auto& emit) {
                    for (size_t i = 0; i + 1 < page_width; ++i) {
                        emit(values[i]);
                    }
                });
                return 1;
            }));
    }
    SECTION("page overrun is rejected") {
        stringstream output;
        REQUIRE_THROWS(bdsg::PagedVector<page_width>::serialize_generated_profiled(
            output, values.size(), profile_offsets, 4,
            [&](size_t block, const auto& emit) {
                for (size_t i = profile_offsets[block];
                     i < profile_offsets[block + 1]; ++i) {
                    emit(values[i]);
                }
            },
            [&](size_t, const auto& write_page) -> size_t {
                write_page(output, 0, [&](const auto& emit) {
                    for (size_t i = 0; i <= page_width; ++i) {
                        emit(values[i]);
                    }
                });
                return 1;
            }));
    }
}

TEST_CASE("PackedGraph parallel memberships cross waves and oversized paths",
          "[packed][path][stream]") {
    bdsg::PackedGraph source = make_source_graph();
    const array<handle_t, 7> pattern = {
        source.get_handle(3), source.flip(source.get_handle(501)),
        source.get_handle(3), source.flip(source.get_handle(7)),
        source.get_handle(75), source.get_handle(3), source.get_handle(501)
    };
    vector<PathSpec> paths;
    paths.reserve(17);
    for (size_t path = 0; path < 17; ++path) {
        const size_t length = path == 0 ? size_t(300001) : size_t(30003 + path % 3);
        PathSpec spec{"wave-" + to_string(path), {}};
        spec.steps.reserve(length);
        for (size_t rank = 0; rank < length; ++rank) {
            spec.steps.push_back(pattern[(rank + path) % pattern.size()]);
        }
        paths.push_back(std::move(spec));
    }

    bdsg::PackedGraph expected = clone_without_paths(source);
    append_paths(expected, paths);
    const string expected_bytes = serialized_bytes(expected);
    vector<bdsg::PathSerializationStage> stages;
    stringstream actual;
    const size_t budget = size_t(50) << 20;
    stream_paths(source, paths, actual, 4, budget,
                 [&](const bdsg::PathSerializationStage& record) {
                     stages.push_back(record);
                 });
    REQUIRE_FALSE(stages.empty());
    REQUIRE(actual.str() == expected_bytes);
    bool saw_microblocks = false;
    for (const auto& record : stages) {
        REQUIRE(record.planned_extra_bytes <= budget);
        saw_microblocks = saw_microblocks ||
            (record.name == "membership-offsets" && record.blocks > 8);
    }
    REQUIRE(saw_microblocks);
}

TEST_CASE("PackedGraph parallel streaming propagates output failure", "[packed][path][stream]") {
    bdsg::PackedGraph source = make_source_graph();
    const vector<PathSpec> paths = make_paths(source);
    bdsg::PackedGraph expected = clone_without_paths(source);
    append_paths(expected, paths);
    const string bytes = serialized_bytes(expected);

    LimitedOutputBuffer buffer(bytes.size() / 2);
    ostream output(&buffer);
    REQUIRE_THROWS(stream_paths(source, paths, output, 4, size_t(128) << 20));
}

TEST_CASE("PackedGraph rejects generated local-link count overflow", "[packed][path][stream]") {
    bdsg::PackedGraph source = make_source_graph();
    stringstream output;
    REQUIRE_THROWS(source.serialize_with_paths(
        output, 2,
        [](size_t i) { return string("overflow-") + to_string(i); },
        [](size_t) { return std::numeric_limits<size_t>::max() / 2 + 1; },
        [](size_t, const auto&) {}, 2, size_t(128) << 20));
    REQUIRE(output.str().empty());
}

TEST_CASE("PackedGraph rejects invalid streamed paths", "[packed][path][stream]") {
    SECTION("source graph already has a path") {
        bdsg::PackedGraph source = make_source_graph();
        source.create_path_handle("existing");
        REQUIRE_FALSE(source.can_serialize_with_paths());
        stringstream output;
        REQUIRE_THROWS(source.serialize_with_paths(output, 0,
            [](size_t) { return string(); },
            [](size_t) { return size_t(0); },
            [](size_t, const auto&) {}));
    }

    SECTION("path generator underruns or overruns its declared size") {
        for (size_t emitted : {size_t(1), size_t(3)}) {
            bdsg::PackedGraph source = make_source_graph();
            stringstream output;
            REQUIRE_THROWS(source.serialize_with_paths(output, 1,
                [](size_t) { return string("count"); },
                [](size_t) { return size_t(2); },
                [&](size_t, const auto& emit) {
                    for (size_t i = 0; i < emitted; ++i) {
                        emit(source.get_handle(3));
                    }
                }));
        }
    }

    SECTION("path generator emits a nonexistent handle") {
        bdsg::PackedGraph source = make_source_graph();
        stringstream output;
        REQUIRE_THROWS(source.serialize_with_paths(output, 1,
            [](size_t) { return string("missing"); },
            [](size_t) { return size_t(1); },
            [](size_t, const auto& emit) { emit(as_handle(uint64_t(999999))); }));
    }

    SECTION("path names must be distinct") {
        bdsg::PackedGraph source = make_source_graph();
        stringstream output;
        REQUIRE_THROWS(source.serialize_with_paths(output, 2,
            [](size_t) { return string("duplicate"); },
            [](size_t) { return size_t(0); },
            [](size_t, const auto&) {}));
    }
}

TEST_CASE("PackedGraph lookup candidate rejects duplicate names atomically",
          "[packed][path][stream][metadata-lookup]") {
    constexpr size_t path_count = 4;
    constexpr size_t declared = 1024;
    const size_t budget = size_t(128) << 20;

    for (size_t workers : {size_t(1), size_t(4)}) {
        for (size_t duplicate_at : {size_t(1), size_t(3)}) {
            bdsg::PackedGraph source = make_source_graph();
            const string source_before = serialized_bytes(source);
            array<size_t, path_count> name_calls{};
            size_t size_calls = 0;
            size_t step_calls = 0;
            stringstream output;
            bool saw_inner_duplicate = false;
            try {
                source.serialize_with_paths(
                    output, path_count,
                    [&](size_t i) {
                        ++name_calls.at(i);
                        if (i == 0 || i == duplicate_at) {
                            return string("duplicate");
                        }
                        return string("unique-") + to_string(i);
                    },
                    [&](size_t) {
                        ++size_calls;
                        return declared;
                    },
                    [&](size_t, const auto& emit) {
                        ++step_calls;
                        for (size_t rank = 0; rank < declared; ++rank) {
                            emit(source.get_handle(rank % 2 == 0 ? 3 : 7));
                        }
                    }, workers, budget);
            }
            catch (const runtime_error& error) {
                saw_inner_duplicate = true;
                REQUIRE(string(error.what()).find("already exists") != string::npos);
            }
            REQUIRE(saw_inner_duplicate);
            REQUIRE(output.str().empty());
            REQUIRE(serialized_bytes(source) == source_before);
            for (size_t i = 0; i < path_count; ++i) {
                REQUIRE(name_calls[i] == (i <= duplicate_at ? 1 : 0));
            }
            REQUIRE(size_calls == (workers == 1 ? duplicate_at + 1 : path_count));
            REQUIRE(step_calls == (workers == 1 ? duplicate_at : 0));
        }
    }
}

TEST_CASE("PackedGraph lookup candidate preserves empty and throwing-name failures",
          "[packed][path][stream][metadata-lookup]") {
    constexpr size_t path_count = 4;
    constexpr size_t declared = 1024;
    const size_t budget = size_t(128) << 20;

    SECTION("empty name is rejected before output") {
        for (size_t workers : {size_t(1), size_t(4)}) {
            bdsg::PackedGraph source = make_source_graph();
            const string source_before = serialized_bytes(source);
            array<size_t, path_count> name_calls{};
            size_t size_calls = 0;
            size_t step_calls = 0;
            stringstream output;
            bool saw_empty_name = false;
            try {
                source.serialize_with_paths(
                    output, path_count,
                    [&](size_t i) {
                        ++name_calls.at(i);
                        return i == 1 ? string() : string("nonempty-") + to_string(i);
                    },
                    [&](size_t) {
                        ++size_calls;
                        return declared;
                    },
                    [&](size_t, const auto& emit) {
                        ++step_calls;
                        for (size_t rank = 0; rank < declared; ++rank) {
                            emit(source.get_handle(3));
                        }
                    }, workers, budget);
            }
            catch (const runtime_error& error) {
                saw_empty_name = true;
                REQUIRE(string(error.what()).find("no name") != string::npos);
            }
            REQUIRE(saw_empty_name);
            REQUIRE(output.str().empty());
            REQUIRE(serialized_bytes(source) == source_before);
            REQUIRE((name_calls == array<size_t, path_count>{{1, 1, 0, 0}}));
            REQUIRE(size_calls == (workers == 1 ? 2 : path_count));
            REQUIRE(step_calls == (workers == 1 ? 1 : 0));
        }
    }

    SECTION("throwing name callback is invoked once through the failure") {
        for (size_t workers : {size_t(1), size_t(4)}) {
            for (size_t throw_at : {size_t(1), size_t(3)}) {
                bdsg::PackedGraph source = make_source_graph();
                const string source_before = serialized_bytes(source);
                array<size_t, path_count> name_calls{};
                size_t size_calls = 0;
                size_t step_calls = 0;
                stringstream output;
                bool saw_callback_failure = false;
                try {
                    source.serialize_with_paths(
                        output, path_count,
                        [&](size_t i) {
                            ++name_calls.at(i);
                            if (i == throw_at) {
                                throw runtime_error("name callback sentinel");
                            }
                            return string("callback-") + to_string(i);
                        },
                        [&](size_t) {
                            ++size_calls;
                            return declared;
                        },
                        [&](size_t, const auto& emit) {
                            ++step_calls;
                            for (size_t rank = 0; rank < declared; ++rank) {
                                emit(source.get_handle(3));
                            }
                        }, workers, budget);
                }
                catch (const runtime_error& error) {
                    saw_callback_failure = true;
                    REQUIRE(string(error.what()) == "name callback sentinel");
                }
                REQUIRE(saw_callback_failure);
                REQUIRE(output.str().empty());
                REQUIRE(serialized_bytes(source) == source_before);
                for (size_t i = 0; i < path_count; ++i) {
                    REQUIRE(name_calls[i] == (i <= throw_at ? 1 : 0));
                }
                REQUIRE(size_calls == (workers == 1 ? throw_at + 1 : path_count));
                REQUIRE(step_calls == (workers == 1 ? throw_at : 0));
            }
        }
    }
}

TEST_CASE("PackedGraph lookup candidate preserves new-character and tombstone bytes",
          "[packed][path][stream][metadata-lookup]") {
    auto exercise = [&](bdsg::PackedGraph& source) {
        REQUIRE(source.get_path_count() == 0);
        REQUIRE(source.can_serialize_with_paths());
        const string source_before = serialized_bytes(source);
        const vector<PathSpec> paths = make_named_paths(
            source, {"reference", "fresh!one", "alternate", "fresh?two"}, 1024);
        bdsg::PackedGraph expected = clone_without_paths(source);
        append_paths(expected, paths);
        const string expected_bytes = serialized_bytes(expected);

        for (size_t workers : {size_t(1), size_t(4)}) {
            vector<bdsg::PathSerializationStage> stages;
            stringstream output;
            stream_paths(source, paths, output, workers, size_t(128) << 20,
                         [&](const bdsg::PathSerializationStage& stage) {
                             stages.push_back(stage);
                         });
            REQUIRE(output.str() == expected_bytes);
            REQUIRE(serialized_bytes(source) == source_before);
            if (workers == 1) {
                REQUIRE(stages.empty());
            }
            else {
                bool used_parallel_preflight = false;
                for (const auto& stage : stages) {
                    used_parallel_preflight = used_parallel_preflight ||
                        (stage.name == "membership-preflight" && stage.workers > 1);
                }
                REQUIRE(used_parallel_preflight);
            }
        }
    };

    bdsg::PackedGraph fresh = make_source_graph();
    exercise(fresh);
    bdsg::PackedGraph tombstoned = make_source_with_deleted_paths();
    exercise(tombstoned);
}

}
}
