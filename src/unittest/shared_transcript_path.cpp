#include <algorithm>
#include <limits>
#include <memory>
#include <vector>
#include "bdsg/hash_graph.hpp"
#include "../transcriptome.hpp"
#include "catch.hpp"
namespace vg { namespace unittest {
static std::vector<EditedMapping> shared_mappings(const EditedTranscriptPath& path, const HandleGraph& graph) {
    std::vector<EditedMapping> result;
    path.for_each_mapping(graph, [&](const EditedMapping& mapping, uint64_t rank) {
        REQUIRE(rank == result.size());
        result.push_back(mapping);
    });
    return result;
}
using namespace std;
TEST_CASE("Shared edited transcript paths expand with graph orientation", "[transcriptome][shared_transcript_path]") {
    auto graph = make_unique<bdsg::HashGraph>();
    const handle_t one = graph->create_handle("AAAA", 1);
    const handle_t two = graph->create_handle("CCCCC", 2);

    using Source = SharedTranscriptPath<EditedMapping>::Source;
    auto source = make_shared<Source>(vector<EditedMapping>{
        {one, 0, 4}, {graph->flip(two), 0, 5}, {one, 0, 4}
    });

    EditedTranscriptPath whole("whole", gbwt::Path::id(0), true, false);
    whole.shared_path.append(source, 0, 3, 0, 4);
    const CompletedTranscriptPath completed(whole, *graph);
    REQUIRE(completed.path.empty());
    REQUIRE(!completed.shared_path.empty());
    vector<handle_t> completed_walk;
    completed.for_each_handle(*graph, [&](const handle_t handle, uint64_t rank) {
        REQUIRE(rank == completed_walk.size());
        completed_walk.push_back(handle);
    });
    REQUIRE(completed_walk == vector<handle_t>{one, graph->flip(two), one});
    REQUIRE(completed.get_first_node_handle(*graph) == one);
    REQUIRE(completed.resident_size() == completed_walk.size());

    EditedTranscriptPath partial("partial", gbwt::Path::id(0), true, false);
    partial.shared_path.append(source, 0, 3, 1, 3);
    REQUIRE(shared_mappings(partial, *graph) == vector<EditedMapping>{
        {one, 1, 3}, {graph->flip(two), 0, 5}, {one, 0, 3}
    });
    partial.shared_path.reverse_complement();
    REQUIRE(shared_mappings(partial, *graph) == vector<EditedMapping>{
        {graph->flip(one), 1, 3}, {two, 0, 5}, {graph->flip(one), 0, 3}
    });
}


TEST_CASE("Shared slices validate boundaries and preserve copied vector API", "[shared_transcript_path]") {
    bdsg::HashGraph graph;
    auto one = graph.create_handle("AAAA", 1);
    auto source = make_shared<SharedTranscriptPath<EditedMapping>::Source>(vector<EditedMapping>{{one, 0, 4}});
    EditedTranscriptPath clipped("clipped", gbwt::Path::id(0), true, false);
    REQUIRE_THROWS_AS(clipped.shared_path.append(source, 0, 1, 3, 2), invalid_argument);
    REQUIRE_THROWS_AS(clipped.shared_path.append(source, 0, 2, 0, 4), invalid_argument);
    clipped.shared_path.append(source, 0, 1, 1, 3);
    REQUIRE_THROWS_AS(CompletedTranscriptPath(clipped, graph), logic_error);
    EditedTranscriptPath whole("whole", gbwt::Path::id(0), true, false);
    whole.shared_path.append(source, 0, 1, 0, 4);
    CompletedTranscriptPath copy(whole, graph);
    copy.materialize(graph);
    REQUIRE(copy.path == vector<handle_t>{one});
    REQUIRE(copy.shared_path.empty());
    REQUIRE(!whole.shared_path.empty());
}
TEST_CASE("Shared edited paths preserve wide node coordinates", "[shared_transcript_path][edited_mapping]") {
    bdsg::HashGraph graph;
    const auto one = graph.create_handle("A", 1);
    const auto two = graph.create_handle("C", 2);
    using SharedPath = SharedTranscriptPath<EditedMapping>;

    const int64_t first_offset = static_cast<int64_t>(numeric_limits<uint32_t>::max()) + 17;
    const uint64_t last_end = static_cast<uint64_t>(first_offset) + 31;
    const uint64_t node_length = last_end + 101;
    auto wide_source = make_shared<SharedPath::Source>(vector<SharedPath::SourceStep>{
        {one, node_length}
    });

    SharedPath clipped;
    clipped.append(wide_source, 0, 1, first_offset, last_end);
    vector<EditedMapping> mappings;
    clipped.for_each_mapping([&](const handle_t& handle) { return graph.flip(handle); },
                             [&](const EditedMapping& mapping, uint64_t) {
        mappings.push_back(mapping);
    });
    REQUIRE(mappings == vector<EditedMapping>{{one, first_offset, 31}});

    clipped.reverse_complement();
    mappings.clear();
    clipped.for_each_mapping([&](const handle_t& handle) { return graph.flip(handle); },
                             [&](const EditedMapping& mapping, uint64_t) {
        mappings.push_back(mapping);
    });
    REQUIRE(mappings == vector<EditedMapping>{
        {graph.flip(one), static_cast<int64_t>(node_length - last_end), 31}
    });

    SharedPath unrepresentable_whole_node;
    REQUIRE_THROWS_AS(unrepresentable_whole_node.append(
        wide_source, 0, 1, 0, node_length), invalid_argument);

    const uint64_t maximum_edit_length = numeric_limits<int32_t>::max();
    auto maximum_length_source = make_shared<SharedPath::Source>(vector<SharedPath::SourceStep>{
        {two, maximum_edit_length}
    });
    SharedPath maximum_length_path;
    maximum_length_path.append(maximum_length_source, 0, 1, 0, maximum_edit_length);
    mappings.clear();
    maximum_length_path.for_each_mapping(
        [&](const handle_t& handle) { return graph.flip(handle); },
        [&](const EditedMapping& mapping, uint64_t) { mappings.push_back(mapping); });
    REQUIRE(mappings == vector<EditedMapping>{{two, 0, numeric_limits<int32_t>::max()}});

    const auto left = graph.create_handle("G", 3);
    const auto middle = graph.create_handle("T", 4);
    const auto right = graph.create_handle("A", 5);
    SharedPath translated_input;
    translated_input.append(wide_source, 0, 1, first_offset, last_end);
    SharedPath::TranslationCache cache;
    cache.register_path(translated_input);
    auto translated = cache.translate(
        translated_input, [&](const SharedPath::SourceStep& mapping, const auto& emit) {
            REQUIRE(mapping.handle == one);
            emit(SharedPath::SourceStep{left, static_cast<uint64_t>(first_offset)});
            emit(SharedPath::SourceStep{
                middle, last_end - static_cast<uint64_t>(first_offset)});
            emit(SharedPath::SourceStep{right, node_length - last_end});
        });
    REQUIRE(cache.empty());
    mappings.clear();
    translated.for_each_mapping(
        [&](const handle_t& handle) { return graph.flip(handle); },
        [&](const EditedMapping& mapping, uint64_t) { mappings.push_back(mapping); });
    REQUIRE(mappings == vector<EditedMapping>{{middle, 0, 31}});
}

TEST_CASE("Shared translation reuses sources and preserves clipped orientations", "[shared_transcript_path]") {
    bdsg::HashGraph graph;
    auto one = graph.create_handle("AAAA", 1);
    auto two = graph.create_handle("CCCCC", 2);
    vector<handle_t> first, second;
    for (size_t i = 0; i < 4; ++i) first.push_back(graph.create_handle("A", 10 + i));
    for (size_t i = 0; i < 5; ++i) second.push_back(graph.create_handle("C", 20 + i));
    using Shared = SharedTranscriptPath<EditedMapping>;
    auto source = make_shared<Shared::Source>(vector<EditedMapping>{{one, 0, 4}, {two, 0, 5}, {one, 0, 4}});
    Shared forward, reverse;
    forward.append(source, 0, 3, 1, 3);
    reverse = forward;
    reverse.reverse_complement();
    Shared::TranslationCache cache;
    cache.register_path(forward); cache.register_path(reverse);
    size_t calls = 0;
    auto mapper = [&](const Shared::SourceStep& mapping, const auto& emit) {
        ++calls;
        for (const auto& handle : mapping.handle == one ? first : second) {
            emit(Shared::SourceStep{handle, 1});
        }
    };
    auto translated_forward = cache.translate(forward, mapper);
    REQUIRE_FALSE(cache.empty());
    auto translated_reverse = cache.translate(reverse, mapper);
    REQUIRE(cache.empty());
    REQUIRE(calls == 3);
    REQUIRE(translated_forward.slices().front().source == translated_reverse.slices().front().source);
    vector<handle_t> expected(first.begin() + 1, first.end());
    expected.insert(expected.end(), second.begin(), second.end());
    expected.insert(expected.end(), first.begin(), first.begin() + 3);
    vector<handle_t> actual;
    auto flip = [&](const handle_t& h) { return graph.flip(h); };
    translated_forward.for_each_mapping(flip, [&](const EditedMapping& mapping, uint64_t rank) {
        REQUIRE(rank == actual.size()); REQUIRE(mapping.offset == 0); REQUIRE(mapping.length == 1);
        actual.push_back(mapping.handle);
    });
    REQUIRE(actual == expected);
    actual.clear();
    translated_reverse.for_each_mapping(flip, [&](const EditedMapping& mapping, uint64_t) { actual.push_back(mapping.handle); });
    std::reverse(expected.begin(), expected.end());
    for (auto& handle : expected) handle = graph.flip(handle);
    REQUIRE(actual == expected);
    REQUIRE_THROWS_AS(cache.translate(forward, mapper), logic_error);
    REQUIRE_THROWS_AS(cache.register_path(forward), logic_error);
}

TEST_CASE("Shared translation omits unused entries and rejects invalid partitions", "[shared_transcript_path]") {
    bdsg::HashGraph graph;
    auto one = graph.create_handle("AAAA", 1);
    auto two = graph.create_handle("CCCCC", 2);
    using Shared = SharedTranscriptPath<EditedMapping>;
    auto source = make_shared<Shared::Source>(vector<EditedMapping>{{one, 0, 4}, {two, 0, 5}, {one, 0, 4}});
    Shared path; path.append(source, 1, 2, 0, 5);
    Shared::TranslationCache cache; cache.register_path(path);
    graph.destroy_handle(one);
    REQUIRE_THROWS_AS(cache.translate(path, [&](const Shared::SourceStep& mapping,
                                                const auto& emit) {
        emit(Shared::SourceStep{mapping.handle, 2});
    }), invalid_argument);
    REQUIRE_FALSE(cache.empty());
    size_t calls = 0;
    auto translated = cache.translate(path, [&](const Shared::SourceStep& mapping,
                                                 const auto& emit) {
        ++calls; REQUIRE(mapping.handle == two); emit(mapping);
    });
    REQUIRE(calls == 1); REQUIRE(cache.empty()); REQUIRE(translated.size() == 1);
}

TEST_CASE("Shared endpoint scans retain partial mappings and walk ranks", "[shared_transcript_path]") {
    bdsg::HashGraph graph;
    auto one = graph.create_handle("AAAA", 1);
    auto two = graph.create_handle("CCCCC", 2);
    auto source = make_shared<SharedTranscriptPath<EditedMapping>::Source>(vector<EditedMapping>{
        {one, 0, 4}, {two, 0, 5}, {one, 0, 4}});
    for (bool reverse : {false, true}) {
        EditedTranscriptPath path("partial", gbwt::Path::id(0), true, false);
        path.shared_path.append(source, 0, 3, 1, 3);
        if (reverse) path.shared_path.reverse_complement();
        auto expanded = shared_mappings(path, graph);
        vector<pair<EditedMapping, uint64_t>> boundary;
        path.shared_path.for_each_boundary([&](const handle_t& handle) { return graph.flip(handle); },
            [&](const EditedMapping& mapping, uint64_t rank) { boundary.emplace_back(mapping, rank); });
        REQUIRE(boundary.size() == 2);
        REQUIRE(boundary.front() == make_pair(expanded.front(), uint64_t(0)));
        REQUIRE(boundary.back() == make_pair(expanded.back(), uint64_t(2)));
    }
    EditedTranscriptPath one_step("one", gbwt::Path::id(0), true, false);
    one_step.shared_path.append(source, 1, 2, 1, 3);
    size_t count = 0;
    one_step.shared_path.for_each_boundary([&](const handle_t& h) { return graph.flip(h); },
        [&](const EditedMapping& mapping, uint64_t rank) { ++count; REQUIRE(rank == 0); REQUIRE(mapping.length == 2); });
    REQUIRE(count == 1);
}
} }
